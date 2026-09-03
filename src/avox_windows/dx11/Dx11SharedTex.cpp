#include "Dx11SharedTex.hpp"

namespace avox {

Dx11SharedTex::Dx11SharedTex() {
  // 默认创建NT共享
  sharedType = DX11SharedType::sharedNT;
  texture = std::make_unique<Dx11Texture>();
  texture->setSharedType(sharedType);
}

// Dx11SharedTex
Dx11SharedTex::~Dx11SharedTex() { release(); }

void Dx11SharedTex::release() {
  releaseHandles();
  // texture.reset();
  interopTex.Reset();
  fence.Reset();
  interopFence.Reset();
  context4.Reset();
  interopContext.Reset();
  interopContext4.Reset();
  interopDevice = nullptr;
  device = nullptr;
}

void Dx11SharedTex::releaseHandles() {
  if (sharedHandle) {
    CloseHandle(sharedHandle);
    sharedHandle = nullptr;
  }
  if (interopFenceHandle) {
    CloseHandle(interopFenceHandle);
    interopFenceHandle = nullptr;
  }
}

Dx11Texture* Dx11SharedTex::getDx11Texture() { return texture.get(); }

bool Dx11SharedTex::initTexture(ID3D11Device* deviceDx11) {
  release();
  device = deviceDx11;
  sharedType = texture->getSharedType();
  vformat = {};
  vformat.width = texture->getWidth();
  vformat.height = texture->getHeight();
  vformat.imageType = getImageType(texture->getFormat());
  if (vformat.width <= 0 || vformat.height <= 0) {
    LOGFLF(LogLevel::warn, " texture size is invalid,width:", vformat.width,
           " height:", vformat.height);
    return false;
  }
  bool bInit = texture->initResource(deviceDx11);
  if (!bInit) {
    LOGFLF(LogLevel::warn, "init texture failed");
    return false;
  }
  if (sharedType == DX11SharedType::no) {
    return true;
  }
  MComPtr<ID3D11DeviceContext> d3dcontext = nullptr;
  device->GetImmediateContext(&d3dcontext);
  if (!d3dcontext) {
    LOGFLF(LogLevel::warn, "get immediate context failed");
    return false;
  }
  HRESULT hr = d3dcontext->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                          (void**)&context4);
  if (FAILED(hr)) {
    AVOX_WIN_LOG_RETURN_FALSE(hr, "query ID3D11DeviceContext4 failed");
  }
  if (sharedType == DX11SharedType::sharedNT) {
    // NT共享句柄
    MComPtr<IDXGIResource1> dxGIResource1 = nullptr;
    HRESULT hr = texture->texture->QueryInterface(__uuidof(IDXGIResource1),
                                                  (void**)(&dxGIResource1));
    // DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
    hr = dxGIResource1->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr, &sharedHandle);
    if (FAILED(hr)) {
      AVOX_WIN_LOG_RETURN_FALSE(hr, "texture create shared handle failed");
    }
    // 共享Fence
    MComPtr<ID3D11Device5> device5 = nullptr;
    hr = deviceDx11->QueryInterface(__uuidof(ID3D11Device5), (void**)&device5);
    if (FAILED(hr)) {
      AVOX_WIN_LOG_RETURN_FALSE(hr, "get device5 failed");
    }
    // 创建可跨 API 共享的 fence（支持 Vulkan 导入）
    hr = device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                              (void**)&fence);
    if (FAILED(hr)) {
      AVOX_WIN_LOG_RETURN_FALSE(hr, "create shared fence failed");
    }
    interopFenceHandle = nullptr;
    // 创建 fence 的共享句柄（NT 句柄），供外部 API（如 Vulkan）导入
    hr = fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                   &interopFenceHandle);
    if (FAILED(hr)) {
      AVOX_WIN_LOG_RETURN_FALSE(hr, "fence create shared handle failed");
    }
  } else {
    sharedHandle = getDx11SharedHandle(texture->texture.Get());
  }
  return sharedHandle != nullptr;
}

