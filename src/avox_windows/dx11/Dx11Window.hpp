#pragma once

#include <mutex>

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
  // 实态: 交换链当前是否已切 10bit+PQ(由渲染线程 applyPendingHdr 提交)
  bool bHdrActive = false;
  // 意愿位: 宿主请求的直通态。方案②(§3.4.1) 下与 bHdrActive 分离 ——
  // setHdrPassthrough 只写它, 渲染线程在 mtx 临界区内实翻后才令两者一致
  bool bHdrPending = false;
  // 护住交换链资源(renderView/backTex/swapChain)的改与用:
  // 改=setHdrPassthrough 挂意愿 / initBuffers / applyPendingHdr; 用=onTickWin 的 Present
  std::mutex bufMtx;
  HdrMeta hdrMeta;

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
  // ST2086 元数据: 直通时经 SetHDRMetaData 声明内容亮度, 供系统/屏映射
  virtual void setHdrMeta(const HdrMeta &meta) override;
  // 直通实态: bHdrActive 本身即实态(SetColorSpace1 失败回滚 false,
  // rebuildDevice 重置, resize 经 applyHdrSwapchainState 重入兜底)
  virtual bool hdrPassthroughActive() const override { return bHdrActive; }

 public:
  virtual ID3D11Device *getDevice() override;
  virtual ID3D11Texture2D *getTexture() override;

private:
  void initDevice();
  void initShader();
  void initBuffers(DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM);
  void detectHdrDisplay();
  // 方案②实翻点(§3.4.1): 渲染线程在 onTickWin 内(持 WindowRender::mtx)调用,
  // 按 bHdrPending 重建 10bit/8bit 交换链并提交 bHdrActive
  void applyPendingHdr();
  bool applyHdrSwapchainState();
  void updateHdrMetaData();
  void renderWindow();
  LRESULT handleMessage(UINT msg, WPARAM wparam, LPARAM lparam);
  
  friend LRESULT CALLBACK DX11WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam);
};

}