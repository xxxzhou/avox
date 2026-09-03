#pragma once

#include <d3d12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>

#include "../WinCommon.hpp"
#include "../dx11/Dx11SharedTex.hpp"
#include "Dx12Command.hpp"
#include "Dx12Context.hpp"

namespace avox {

// 一般用来中转,与DX12外部环境(如UE4)交互到DX11环境
// 然后可以交换DX11/CUDA/Vulkan
class Dx12SharedTex : public Dx11SharedTex {
 public:
  Dx12SharedTex();
  virtual ~Dx12SharedTex();

 private:
  std::unique_ptr<Dx12Command> dx12Command = nullptr;
  // ID3D12Resource* texture = nullptr;
  // dx12设备从NT共享句柄打开的资源(实际是DX11资源)
  MComPtr<ID3D12Resource> sharedTex = nullptr;
  MComPtr<ID3D12Fence> sharedFenceDx12 = nullptr;
  ID3D12Resource* oldTex = nullptr;
  bool oldShard2Tex = false;
  bool mustResetCmd = false;

 public:
  virtual void interopTexture(IRenderContext* renderContext,
                                   bool shared2tex) override;
  virtual void release() override;
};

bool createDevice12(ID3D12Device** deviceDx11);

bool getImageFormat(ID3D12Resource* texture, ImageFormat& imageFormat);

}