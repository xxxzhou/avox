#include "Dx11Window.hpp"
#include "Dx11ShaderCache.hpp"
#include "avox/AvoxMath.h"
#include <dxgi1_6.h>
// 老SDK的 DXGI_COLOR_SPACE_TYPE 枚举缺 PQ 值(12), 兜底补齐
#ifndef DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P709
#define DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P709 ((DXGI_COLOR_SPACE_TYPE)12)
#endif
namespace avox {

extern vec4i getViewRect(int swidth, int sheight, float aspect);

// 顶点着色器
static const char* vertexShaderSource = R"(
struct VSInput {
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};
struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};
PSInput main(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texCoord = input.texCoord;
    return output;
}
)";

// 像素着色器
static const char* pixelShaderSource = R"(
Texture2D videoTexture : register(t0);
SamplerState linearSampler : register(s0);
struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};
float4 main(PSInput input) : SV_TARGET {
    return videoTexture.SampleLevel(linearSampler, input.texCoord, 0);
}
)";

LRESULT CALLBACK DX11WndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                             LPARAM lParam) {
  Dx11Window* window =
      reinterpret_cast<Dx11Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  if (!window) {
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
  }
  return window->handleMessage(uMsg, wParam, lParam);
}

ID3D11Device* Dx11Window::getDevice() { return device.Get(); }
ID3D11Texture2D* Dx11Window::getTexture() { return backTex.Get(); }

Dx11Window::Dx11Window() { renderType = RenderType::D3D11; }
Dx11Window::~Dx11Window() {}

bool Dx11Window::onValidWin() { return bInitDevice; }

void Dx11Window::onChangeSize() {
  // 如果设备已初始化，使用 ResizeBuffers 调整大小
  // 避免重新创建设备导致 "Only one flip model swap chain can be associate with
  // an HWND" 错误
  if (bInitDevice && swapChain) {
    // 方案②(§3.4.1): 本调用来自 onPreTick/updateSize(渲染线程持 mtx), 但
    // setHdrPassthrough 侧也会摸 bHdrPending, 统一走 bufMtx 护住
    std::lock_guard<std::mutex> lck(bufMtx);
    // 直通态必须保住 10bit: 无参 initBuffers 会把交换链降回 8bit。
    // 用「意愿或实态」任一为真判直通, 否则翻转窗口内的 resize 会把待翻态打回 8bit
    const bool bHdr = bHdrActive || bHdrPending;
    initBuffers(bHdr ? DXGI_FORMAT_R10G10B10A2_UNORM
                     : DXGI_FORMAT_R8G8B8A8_UNORM);
    applyHdrSwapchainState();  // ResizeBuffers 会重置色彩空间, 直通态必须重挂
  } else {
    // 首次初始化设备
    initDevice();
  }
}

bool Dx11Window::onPreTick() {
  bool quit = false;
  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      quit = true;
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  if (wdWidth <= 0 || wdHeight <= 0) {
    return false;
  }
  return !quit;
}

void Dx11Window::onTickWin() {
  if (!bInitDevice) {
    LOGFLF(LogLevel::warn, "dx11 device not init");
    return;
  }
  // 方案②(§3.4.1): 交换链翻转落在此处 —— 本函数由 WindowRender::run() 持 mtx
  // 调用(WindowRender.cpp:323), 与 Present 同一临界区, 消除宿主线程拆交换链的
  // 竞态 A。翻转窗口内双端都停旧口径 = 过渡帧不出值域错配
  applyPendingHdr();
  // 初始化 shader
  if (!bInitShader) {
    initShader();
  }
  // 共享纹理 → 渲染管线
  if (!sharedTexture) {
    return;
  }
  sharedTexture->setInteropDevice(device.Get());
  // if (!sharedTexture->canInteropRead()) {
  //   return;
  // }
  ID3D11Texture2D* interopTexture = sharedTexture->getInteropTexture();
  if (!interopTexture) {
    return;
  }
  ImageFormat vformat = sharedTexture->getImageFormat();
  aspect = (float)vformat.width / (float)vformat.height;
  // sharedTexture->logInteropTex();
  ID3D11RenderTargetView* renderViews[1] = {renderView.Get()};
  context->OMSetRenderTargets(1, renderViews, nullptr);
  D3D11_VIEWPORT vp = {0, 0, (FLOAT)wdWidth, (FLOAT)wdHeight, 0.0f, 1.0f};
  if (!bFullScreen && aspect > 0.0f) {
    vec4i viewRect = getViewRect(wdWidth, wdHeight, aspect);
    vp = {(FLOAT)viewRect.x,
          (FLOAT)viewRect.y,
          (FLOAT)viewRect.z,
          (FLOAT)viewRect.w,
          0.0f,
          1.0f};
  }
  context->RSSetViewports(1, &vp);
  // 清空 back buffer
  const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  context->ClearRenderTargetView(renderView.Get(), clearColor);
  // VS+PS 渲染
  renderWindow();
  swapChain->Present(1, 0);
  //
  sharedTexture->signalInteropFence();
}

