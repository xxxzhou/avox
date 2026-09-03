#include "Dx12Context.hpp"

namespace avox {

void Dx12Context::setDevice(ID3D12Device *device_) { device = device_; }

void Dx12Context::setCommandQueue(ID3D12CommandQueue *commandQueue_) {
  commandQueue = commandQueue_;
}

void Dx12Context::setTexture(ID3D12Resource *texture_) { texture = texture_; }

ID3D12Device *Dx12Context::getDevice() { return device; }

ID3D12CommandQueue *Dx12Context::getCommandQueue() { return commandQueue; }

ID3D12Resource *Dx12Context::getTexture() { return texture; }

bool fetchTexture(ID3D12Resource *texture, ImageBuffer *buffer) {
  if (!texture || !buffer) {
    return false;
  }
  ImageFormat format = {};
  getImageFormat(texture, format);
  buffer->setImageFormat(format);
  int32_t imageSize = buffer->getBufferSize();
  D3D12_RANGE readRange = {0, imageSize};
  void *mapPointer = buffer->getPointer();
  HRESULT hr = texture->Map(0, &readRange, &mapPointer);
  if (!SUCCEEDED(hr)) {
    return false;
  }
  texture->Unmap(0, &readRange);
  return true;
}

}