void Dx11SharedTex::updateInteropDevice(ID3D11Device* d3ddevice) {
  // 针对设备做缓存,当设备变动后重新缓存
  if (interopDevice == d3ddevice && interopFence && interopContext4) {
    return;
  }
  if (sharedType == DX11SharedType::sharedNT) {
    MComPtr<ID3D11Device1> d3ddevice1 = nullptr;
    HRESULT hr =
        d3ddevice->QueryInterface(__uuidof(ID3D11Device1), (void**)&d3ddevice1);
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, "query ID3D11Device1 failed");
      return;
    }
    interopContext.Reset();
    d3ddevice->GetImmediateContext(&interopContext);
    if (!interopContext) {
      return;
    }
    MComPtr<ID3D11Device5> d3ddevice5 = nullptr;
    hr =
        d3ddevice->QueryInterface(__uuidof(ID3D11Device5), (void**)&d3ddevice5);
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, "query ID3D11Device5 failed");
      return;
    }
    interopTex.Reset();
    hr = d3ddevice1->OpenSharedResource1(
        sharedHandle, __uuidof(ID3D11Texture2D), (void**)(&interopTex));
    if (FAILED(hr) || interopTex == nullptr) {
      AVOX_WIN_LOG(hr, "open dx11 shared texture error.");
      return;
    }
    interopSrv.Reset();
    D3D11_TEXTURE2D_DESC texDesc = {};
    interopTex->GetDesc(&texDesc);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    // 创建SRV
    hr = d3ddevice->CreateShaderResourceView(interopTex.Get(), &srvDesc,
                                             &interopSrv);
    if (FAILED(hr) || interopSrv == nullptr) {
      AVOX_WIN_LOG(hr, "create srv view error.");
      return;
    }
    interopContext4.Reset();
    hr = interopContext->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                        (void**)&interopContext4);
    if (FAILED(hr)) {
      AVOX_WIN_LOG(hr, "query ID3D11DeviceContext4 failed");
      return;
    }
    interopFence.Reset();
    hr = d3ddevice5->OpenSharedFence(interopFenceHandle, __uuidof(ID3D11Fence),
                                     (void**)(&interopFence));
    if (FAILED(hr) || interopFence == nullptr) {
      AVOX_WIN_LOG(hr, "open dx11 shared fence error.");
      return;
    }
    interopDevice = d3ddevice;
  }
}

void Dx11SharedTex::copyTexture(ID3D11Texture2D* destTex, bool shared2tex) {
  if (!device || !texture || !texture->texture || !fence) {
    return;
  }
  if (!context4) {
    return;
  }
  uint64_t currentFence = fence->GetCompletedValue();
  if (shared2tex) {
    if (currentFence % 2 == AVOX_DX11_MUTEX_READ) {
      context4->CopyResource(destTex, texture->texture.Get());
      context4->Signal(fence.Get(), currentFence + 1);
    }
  } else {
    if (currentFence % 2 == AVOX_DX11_MUTEX_WRITE) {
      context4->CopyResource(texture->texture.Get(), destTex);
      context4->Signal(fence.Get(), currentFence + 1);
    }
  }
}

void Dx11SharedTex::interopTexture(IRenderContext* renderContext,
                                   bool shared2tex) {
  IDx11Context* dx11Context = static_cast<IDx11Context*>(renderContext);
  // LOGASSERT(dx11Context, "renderContext is not dx11 context.")
  ID3D11Device* d3ddevice = dx11Context->getDevice();
  ID3D11Texture2D* dxtexture = dx11Context->getTexture();
  if (!d3ddevice || !dxtexture || !sharedHandle) {
    return;
  }
  // 交互的上下文有变化,更新缓存数据,因为从NT句柄取pBuffer比较费时
  updateInteropDevice(d3ddevice);
  if (sharedType == DX11SharedType::sharedNT) {
    if (!interopTex || !interopFence || !interopContext) {
      return;
    }
    if (shared2tex) {
      uint64_t currentFence = interopFence->GetCompletedValue();
      if (currentFence % 2 == AVOX_DX11_MUTEX_READ) {
        //  log(LogLevel::info, "x1:", currentFence);
        interopContext->CopyResource(dxtexture, interopTex.Get());
        HRESULT hr =
            interopContext4->Signal(interopFence.Get(), currentFence + 1);
      }
    } else {
      // 设计成不等待,二种状态
      uint64_t currentFence = interopFence->GetCompletedValue();
      if (currentFence % 2 == AVOX_DX11_MUTEX_WRITE) {
        //  log(LogLevel::info, "x2:", currentFence);
        interopContext->CopyResource(interopTex.Get(), dxtexture);
        // 从结果上来看,设定的fence value不能少于本身
        HRESULT hr =
            interopContext4->Signal(interopFence.Get(), currentFence + 1);
        if (FAILED(hr)) {
          // AVOX_WIN_LOG(hr, "shard texture signal failed");
        }
      }
    }
  }
}

