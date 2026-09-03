#pragma once
#include "../../avox/AvoxLayer.h"
#include "../WinCommon.hpp"
#include "Dx12Helper.hpp"

namespace avox {

class IDx12Context : public IRenderContext {
 public:
  IDx12Context() = default;
  virtual ~IDx12Context() = default;

  // IRenderContext
 public:
  virtual RenderType getRenderType() override { return RenderType::D3D12; }

 public:
  virtual ID3D12Device* getDevice() = 0;
  virtual ID3D12CommandQueue* getCommandQueue() = 0;
  virtual ID3D12Resource* getTexture() = 0;
};

class Dx12Context : public IDx12Context {
 public:
  Dx12Context() {};
  virtual ~Dx12Context() {};

 protected:
  ID3D12Device* device = nullptr;
  ID3D12CommandQueue* commandQueue = nullptr;
  ID3D12Resource* texture = nullptr;

 public:
  void setDevice(ID3D12Device* device);
  void setCommandQueue(ID3D12CommandQueue* commandQueue);
  void setTexture(ID3D12Resource* texture);

 public:
  virtual ID3D12Device* getDevice() override;
  virtual ID3D12CommandQueue* getCommandQueue() override;
  virtual ID3D12Resource* getTexture() override;
};

bool fetchTexture(ID3D12Resource *texture, ImageBuffer *buffer);

}