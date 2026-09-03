#include "Dx12Command.hpp"
#include "../WinCommon.hpp"
namespace avox {

Dx12Command::Dx12Command(/* args */) {}

Dx12Command::~Dx12Command() {
  // executeCommand();
  closeCommand();
}

bool Dx12Command::initCommand() {
  if (!device) {
    return true;
  }
  // D3D12_COMMAND_LIST_TYPE_COPY D3D12_COMMAND_LIST_TYPE_DIRECT
  D3D12_COMMAND_LIST_TYPE commandType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  HRESULT hr = device->CreateCommandAllocator(commandType,
                                              IID_PPV_ARGS(&commandAllocator));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create command allocator failed");
    return false;
  }
  // 创建CommandList
  hr = device->CreateCommandList(0, commandType, commandAllocator.Get(), nullptr,
                                 IID_PPV_ARGS(&commandList));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create command list failed");
    return false;
  }
  commandList->SetName(L"Dx12Command commandList");
  commandList->Close();
  // 创建同步
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create fence failed");
    return false;
  }
  fenceValue = 1;
  fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  return true;
}

void Dx12Command::closeCommand() {
  if (!queue || !commandList) {
    return;
  }
  // 等待完成并关闭ID3D12CommandAllocator
  // 当队列所有命令完成时,fence为1
  queue->Signal(fence.Get(), fenceValue);
  // fence信号为1时触发
  fence->SetEventOnCompletion(fenceValue, fenceEvent);
  // 等待fenceEvent触发
  WaitForSingleObject(fenceEvent, INFINITE);
  // 重置命令列表
  commandAllocator->Reset();
  if (fenceEvent) {
    CloseHandle(fenceEvent);
    fenceEvent = nullptr;
  }
  // 关闭命令列表
  commandAllocator = nullptr;;
  commandList = nullptr;;
}

bool Dx12Command::setDevice(ID3D12Device* devicex, ID3D12CommandQueue* dqueue) {
  if (device == devicex && queue == dqueue) {
    return false;
  }
  // 先安全关闭老的
  closeCommand();
  device = devicex;
  queue = dqueue;
  // 再重新生成新的
  initCommand();
  return true;
}

void Dx12Command::beginCommand() {
  if (!commandAllocator) {
    return;
  }
  commandAllocator->Reset();
  MComPtr<ID3D12PipelineState> pipelineState = nullptr;
  // 设置记录状态
  commandList->Reset(commandAllocator.Get(), pipelineState.Get());
}

void Dx12Command::addBarrier(ID3D12Resource* resource,
                             D3D12_RESOURCE_STATES before,
                             D3D12_RESOURCE_STATES after) {
  if (!queue || !commandList) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &barrier);
}

void Dx12Command::copyBuffer(ID3D12Resource* dest, ID3D12Resource* src) {
  if (!queue || !commandList) {
    return;
  }
  commandList->CopyResource(dest, src);
}

void Dx12Command::endCommand() {
  if (!queue || !commandList) {
    return;
  }
  commandList->Close();
}

void Dx12Command::executeCommand(ID3D12Fence* sfence, uint64_t sfenceValue) {
  if (!queue || !commandList) {
    return;
  }
  ID3D12CommandList* ppCommandLists[] = {commandList.Get()};
  queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
  // 当queue执行完成后,给ID3D12Fence设置特定值
  if (sfence) {
    queue->Signal(sfence, sfenceValue);
  }
}

}