ID3D11Texture2D* Dx11SharedTex::getInteropTexture() {
  if (interopTex) {
    return interopTex.Get();
  }
  return nullptr;
}

ID3D11ShaderResourceView* Dx11SharedTex::getInteropSrv() {
  if (interopSrv) {
    return interopSrv.Get();
  }
  return nullptr;
}

void Dx11SharedTex::setDevice(ID3D11Device* deviceDx11) {
  device = deviceDx11;
  MComPtr<ID3D11DeviceContext> d3dcontext = nullptr;
  device->GetImmediateContext(&d3dcontext);
  if (!d3dcontext) {
    LOGFLF(LogLevel::warn, "get immediate context failed");
    return;
  }
  HRESULT hr = d3dcontext->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                          (void**)&context4);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "query ID3D11DeviceContext4 failed");
  }
}

void Dx11SharedTex::setInteropDevice(ID3D11Device* device) {
  updateInteropDevice(device);
}

bool Dx11SharedTex::canRead() {
  if (!fence) {
    return false;
  }
  uint64_t currentFence = fence->GetCompletedValue();
  // 当前 Device 可以读取（对方写入完成）
  return currentFence % 2 == AVOX_DX11_MUTEX_READ;
}

bool Dx11SharedTex::canWrite() {
  if (!fence) {
    return false;
  }
  uint64_t currentFence = fence->GetCompletedValue();
  // 当前 Device 可以写入（对方读取完成）
  return currentFence % 2 == AVOX_DX11_MUTEX_WRITE;
}

void Dx11SharedTex::signalFence() {
  if (!fence || !context4) {
    return;
  }
  uint64_t currentFence = fence->GetCompletedValue();
  context4->Signal(fence.Get(), currentFence + 1);
}

bool Dx11SharedTex::canInteropRead() {
  if (!interopFence) {
    return false;
  }
  uint64_t currentFence = interopFence->GetCompletedValue();
  return currentFence % 2 == AVOX_DX11_MUTEX_READ;
}

bool Dx11SharedTex::canInteropWrite() {
  if (!interopFence) {
    return false;
  }
  uint64_t currentFence = interopFence->GetCompletedValue();
  return currentFence % 2 == AVOX_DX11_MUTEX_WRITE;
}

void Dx11SharedTex::signalInteropFence() {
  if (!interopFence || !interopContext4) {
    return;
  }
  uint64_t currentFence = interopFence->GetCompletedValue();
  HRESULT hr = interopContext4->Signal(interopFence.Get(), currentFence + 1);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "shared texture interop signal failed");
  }
}

void Dx11SharedTex::logTex() { logTexture(device, texture->texture.Get()); }

void Dx11SharedTex::logInteropTex() {
  logTexture(interopDevice, interopTex.Get());
}

bool copySharedToTexture(IDx11Context* context, Dx11SharedTex* sharedTex) {
  if (!sharedTex) {
    return false;
  }
  sharedTex->interopTexture(context, true);
  return true;
}

bool copyTextureToShared(IDx11Context* context, Dx11SharedTex* sharedTex) {
  if (!sharedTex) {
    return false;
  }
  sharedTex->interopTexture(context, false);
  return true;
}
}