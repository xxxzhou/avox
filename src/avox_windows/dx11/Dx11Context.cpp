#include "Dx11Context.hpp"

namespace avox {

void Dx11Context::setDevice(ID3D11Device *device_) {
  device = device_;
  device->GetImmediateContext(&d3dcontext);
}

void Dx11Context::setTexture(ID3D11Texture2D *texture_) { texture = texture_; }

ID3D11Device *Dx11Context::getDevice() { return device; }

ID3D11Texture2D *Dx11Context::getTexture() { return texture; }

ID3D11DeviceContext *Dx11Context::getContext() { return d3dcontext.Get(); }

bool fetchTexture(IDx11Context *context, ImageBuffer *buffer) {
  ID3D11Device *dxdevice = context->getDevice();
  ID3D11Texture2D *dxtexture = context->getTexture();
  if (buffer == nullptr || dxdevice == nullptr || dxtexture == nullptr) {
    return false;
  }
  MComPtr<ID3D11DeviceContext> d3dcontext = nullptr;
  dxdevice->GetImmediateContext(&d3dcontext);
  // 得到纹理描述
  MComPtr<ID3D11Texture2D> temp = nullptr;
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  dxtexture->GetDesc(&desc);
  if (!(desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) {
    bool bCreate = createAndCopyToDebugBuf(dxdevice, dxtexture, &temp);
    if (!bCreate) {
      return false;
    }
  }
  // 初始化buffer
  ImageFormat format = {};
  getImageFormat(dxtexture, format);
  // map数据
  D3D11_MAPPED_SUBRESOURCE MappedResource;
  HRESULT hr = d3dcontext->Map(temp ? temp.Get() : dxtexture, 0, D3D11_MAP_READ,
                               0, &MappedResource);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "Map failed");
    return false;
  }
  format.rowPitch = MappedResource.RowPitch;
  // 根据format申请内存
  buffer->setImageFormat(format);
  uint8_t *mapData = (uint8_t *)MappedResource.pData;
  uint8_t *destData = buffer->getPointer();
  memcpy(destData, mapData, MappedResource.RowPitch * desc.Height);
  d3dcontext->Unmap(temp ? temp.Get() : dxtexture, 0);
  return true;
}

}