void Dx11Window::onInitWin() {
  HMODULE wdInstance = GetModuleHandle(nullptr);
  surface = createWin32Window((HINSTANCE)wdInstance, hwnd, wdWidth, wdHeight,
                              wdTitle.c_str(), DX11WndProc, this);
  initDevice();
}

IRenderContext* Dx11Window::getRenderContext() { return this; }

void Dx11Window::renderContext(IRenderContext* context) {
  IDx11Context* dxcontext = dynamic_cast<IDx11Context*>(context);
  if (!dxcontext) {
    return;
  }
  if (dxcontext->bInteropTexture()) {
    sharedTexture = dynamic_cast<Dx11SharedTex*>(dxcontext);
  } else {
    // Dx11Context* temp = dynamic_cast<Dx11Context*>(dxcontext);
  }
}

void Dx11Window::renderWindow() {
  // 设置管线状态
  context->VSSetShader(vertexShader.Get(), nullptr, 0);
  context->PSSetShader(pixelShader.Get(), nullptr, 0);
  context->IASetInputLayout(inputLayout.Get());
  // 设置顶点数据
  UINT stride = sizeof(float) * 4;
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride,
                              &offset);
  context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  // 设置纹理和采样器
  ID3D11ShaderResourceView* intSrv = sharedTexture->getInteropSrv();
  ID3D11ShaderResourceView* srvs[1] = {intSrv};
  context->PSSetShaderResources(0, 1, srvs);
  context->PSSetSamplers(0, 1, samplerState.GetAddressOf());
  // 设置光栅化状态
  context->RSSetState(rasterizerState.Get());
  // 渲染
  context->DrawIndexed(6, 0, 0);
  // 解绑 SRV
  ID3D11ShaderResourceView* nullSrvs[1] = {nullptr};
  context->PSSetShaderResources(0, 1, nullSrvs);
}

void Dx11Window::initDevice() {
  bInitDevice = false;
  bInitShader = false;
  // 重建设备=新交换链, 直通实态与挂起的意愿一并作废(意愿残留会在 SDR 屏被
  // applyPendingHdr 重放成 PQ 交换链=洗白/品红钉死; 宿主补发可自愈)
  bHdrActive = false;
  bHdrPending = false;

  HRESULT hr = S_OK;
  UINT createDeviceFlags = 0;
#if AVOX_DEBUG
  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_DRIVER_TYPE driverTypes[] = {
      D3D_DRIVER_TYPE_HARDWARE,
      D3D_DRIVER_TYPE_WARP,
      D3D_DRIVER_TYPE_REFERENCE,
  };
  UINT numDriverTypes = ARRAYSIZE(driverTypes);

  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };
  UINT numFeatureLevels = ARRAYSIZE(featureLevels);

  DXGI_SWAP_CHAIN_DESC sd = {};
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = frameCount;
  sd.BufferDesc.Width = wdWidth;
  sd.BufferDesc.Height = wdHeight;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = surface;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  // DXGI_SWAP_EFFECT_FLIP_DISCARD DXGI_SWAP_EFFECT_DISCARD
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes;
       driverTypeIndex++) {
    driverType = driverTypes[driverTypeIndex];
    hr = D3D11CreateDeviceAndSwapChain(
        NULL, driverType, NULL, createDeviceFlags, featureLevels,
        numFeatureLevels, D3D11_SDK_VERSION, &sd, &swapChain, &device,
        &featureLevel, &context);
    if (SUCCEEDED(hr)) {
      break;
    }
  }
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "create device and swapchain failed");
    return;
  }
  detectHdrDisplay();
  initBuffers();
  bInitDevice = true;
}

