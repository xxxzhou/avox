#include "Dx11Resource.hpp"

#include <d3d11_1.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include "../dx12/Dx12Helper.hpp"

namespace avox {

void Dx11Resource::releaseResource() {
  if (bBufferInit) {
    releaseResourceImpl();
    bBufferInit = false;
  }
}

bool Dx11Resource::initResource(ID3D11Device* deviceDx11) {
  releaseResource();
  bBufferInit = createResource(deviceDx11);
  return bBufferInit;
}

void Dx11CSResource::setCpuWrite(bool bCpuWrite_) { bCpuWrite = bCpuWrite_; }

void Dx11CSResource::setSharedType(DX11SharedType shared) {
  if (shared != sharedType) {
    releaseResource();
    sharedType = shared;
    if (sharedType != DX11SharedType::no) {
      // bNoView = true;
    }
  }
}

void Dx11CSResource::setNoView(bool bNoView_) {
  if (bNoView != bNoView_) {
    releaseResource();
    bNoView = bNoView_;
  }
}

void Dx11CSResource::setOnlyUAV(bool bOnlyUAV_) { bOnlyUAV = bOnlyUAV_; }
void Dx11CSResource::setVideoOut(bool bVideo_) { bVideoOut = bVideo_; }

void Dx11CSResource::setGUI(bool gui) { bGUI = gui; }

void Dx11CSResource::releaseResourceImpl() {
  srvView.Reset();
  uavView.Reset();
}

void Dx11Texture::setTextureSize(int32_t width_, int32_t height_,
                                 DXGI_FORMAT format_) {
  if (width != width_ || height != height_ || format != format_) {
    releaseResource();
    width = width_;
    height = height_;
    format = format_;
  }
}

bool Dx11Texture::createResource(ID3D11Device* deviceDx11) {
  D3D11_TEXTURE2D_DESC textureDesc = {};
  textureDesc.Width = width;
  textureDesc.Height = height;
  textureDesc.Format = format;
  textureDesc.MipLevels = 1;
  textureDesc.ArraySize = 1;
  textureDesc.MiscFlags = 0;
  textureDesc.BindFlags =
      D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  if (bCpuWrite) {
    // DYNAMIC的对应cpu可写,STAGING CPU可读写,否则cpu没任何权限
    textureDesc.Usage = D3D11_USAGE_DYNAMIC;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  }
  if (bVideoOut) {
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET |
                            D3D11_BIND_SHADER_RESOURCE |
                            D3D11_BIND_UNORDERED_ACCESS;
  }
  if (bGUI) {
    textureDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
  }
  if (sharedType != DX11SharedType::no) {
    if (sharedType == DX11SharedType::shared) {
      textureDesc.MiscFlags =
          textureDesc.MiscFlags | D3D11_RESOURCE_MISC_SHARED;
    } else if (sharedType == DX11SharedType::sharedNT) {
      textureDesc.MiscFlags = textureDesc.MiscFlags |
                              D3D11_RESOURCE_MISC_SHARED |
                              D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    }
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
  }
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0;
  HRESULT result = 0;
  if (cpuData) {
    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = cpuData;
    result = deviceDx11->CreateTexture2D(&textureDesc, &initData, &texture);
  } else {
    result = deviceDx11->CreateTexture2D(&textureDesc, nullptr, &texture);
  }
  bBufferInit = SUCCEEDED(result);
  if (bBufferInit && !bNoView) {
    if (!bOnlyUAV) {
      createBufferSRV(deviceDx11, texture.Get(), &srvView);
    }
    if (!bCpuWrite) {
      createBufferUAV(deviceDx11, texture.Get(), &uavView);
    }
  }
  return bBufferInit;
}

bool Dx11Texture::updateResource(ID3D11DeviceContext* ctxDx11) {
  if (!cpuData || !bBufferInit) {
    return false;
  }
  int dataType = width * height * sizeDxFormatElement(format);
  if ((width % 32) != 0) {
    log(LogLevel::warn, "texture width ", width, " no mod 32.");
  }
  bool bUpdate = updateDx11Resource(ctxDx11, texture.Get(), cpuData, dataType);
  return bUpdate;
}

void Dx11Texture::releaseResourceImpl() {
  Dx11CSResource::releaseResourceImpl();
  texture.Reset();
}

void Dx11Buffer::setBufferSize(int32_t elementSize, int32_t dataType,
                               bool rawBuffer) {
  if (this->elementSize != elementSize || this->dataType != dataType ||
      this->bRawBuffer != rawBuffer) {
    releaseResource();
    this->dataType = dataType;
    this->elementSize = elementSize;
    this->bRawBuffer = rawBuffer;
  }
}

bool Dx11Buffer::createResource(ID3D11Device* deviceDx11) {
  D3D11_BUFFER_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.ByteWidth = elementSize * dataType;
  desc.StructureByteStride = elementSize;  // elementSize;
  // Structured Buffer
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  if (bRawBuffer) {
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  }
  // UAV SAV
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  if (bCpuWrite) {
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.Usage = D3D11_USAGE_DYNAMIC;
  }
  // 可以在不同的上下文使用
  if (sharedType != DX11SharedType::no) {
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX |
                     D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  }
  HRESULT result = 0;
  if (cpuData) {
    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = cpuData;
    result = deviceDx11->CreateBuffer(&desc, &initData, &buffer);
  } else
    result = deviceDx11->CreateBuffer(&desc, nullptr, &buffer);
  // D3D11 ERROR : ID3D11Device::CreateBuffer : When creating a buffer with
  // the MiscFlag D3D11_RESOURCE_MISC_BUFFER_STRUCTURED specified, the
  // StructureByteStride must be greater than zero, no greater than 2048, and
  // a multiple of 4.[STATE_CREATION ERROR #2097339:
  bBufferInit = SUCCEEDED(result);
  if (bBufferInit && !bNoView) {
    createBufferSRV(deviceDx11, buffer.Get(), &srvView);
    if (!bCpuWrite) {
      createBufferUAV(deviceDx11, buffer.Get(), &uavView);
    }
  }
  return bBufferInit;
}

bool Dx11Buffer::updateResource(ID3D11DeviceContext* ctxDx11) {
  if (!cpuData || !bBufferInit) return false;
  int elementByteCount = elementSize * dataType;
  bool bUpdate =
      updateDx11Resource(ctxDx11, buffer.Get(), cpuData, elementByteCount);
  return bUpdate;
}

void Dx11Buffer::releaseResourceImpl() {
  Dx11CSResource::releaseResourceImpl();
  buffer.Reset();
}

void Dx11Constant::setBufferSize(int32_t dataType) {
  if (byteDataSize != dataType) {
    releaseResource();
    byteDataSize = dataType;
  }
}

bool Dx11Constant::createResource(ID3D11Device* deviceDx11) {
  // Constant默认不需要重建功能
  if (buffer != nullptr) {
    return true;
  }
  HRESULT hr;
  D3D11_BUFFER_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.ByteWidth = (UINT)(ceil(byteDataSize / 16.0)) * 16;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.MiscFlags = 0;
  desc.StructureByteStride = 0;
  if (cpuData) {
    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = cpuData;
    initData.SysMemPitch = 0;
    initData.SysMemSlicePitch = 0;
    hr = deviceDx11->CreateBuffer(&desc, &initData, &buffer);
  } else {
    hr = deviceDx11->CreateBuffer(&desc, nullptr, &buffer);
  }
  bBufferInit = SUCCEEDED(hr);
  return bBufferInit;
}

bool Dx11Constant::updateResource(ID3D11DeviceContext* ctxDx11) {
  if (!cpuData) {
    return false;
  }
  ctxDx11->UpdateSubresource(buffer.Get(), 0, nullptr, cpuData, 0, 0);
  return true;
}

void Dx11Constant::releaseResourceImpl() { buffer.Reset(); }

ShaderInclude::ShaderInclude(std::string modelName, std::string rctype,
                             int32_t rcId) {
  this->rcId = rcId;
  // readResouce(modelName.c_str(), rcId, rctype.c_str(), strRes, length);
}

HRESULT __stdcall ShaderInclude::Open(D3D_INCLUDE_TYPE IncludeType,
                                      LPCSTR pFileName, LPCVOID pParentData,
                                      LPCVOID* ppData, UINT* pBytes) {
  *ppData = (const void*)strRes.c_str();
  *pBytes = length;
  return true;
}

HRESULT __stdcall ShaderInclude::Close(LPCVOID pData) { return true; }

}

// // 生成一个信号
// HANDLE eventHandle = CreateEventEx(nullptr, false, false,
// EVENT_ALL_ACCESS);
// // 等待Fence完成通知信号
// fence->SetEventOnCompletion(AVOX_DX11_MUTEX_READ, eventHandle);
// // 等待信号完成,不设时间
// DWORD result = WaitForSingleObject(eventHandle, 0);
// if (result == WAIT_OBJECT_0 && pBuffer) {
//     d3dcontext->CopyResource(texture, pBuffer);
// }
// CloseHandle(eventHandle);