#pragma once

#include "Dx11Helper.hpp"
#include "Dx11Resource.hpp"
#include "avox/video/Window.hpp"
#include "Dx11SharedTex.hpp"

namespace avox {

class Dx11Window : public IDx11Context, public Window {
public:
  Dx11Window();
  virtual ~Dx11Window();

private:
  static const uint32_t frameCount = 2;
  
  D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_NULL;
  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
  MComPtr<ID3D11Device> device = nullptr;
  MComPtr<ID3D11DeviceContext> context = nullptr;
  MComPtr<IDXGISwapChain> swapChain = nullptr;
  MComPtr<ID3D11Texture2D> backTex = nullptr;
  MComPtr<ID3D11RenderTargetView> renderView = nullptr;
  
  // 共享纹理（跨线程）
  // std::unique_ptr<Dx11SharedTex> sharedTex = nullptr;
  // 渲染纹理（当前线程）
  std::unique_ptr<Dx11Texture> videoTexture = nullptr;
  Dx11SharedTex* sharedTexture = nullptr;  
  
  // VS+PS 资源
  MComPtr<ID3D11VertexShader> vertexShader = nullptr;
  MComPtr<ID3D11PixelShader> pixelShader = nullptr;
  MComPtr<ID3D11InputLayout> inputLayout = nullptr;
  MComPtr<ID3D11Buffer> vertexBuffer = nullptr;
  MComPtr<ID3D11Buffer> indexBuffer = nullptr;
  MComPtr<ID3D11SamplerState> samplerState = nullptr;
  MComPtr<ID3D11RasterizerState> rasterizerState = nullptr;
  
  bool bInitDevice = false;
  bool bInitShader = false;
  ImageFormat sformat = {};
  // HDR 直通输出(块3): 显示器能力探测 + 交换链 PQ 色彩空间状态
  bool bHdrDisplay = false;
  bool bHdrActive = false;

 protected:
  virtual void onInitWin() override;
  virtual bool onValidWin() override;
  virtual void onChangeSize() override;
  virtual bool onPreTick() override;
  virtual void onTickWin() override;
  virtual IRenderContext *getRenderContext() override;
  // 供外部调用，传入解码后的纹理
  virtual void renderContext(IRenderContext* context) override;
  // HDR 直通输出: 显示器支持时切 10bit+PQ 交换链 (Window 虚接口)
  virtual bool setHdrPassthrough(bool bPassthrough) override;

 public:
  virtual ID3D11Device *getDevice() override;
  virtual ID3D11Texture2D *getTexture() override;

private:
  void initDevice();
  void initShader();
  void initBuffers(DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM);
  void detectHdrDisplay();
  void renderWindow();
  LRESULT handleMessage(UINT msg, WPARAM wparam, LPARAM lparam);
  
  friend LRESULT CALLBACK DX11WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam);
};

}