// 探测所在显示器的 HDR 能力(IDXGIOutput6 色彩空间), 决定直通输出是否可生效
void Dx11Window::detectHdrDisplay() {
  bHdrDisplay = false;
  MComPtr<IDXGIOutput> output = nullptr;
  MComPtr<IDXGIOutput6> output6 = nullptr;
  if (FAILED(swapChain->GetContainingOutput(&output)) || !output) {
    LOGFLF(LogLevel::info, "hdr detect: no containing output");
    return;
  }
  if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output6))) || !output6) {
    LOGFLF(LogLevel::info, "hdr detect: no IDXGIOutput6 (win10 1703前)");
    return;
  }
  DXGI_OUTPUT_DESC1 desc1 = {};
  if (FAILED(output6->GetDesc1(&desc1))) {
    return;
  }
  bHdrDisplay =
      desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P709;
  LOGFLF(LogLevel::info, "hdr display capable:", bHdrDisplay);
}

// forceHDR 时把交换链切 10bit+PQ 色彩空间, 内容原样上屏;
// SDR 显示器恒 no-op(返回 false), 行为零变化。
// 方案②(§3.4.1): 本函数只在宿主线程做「能力检查 + 挂意愿」, 不当场改交换链 ——
// 实翻由渲染线程在 onTickWin 的 mtx 临界区内经 applyPendingHdr() 完成
bool Dx11Window::setHdrPassthrough(bool bPassthrough) {
  if (!swapChain || !bInitDevice) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lck(bufMtx);
    if (bHdrActive == bPassthrough && bHdrPending == bPassthrough) {
      if (bPassthrough) {
        applyHdrSwapchainState();  // 重入兜底: 重建路径可能已重置色彩空间
      }
      return true;
    }
  }
  if (!bPassthrough && !bHdrDisplay) {
    // SDR 屏永远到不了直通态: 实态/意愿一并压 false。只 return true 不清
    // pending 的话, 宿主降档每次都白发, applyPendingHdr 会把 PQ 交换链
    // 重放回来(10/1 跨屏品灰案的根因之一)
    std::lock_guard<std::mutex> lck(bufMtx);
    bHdrPending = false;
    bHdrActive = false;
    return true;
  }
  if (!bHdrDisplay) {
    LOGFLF(LogLevel::info, "hdr passthrough ignored, display not hdr capable");
    // 被拒请求不得留意愿: 残留会被 applyPendingHdr 无视显示能力重放
    std::lock_guard<std::mutex> lck(bufMtx);
    bHdrPending = bHdrActive;
    return false;
  }
  // 只挂意愿: 实际切 10bit 交换链 + SetColorSpace1 由渲染线程执行(见 applyPendingHdr)
  bool bActive = false;
  {
    std::lock_guard<std::mutex> lck(bufMtx);
    bHdrPending = bPassthrough;
    bActive = bHdrActive;
  }
  LOGFLF(LogLevel::info, "hdr passthrough requested, active:", bActive ? 1 : 0,
         " pending:", bPassthrough ? 1 : 0);
  return true;
}

