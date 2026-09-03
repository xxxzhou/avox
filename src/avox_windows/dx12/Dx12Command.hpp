#pragma once

#include <d3d12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>

#include "../WinCommon.hpp"

namespace avox {

// 对应VkCommand里完成一些简单GPU操作,如资源的复制
class Dx12Command {
 private:
  /* data */
  ID3D12Device* device = nullptr;
  ID3D12CommandQueue* queue = nullptr;
  MComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
  MComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
  MComPtr<ID3D12Fence> fence = nullptr;
  HANDLE fenceEvent = nullptr;
  // 停止信号
  UINT64 fenceValue = 0;

 public:
  Dx12Command(/* args */);
  ~Dx12Command();

 private:
  bool initCommand();
  void closeCommand();

 public:
  bool setDevice(ID3D12Device* device, ID3D12CommandQueue* queue);

  void beginCommand();
  void addBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                  D3D12_RESOURCE_STATES after);
  void copyBuffer(ID3D12Resource* dest, ID3D12Resource* src);
  void endCommand();
  void executeCommand(ID3D12Fence* fence = nullptr, uint64_t fenceValue = 0);
};

}
