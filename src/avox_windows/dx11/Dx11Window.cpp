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
    initBuffers();
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
// SDR 显示器恒 no-op(返回 false), 行为零变化
bool Dx11Window::setHdrPassthrough(bool bPassthrough) {
  if (!swapChain || !bInitDevice) {
    return false;
  }
  if (bHdrActive == bPassthrough) {
    return true;
  }
  if (!bPassthrough && !bHdrDisplay) {
    return true;  // SDR 链本来就是非直通状态
  }
  if (!bHdrDisplay) {
    LOGFLF(LogLevel::info, "hdr passthrough ignored, display not hdr capable");
    return false;
  }
  MComPtr<IDXGISwapChain3> sc3 = nullptr;
  if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) {
    LOGFLF(LogLevel::warn, "no IDXGISwapChain3, hdr passthrough unsupported");
    return false;
  }
  // PQ 直通需要 10bit 交换链; 切回时恢复 8bit
  initBuffers(bPassthrough ? DXGI_FORMAT_R10G10B10A2_UNORM
                           : DXGI_FORMAT_R8G8B8A8_UNORM);
  HRESULT hr = sc3->SetColorSpace1(
      bPassthrough ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P709
                   : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "SetColorSpace1 failed");
    if (bPassthrough) {
      initBuffers();  // 回退 SDR 链
    }
    return false;
  }
  bHdrActive = bPassthrough;
  LOGFLF(LogLevel::info, "hdr passthrough swapchain:", bPassthrough);
  return true;
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