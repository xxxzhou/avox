#include "Dx12Helper.hpp"

#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

namespace avox {

Dx12SharedTex::Dx12SharedTex() {
  dx12Command = std::make_unique<Dx12Command>();
}

Dx12SharedTex::~Dx12SharedTex() {
  dx12Command->setDevice(nullptr, nullptr);
  oldTex = nullptr;
}

void Dx12SharedTex::release() {
  Dx11SharedTex::release();
  dx12Command->setDevice(nullptr, nullptr);
  oldTex = nullptr;
}

void Dx12SharedTex::interopTexture(IRenderContext* renderContext,
                                        bool shared2tex) {
  IDx12Context* dx12Context = dynamic_cast<IDx12Context*>(renderContext);
  LOGASSERT(dx12Context, "renderContext is not dx12 context.");
  ID3D12Resource* dxtexture = dx12Context->getTexture();
  if (!dxtexture || !sharedHandle) {
    return;
  }
  ID3D12Device* d3ddevice = dx12Context->getDevice();
  ID3D12CommandQueue* d3dqueue = dx12Context->getCommandQueue();
  bool bUpdate = dx12Command->setDevice(d3ddevice, d3dqueue);
  // 如果外部Device没有改变,其sharedTex/sharedFenceDx12缓存下来使用,比较费时
  if (bUpdate) {
    if (sharedTex) {
      sharedTex = nullptr;      
    }
    HRESULT hr = d3ddevice->OpenSharedHandle(
        sharedHandle, __uuidof(ID3D12Resource), (void**)(&sharedTex));
    if (FAILED(hr) || sharedTex == nullptr) {
      AVOX_WIN_LOG(hr, "open dx12 shared texture error.");
      return;
    }
    if (sharedFenceDx12) {
      sharedFenceDx12 = nullptr;
    }
    hr = d3ddevice->OpenSharedHandle(interopFenceHandle, __uuidof(ID3D12Fence),
                                     (void**)(&sharedFenceDx12));
    if (FAILED(hr) || sharedFenceDx12 == nullptr) {
      AVOX_WIN_LOG(hr, "open dx12 shared fence error.");
      return;
    }
  }
  // 需要重新记录
  mustResetCmd = bUpdate || oldTex != dxtexture || oldShard2Tex != shared2tex;
  if (mustResetCmd) {
    oldTex = dxtexture;
    oldShard2Tex = shared2tex;
  }
  // log(LogLevel::warn,
  //            "dx12 SharedTex GetCompletedValue s2t:", shared2tex);
  uint64_t currentFence = sharedFenceDx12->GetCompletedValue();
  if (shared2tex) {
    if (mustResetCmd) {
      dx12Command->beginCommand();
      dx12Command->addBarrier(dxtexture, D3D12_RESOURCE_STATE_COMMON,
                              D3D12_RESOURCE_STATE_COPY_DEST);
      dx12Command->copyBuffer(dxtexture, sharedTex.Get());
      dx12Command->addBarrier(dxtexture, D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_COMMON);
      dx12Command->endCommand();
    }
    if (currentFence % 2 == AVOX_DX11_MUTEX_READ) {
      dx12Command->executeCommand(sharedFenceDx12.Get(), currentFence + 1);
    }
  } else if (!shared2tex) {
    // log(LogLevel::info, "texture to shared fence:", currentFence);
    if (mustResetCmd) {
      dx12Command->beginCommand();
      dx12Command->addBarrier(dxtexture, D3D12_RESOURCE_STATE_COMMON,
                              D3D12_RESOURCE_STATE_COPY_SOURCE);
      dx12Command->copyBuffer(sharedTex.Get(), dxtexture);
      dx12Command->addBarrier(dxtexture, D3D12_RESOURCE_STATE_COPY_SOURCE,
                              D3D12_RESOURCE_STATE_COMMON);
      dx12Command->endCommand();
    }
    if (currentFence % 2 == AVOX_DX11_MUTEX_WRITE) {
      dx12Command->executeCommand(sharedFenceDx12.Get(), currentFence + 1);
    }
  }
}

bool createDevice12(ID3D12Device** deviceDx12) {
  UINT dxgiFactoryFlags = 0;
  *deviceDx12 = nullptr;
#if defined(DEBUG) || defined(_DEBUG)
  MComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif
  MComPtr<IDXGIFactory4> factory = nullptr;
  HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
  MComPtr<IDXGIAdapter1> adapter = nullptr;
  for (UINT adapterIndex = 0;
       DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter);
       ++adapterIndex) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      adapter = nullptr;
      ;
      continue;
    }
    log(LogLevel::info, "create dx12 device select graphics card:",
        utf8TString(desc.Description), " deviceId:", desc.DeviceId,
        " graphics memory:", desc.DedicatedVideoMemory / (1024 * 1024), "M");
    // 检查能否正确创建设备
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    _uuidof(ID3D12Device), nullptr))) {
      break;
    }
    adapter = nullptr;
  }
  hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(deviceDx12));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "avox_win createDevice12 fail.");
    return false;
  }
  return true;
}

bool getImageFormat(ID3D12Resource* texture, ImageFormat& imageFormat) {
  D3D12_RESOURCE_DESC desc = texture->GetDesc();
  imageFormat = {};
  imageFormat.width = desc.Width;
  imageFormat.height = desc.Height;
  imageFormat.imageType = getImageType(desc.Format);
  return true;
}
}