// 方案②实翻点: 由渲染线程在 onTickWin 内(持 WindowRender::mtx)调用。
// PQ 直通需 10bit 交换链; 切回恢复 8bit。改完提交 bHdrActive, 令
// hdrPassthroughActive() 与输出端检查点(checkTargetPassthrough)在同一帧看到新值
void Dx11Window::applyPendingHdr() {
  bool want = false;
  {
    std::lock_guard<std::mutex> lck(bufMtx);
    want = bHdrPending;
  }
  if (want == bHdrActive) {
    return;
  }
  // 显示能力先于翻转判定: SetColorSpace1 是交换链声明, 屏不拦它 —— 不看
  // bHdrDisplay 就翻, SDR 屏上会钉死在 PQ 交换链(10/1 跨屏品灰案根因)
  if (want && !bHdrDisplay) {
    std::lock_guard<std::mutex> lck(bufMtx);
    bHdrPending = false;
    if (bHdrActive) {
      bHdrActive = false;
      initBuffers();  // 退出误入的 PQ 链
      applyHdrSwapchainState();
      LOGFLF(LogLevel::warn,
             "hdr passthrough dropped, display not hdr capable");
    }
    return;
  }
  initBuffers(want ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
  bHdrActive = want;
  if (!applyHdrSwapchainState()) {
    if (want) {
      bHdrActive = false;
      initBuffers();  // 回退 SDR 链
      applyHdrSwapchainState();
      // 失败清意愿: 不清则每帧重建交换链循环(实翻失败≠能力不足时宿主
      // 重发前唯一的退出口)
      std::lock_guard<std::mutex> lck(bufMtx);
      bHdrPending = false;
    }
    return;
  }
  LOGFLF(LogLevel::info, "hdr passthrough swapchain:", want ? 1 : 0);
}

// 重挂交换链色彩空间: ResizeBuffers/重建会把它重置回默认, PQ 码被当 sRGB
// 呈现即发灰。直通态顺带补 ST.2086 元数据。
bool Dx11Window::applyHdrSwapchainState() {
  if (!swapChain || !bInitDevice) {
    return false;
  }
  MComPtr<IDXGISwapChain3> sc3 = nullptr;
  if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) {
    LOGFLF(LogLevel::warn, "no IDXGISwapChain3, hdr colorspace unsupported");
    return false;
  }
  HRESULT hr = sc3->SetColorSpace1(
      bHdrActive ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P709
                 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "SetColorSpace1 failed");
    return false;
  }
  if (bHdrActive) {
    updateHdrMetaData();
  }
  return true;
}

// ST2086 转 DXGI_HDR_METADATA_HDR10: 基色/白点单位 1/50000, 四亮度字段
// 单位 1/10000 nits(HdrMeta.minLuminance 原生即此单位, 其余为 nits 乘 1e4)。
// 无亮度标记不冒充(系统按默认映射); 基色缺按 BT.2020/D65 兜底。
void Dx11Window::updateHdrMetaData() {
  if (!bHdrActive || !swapChain) {
    return;
  }
  if (hdrMeta.maxCLL == 0 && hdrMeta.maxLuminance == 0) {
    return;
  }
  MComPtr<IDXGISwapChain4> sc4 = nullptr;
  if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&sc4))) || !sc4) {
    LOGFLF(LogLevel::warn, "no IDXGISwapChain4, hdr metadata unsupported");
    return;
  }
  static const float k2020[6] = {0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f};
  static const float kD65[2] = {0.3127f, 0.3290f};
  const bool hasPri = hdrMeta.primaries[0] > 0 || hdrMeta.primaries[1] > 0;
  const float *pri = hasPri ? hdrMeta.primaries : k2020;
  const float *wp = hdrMeta.whitePoint[0] > 0 ? hdrMeta.whitePoint : kD65;
  DXGI_HDR_METADATA_HDR10 m = {};
  m.RedPrimary[0] = (UINT16)(pri[0] * 50000.0f);
  m.RedPrimary[1] = (UINT16)(pri[1] * 50000.0f);
  m.GreenPrimary[0] = (UINT16)(pri[2] * 50000.0f);
  m.GreenPrimary[1] = (UINT16)(pri[3] * 50000.0f);
  m.BluePrimary[0] = (UINT16)(pri[4] * 50000.0f);
  m.BluePrimary[1] = (UINT16)(pri[5] * 50000.0f);
  m.WhitePoint[0] = (UINT16)(wp[0] * 50000.0f);
  m.WhitePoint[1] = (UINT16)(wp[1] * 50000.0f);
  m.MaxMasteringLuminance = (UINT)(hdrMeta.maxLuminance * 10000.0f);
  m.MinMasteringLuminance = (UINT)hdrMeta.minLuminance;
  m.MaxContentLightLevel = (UINT)(hdrMeta.maxCLL * 10000.0f);
  m.MaxFrameAverageLightLevel = (UINT)(hdrMeta.maxFALL * 10000.0f);
  HRESULT hr = sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(m), &m);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "SetHDRMetaData failed");
  }
}

void Dx11Window::setHdrMeta(const HdrMeta &meta) {
  hdrMeta = meta;
  if (bHdrActive) {
    updateHdrMetaData();
  }
}

