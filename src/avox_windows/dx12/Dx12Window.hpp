#pragma once

#include "Dx12Context.hpp"
#include "Dx12Helper.hpp"
#include "avox/video/Window.hpp"
namespace avox {

class Dx12Window : public IDx12Context, public Window {
public:
  Dx12Window(/* args */);
  virtual ~Dx12Window();

private:
  static const uint32_t frameCount = 2;
  HWND hwnd = nullptr;

  MComPtr<ID3D12Device> device = nullptr;
  MComPtr<IDXGISwapChain3> swapChain = nullptr;
  MComPtr<ID3D12CommandQueue> queue = nullptr;
  MComPtr<ID3D12CommandAllocator> commandAllocator;
  MComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
  MComPtr<ID3D12PipelineState> pipelineState = nullptr;
  MComPtr<ID3D12DescriptorHeap> rtvHeap = nullptr;
  MComPtr<ID3D12Resource> renderTargets[frameCount];
  MComPtr<ID3D12Fence> fence = nullptr;
  MComPtr<ID3D12Resource> texture = nullptr;
  HANDLE fenceEvent = nullptr;
  uint32_t rtvDescriptorSize = 0;
  uint32_t frameIndex = 0;
  UINT64 fenceValue = 0;

  // 如果解码DX上下文过来,需要创建共享纹理
  std::unique_ptr<Dx11SharedTex> sharedTex = nullptr;

private:
  friend LRESULT CALLBACK DX12WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam);

private:
  LRESULT handleMessage(UINT msg, WPARAM wparam, LPARAM lparam);
  bool initDevice(); 

protected:
  virtual void onInitWin() override;
  // virtual bool onPreTick() override;
  virtual void onTickWin() override;  
  virtual IRenderContext *getRenderContext() override;

public:
  virtual ID3D12Device *getDevice() override;
  virtual ID3D12CommandQueue *getCommandQueue() override;
  virtual ID3D12Resource *getTexture() override;
};

}