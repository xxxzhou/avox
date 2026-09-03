#include "Dx12Window.hpp"

#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

namespace avox {

LRESULT CALLBACK DX12WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                             LPARAM lParam) {
  Dx12Window *window =
      reinterpret_cast<Dx12Window *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  if (!window) {
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
  }
  return window->handleMessage(uMsg, wParam, lParam);
}

Dx12Window::Dx12Window(/* args */) { renderType = RenderType::D3D12; }

Dx12Window::~Dx12Window() {
  if (fenceEvent) {
    CloseHandle(fenceEvent);
    fenceEvent = nullptr;
  }
}

LRESULT Dx12Window::handleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  PAINTSTRUCT ps;
  HDC hdc;
  switch (msg) {
  case WM_PAINT: {
    hdc = BeginPaint(surface, &ps);
    EndPaint(surface, &ps);
  } break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(surface, msg, wparam, lparam);
    break;
  }
  return 0;
}

void Dx12Window::onInitWin() {
  HMODULE wdInstance = GetModuleHandle(nullptr);
  surface = createWin32Window((HINSTANCE)wdInstance, (HWND)hwnd, wdWidth,
                              wdHeight, wdTitle.c_str(), DX12WndProc, this);
  // create device12
  initDevice();
}

bool Dx12Window::initDevice() {
  // 创建设备
  UINT dxgiFactoryFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
  MComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif
  MComPtr<IDXGIFactory4> factory = nullptr;
  HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
  MComPtr<IDXGIAdapter1> adapter = nullptr;
  for (UINT adapterIndex = 0;
       DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter);
       ++adapterIndex) {
    DXGI_ADAPTER_DESC1 desc = {};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      adapter = nullptr;
      ;
      continue;
    }
    // 检查能否正确创建设备
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    _uuidof(ID3D12Device), nullptr))) {
      log(LogLevel::info, "create dx12 device select graphics card:",
          utf8TString(desc.Description), " deviceId:", desc.DeviceId,
          " graphics memory:", desc.DedicatedVideoMemory / (1024 * 1024), "M");
      break;
    }
    adapter = nullptr;
  }
  hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(&device));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "avox_win createDevice12 fail.");
    return false;
  }
  D3D12_COMMAND_LIST_TYPE commandType = D3D12_COMMAND_LIST_TYPE_COPY;
  // 创建CommandQueue
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create command queue failed");
    return false;
  }
  queue->SetName(L"dx12 window queue");
  // 创建交换链
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = frameCount;
  swapChainDesc.Width = wdWidth;
  swapChainDesc.Height = wdHeight;
  // DXGI_FORMAT_B8G8R8A8_UNORM DXGI_FORMAT_R8G8B8A8_UNORM
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;
  MComPtr<IDXGISwapChain1> swapChain1 = nullptr;
  hr = factory->CreateSwapChainForHwnd(queue.Get(), surface, &swapChainDesc,
                                       nullptr, nullptr, &swapChain1);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create swap chain failed");
    return false;
  }
  swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain));
  // 创建RTV(render target view) descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = frameCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create RTV descriptor heap failed");
    return false;
  }
  rtvDescriptorSize =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  // create frame resource
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      rtvHeap->GetCPUDescriptorHandleForHeapStart());
  for (int32_t i = 0; i < frameCount; i++) {
    swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
    // renderTargets[i]->SetName(L"xxx");
    device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
    rtvHandle.ptr += rtvDescriptorSize;
  }
  // 创建CommandAllocator
  hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      IID_PPV_ARGS(&commandAllocator));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create command allocator failed");
    return false;
  }
  // 创建CommandList
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 commandAllocator.Get(), nullptr,
                                 IID_PPV_ARGS(&commandList));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create command list failed");
    return false;
  }
  commandList->SetName(L"Dx12Window commandList");
  commandList->Close();
  // 创建同步
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 create fence failed");
    return false;
  }
  // 创建纹理(别的Queue不能操作当前的交换链RTV)
  D3D12_HEAP_PROPERTIES hProperties = {};
  hProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
  hProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  hProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  hProperties.CreationNodeMask = 1;
  hProperties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc = renderTargets[0]->GetDesc();
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  hr = device->CreateCommittedResource(
      &hProperties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture));
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "dx12 window create texture failed");
    return false;
  }
  texture->SetName(L"dx12window texture");
  frameIndex = swapChain->GetCurrentBackBufferIndex();
  fenceValue = 1;
  fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  return true;
}

void Dx12Window::onTickWin() {
  commandAllocator->Reset();
  commandList->Reset(commandAllocator.Get(), pipelineState.Get());
  // 用户处理背景
  dispatch(&IWindowOb::onRenderWindow);
  // 转换renderTargets[frameIndex]状态
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = renderTargets[frameIndex].Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &barrier);
  // 写入背景
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      rtvHeap->GetCPUDescriptorHandleForHeapStart());
  rtvHandle.ptr += rtvDescriptorSize * frameIndex;
  commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
  // float clearColor[] = {0.1f, 0.2f, 0.8f, 1.0f};
  // commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  D3D12_RESOURCE_BARRIER tbarrier = {};
  tbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  tbarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  tbarrier.Transition.pResource = renderTargets[frameIndex].Get();
  tbarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  tbarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  tbarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &tbarrier);
  commandList->CopyResource(renderTargets[frameIndex].Get(), texture.Get());
  // 转换renderTargets[frameIndex]状态
  D3D12_RESOURCE_BARRIER xbarrier = {};
  xbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  xbarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  xbarrier.Transition.pResource = renderTargets[frameIndex].Get();
  xbarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  xbarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  xbarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &xbarrier);
  // 关闭命令列表准备提交
  commandList->Close();
  // 执行命令列表
  ID3D12CommandList *ppCommandLists[] = {commandList.Get()};
  queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
  // 交换链准备呈现
  swapChain->Present(1, 0);
  // 插入fence
  const UINT64 tempFenceValue = fenceValue;
  queue->Signal(fence.Get(), tempFenceValue);
  fenceValue++;
  // 等待fence完成
  if (fence->GetCompletedValue() < tempFenceValue) {
    fence->SetEventOnCompletion(tempFenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
  }
  frameIndex = swapChain->GetCurrentBackBufferIndex();
}

IRenderContext *Dx12Window::getRenderContext() { return this; }

// bool Dx12Window::onPreTick() { return true; }

ID3D12Device *Dx12Window::getDevice() { return device.Get(); }

ID3D12CommandQueue *Dx12Window::getCommandQueue() { return queue.Get(); }

ID3D12Resource *Dx12Window::getTexture() { return texture.Get(); }

}