void Dx11Window::initShader() {
  if (bInitShader) {
    return;
  }
  HRESULT hr;
  // 编译走进程内 DXBC 缓存(见 Dx11ShaderCache.hpp): blob 由缓存持有, 本函数内
  // 不要 Release(vsBlob 后面还要喂 CreateInputLayout)
  ID3DBlob* errorBlob = nullptr;
  ID3DBlob* vsBlob =
      Dx11ShaderCache::get(vertexShaderSource, "main", "vs_5_0", &errorBlob);
  if (!vsBlob) {
    if (errorBlob) errorBlob->Release();
    return;
  }
  hr = device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                  vsBlob->GetBufferSize(), nullptr,
                                  &vertexShader);
  if (FAILED(hr)) {
    return;
  }

  // 编译像素着色器
  errorBlob = nullptr;
  ID3DBlob* psBlob =
      Dx11ShaderCache::get(pixelShaderSource, "main", "ps_5_0", &errorBlob);
  if (!psBlob) {
    if (errorBlob) errorBlob->Release();
    return;
  }
  hr =
      device->CreatePixelShader(psBlob->GetBufferPointer(),
                                psBlob->GetBufferSize(), nullptr, &pixelShader);
  if (FAILED(hr)) {
    return;
  }

  // 创建输入布局
  D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(),
                                 vsBlob->GetBufferSize(), &inputLayout);
  // vsBlob/psBlob 由 Dx11ShaderCache 持有, 此处不 Release
  if (FAILED(hr)) return;

  // 创建顶点缓冲区（全屏四边形）
  float vertices[] = {
      // position    // texCoord
      -1.0f, 1.0f,  0.0f, 0.0f,  // 左上
      1.0f,  1.0f,  1.0f, 0.0f,  // 右上
      1.0f,  -1.0f, 1.0f, 1.0f,  // 右下
      -1.0f, -1.0f, 0.0f, 1.0f,  // 左下
  };
  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.Usage = D3D11_USAGE_DEFAULT;
  vbDesc.ByteWidth = sizeof(vertices);
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vbData = {vertices, 0, 0};
  hr = device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer);
  if (FAILED(hr)) return;

  // 创建索引缓冲区
  uint32_t indices[] = {0, 1, 2, 0, 2, 3};
  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.Usage = D3D11_USAGE_DEFAULT;
  ibDesc.ByteWidth = sizeof(indices);
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA ibData = {indices, 0, 0};
  hr = device->CreateBuffer(&ibDesc, &ibData, &indexBuffer);
  if (FAILED(hr)) return;

  // 创建采样器
  D3D11_SAMPLER_DESC sampDesc = {};
  sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  hr = device->CreateSamplerState(&sampDesc, &samplerState);
  if (FAILED(hr)) return;

  // 创建光栅化状态
  D3D11_RASTERIZER_DESC rsDesc = {};
  rsDesc.FillMode = D3D11_FILL_SOLID;
  rsDesc.CullMode = D3D11_CULL_NONE;
  hr = device->CreateRasterizerState(&rsDesc, &rasterizerState);
  if (FAILED(hr)) return;

  bInitShader = true;
}

void Dx11Window::initBuffers(DXGI_FORMAT fmt) {
  // 释放 RTV 和 back buffer 引用
  renderView.Reset();
  backTex.Reset();

  // 关键：确保没有 pending 的 GPU 命令
  context->OMSetRenderTargets(0, nullptr, nullptr);  // 解绑 RTV
  context->ClearState();
  context->Flush();
  // 调整 buffer 大小(fmt 参数支持 HDR 直通切 10bit)
  HRESULT hr = swapChain->ResizeBuffers(frameCount, wdWidth, wdHeight, fmt, 0);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "resize buffer failed");
    return;
  }
  hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backTex);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "get swapchain buffer failed");
    return;
  }
  hr = device->CreateRenderTargetView(backTex.Get(), NULL, &renderView);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "create render target view failed");
    return;
  }
}

LRESULT Dx11Window::handleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  PAINTSTRUCT ps;
  HDC hdc;
  switch (msg) {
    // case WM_PAINT: {
    //   hdc = BeginPaint(surface, &ps);
    //   EndPaint(surface, &ps);
    // } break;
    // case WM_DESTROY:
    //   PostQuitMessage(0);
    //   break;
    default:
      return DefWindowProc(surface, msg, wparam, lparam);
  }
  return 0;
}

}