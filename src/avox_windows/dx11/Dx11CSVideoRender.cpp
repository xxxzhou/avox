

#include "Dx11CSVideoRender.hpp"

#include "Dx11Window.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/video/ColorSpace.hpp"

#if AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFCommon.hpp"
#endif

#pragma comment(lib, "D3DCompiler.lib")

namespace avox {

static const char* yuvToRgbaShader = R"(
Texture2D yTex: register(t0);
Texture2D uvTex: register(t1);
RWTexture2D<float4> outTex: register(u0);

cbuffer CBParameters : register(b0)
{
    uint2 inputSize;     // 输出RGBA尺寸
    uint  yuvType;       // 0=nv12 1=p010
    int   transfer;      // YuvTransfer 声明序: 0=gamma 1=linear 2=pq 3=hlg
    int   hdrMode;       // HdrMode 声明序: 0=follow 1=forceSDR 2=forceHDR
    float maxLuminance;  // 内容峰值亮度 nits
    float sdrWhiteNits;  // SDR 白点 nits
    float _pad;
};

// YUV 转 RGB 函数 (NV12 现状路径: full-range 近似系数, 保持既有行为不动)
float4 yuv2Rgb(float y, float u, float v, float a) {
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    return float4(clamp(r, 0, 1), clamp(g, 0, 1), clamp(b, 0, 1), a);
}

// ---- HDR 处理: 与 glsl/yuv2rgbaV5.comp 同源 ----

// PQ EOTF (SMPTE ST2084): 编码值 -> 线性光, 1.0 = 10000 nits
float3 pqToLinear(float3 n) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(clamp(n, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0)), 1.0 / m2);
    float3 num = max(p - c1, float3(0.0, 0.0, 0.0));
    return pow(num / (c2 - c3 * p), 1.0 / m1);
}

// HLG 解码(BT.2100): OETF^-1 得场景线性, 再逆 OOTF(1.2, 1000nit 参考屏)
// 输出线性光 1.0 = 1000 nits
float3 hlgToLinear(float3 e) {
    float3 t = clamp(e, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
    float3 lo = t * t / 3.0;
    float3 hi = (exp((t - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
    float3 scene = lerp(lo, hi, step(0.5, t));
    float ys = dot(scene, float3(0.2627, 0.6780, 0.0593));
    return scene * pow(max(ys, 1e-6), 0.2);
}

// tone map: 线性光(10000nit 归一) -> 显示线性 [0,1] (ACES 近似 Narkowicz)
float3 toneMap(float3 lin) {
    float xScale = 10000.0 / sdrWhiteNits;
    float3 x = max(lin * xScale, float3(0.0, 0.0, 0.0));
    float3 a = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    float peakX = max(maxLuminance, sdrWhiteNits) / sdrWhiteNits;
    float peak = (peakX * (2.51 * peakX + 0.03)) / (peakX * (2.43 * peakX + 0.59) + 0.14);
    return clamp(a / peak, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

// BT.2020 -> BT.709 线性域 primaries 转换, 越界分量截断
// (GLSL mat3 列主序构造的转置即行主序三行)
float3 bt2020ToBt709(float3 c) {
    return max(mul(float3x3(
        1.6605, -0.5876, -0.0728,
        -0.1246, 1.1329, -0.1006,
        -0.0182, -0.1006, 1.1187), c), float3(0.0, 0.0, 0.0));
}

// 线性光 -> BT.709 OETF 编码
float3 linearToBt709(float3 c) {
    float3 lo = c * 4.5;
    float3 hi = 1.099 * pow(max(c, float3(0.0, 0.0, 0.0)), 0.45) - 0.099;
    return lerp(lo, hi, step(0.018, c));
}

// 色彩处理总入口: forceHDR 跳过全部处理; 否则仅 PQ/HLG 做变换
float3 processColor(float3 rgb) {
    if (hdrMode == 2) {
        return rgb;
    }
    if (transfer == 2) {
        float3 lin = pqToLinear(rgb);
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    if (transfer == 3) {
        float3 lin = hlgToLinear(rgb) * 0.1;
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    return rgb;
}

// P010: 16-bit 视图采样(R16_UNORM/R16G16_UNORM), 高 10 位有效, BT.2020 tv-range
float4 p010Point(uint2 pix, float a) {
    float k = 65535.0 / 64.0 / 1023.0;  // R16_UNORM 值 -> 10bit 归一
    float y = yTex.Load(int3(pix, 0)).r * k;
    float2 uvRaw = uvTex.Load(int3(pix.x / 2, pix.y / 2, 0)).rg;
    float u = uvRaw.x * k - 0.5;
    float v = uvRaw.y * k - 0.5;
    // BT.2020 tv-range 展开(Y 64..940, UV 512 居中) 后矩阵
    float yy = saturate((y - 64.0 / 1023.0) / (876.0 / 1023.0));
    float uu = u * (876.0 / 896.0);
    float vv = v * (876.0 / 896.0);
    float r = yy + 1.4746 * vv;
    float g = yy - 0.164553 * uu - 0.571353 * vv;
    float b = yy + 1.8814 * uu;
    return float4(saturate(processColor(float3(r, g, b))), a);
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint2 size = inputSize;
    if(DTid.x >= size.x/2 || DTid.y >= size.y/2){
        return;
    }
    if (yuvType == 1) {
        float4 o1 = p010Point(uint2(DTid.x*2, DTid.y*2), 1.0f);
        float4 o2 = p010Point(uint2(DTid.x*2+1, DTid.y*2), 1.0f);
        float4 o3 = p010Point(uint2(DTid.x*2, DTid.y*2+1), 1.0f);
        float4 o4 = p010Point(uint2(DTid.x*2+1, DTid.y*2+1), 1.0f);
        outTex[int2(DTid.x*2, DTid.y*2)] = o1;
        outTex[int2(DTid.x*2+1, DTid.y*2)] = o2;
        outTex[int2(DTid.x*2, DTid.y*2+1)] = o3;
        outTex[int2(DTid.x*2+1, DTid.y*2+1)] = o4;
        return;
    }
    float y1 = yTex.Load(int3(DTid.x*2,DTid.y*2, 0)).r;
    float y2 = yTex.Load(int3(DTid.x*2+1,DTid.y*2, 0)).r;
    float y3 = yTex.Load(int3(DTid.x*2,DTid.y*2+1, 0)).r;
    float y4 = yTex.Load(int3(DTid.x*2+1,DTid.y*2+1, 0)).r;
    float2 uv = uvTex.Load(int3(DTid.x, DTid.y, 0)).rg - float2(0.5f, 0.5f);

    float4 rgba1 = yuv2Rgb(y1, uv.x, uv.y, 1.0f);
    float4 rgba2 = yuv2Rgb(y2, uv.x, uv.y, 1.0f);
    float4 rgba3 = yuv2Rgb(y3, uv.x, uv.y, 1.0f);
    float4 rgba4 = yuv2Rgb(y4, uv.x, uv.y, 1.0f);

    outTex[int2(DTid.x*2,DTid.y*2)] = rgba1;
    outTex[int2(DTid.x*2+1,DTid.y*2)] = rgba2;
    outTex[int2(DTid.x*2,DTid.y*2+1)] = rgba3;
    outTex[int2(DTid.x*2+1,DTid.y*2+1)] = rgba4;
}
)";

Dx11CSVideoRender::Dx11CSVideoRender() { renderType = RenderType::D3D11; }

bool Dx11CSVideoRender::vaildAndInitGraph() {
  if (!gpuFrame.buffer || cpuIn) {
    return false;
  }
  Dx11Context* context = static_cast<Dx11Context*>(gpuFrame.context);
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  // 原子读+清重置标志: 释放决策用捕获值, 只清本次读到的值 —— 读-清分离期间宿主
  // 新置的请求不会被盲写抹掉, 留到下一帧再重建一次(见 VideoRender.hpp 契约)
  const bool bNeedReset = bResetFlag.exchange(false);
  // 如果上下文或是大小变化，重新创建
  if (device != context->getDevice() || bNeedReset) {
    releaseGraph();
  }
  // NV12/P010 流切换: SRV 视图与着色器分支都不同, 必须重建
  if (yuvDesc.Format != desc.Format && desc.Format != DXGI_FORMAT_UNKNOWN) {
    releaseGraph();
  }
  // 早退/失败的重试由 computeShader 是否为空驱动, 不依赖本标志
  if (computeShader) {
    return true;
  }
  // 纹理可能无效，比如源纹理重建了，在渲染线程中指针还在
  if (desc.Width == 0 || desc.Height == 0) {
    return false;
  }
  // 使用解码的D3D11设备
  setDevice(context->getDevice());
  imageWidth = gpuFrame.format.width;
  imageHeight = gpuFrame.format.height;
  yuvDesc = desc;
  createProgram();
  return computeShader != nullptr;
}

void Dx11CSVideoRender::releaseGraph() {
  if (computeShader) {
    computeShader.Reset();
  }
  // 释放回读资源,映射指针一并失效
  if (bStagingMapped && d3dcontext) {
    d3dcontext->Unmap(stagingTexture.Get(), 0);
    bStagingMapped = false;
  }
  stagingTexture.Reset();
  stagingWidth = 0;
  stagingHeight = 0;
}

void Dx11CSVideoRender::renderGpuFrame(const GpuFrame& frame) {
  renderToTexture(frame);
}

void Dx11CSVideoRender::setColorSpace(const ColorSpaceDesc& c) {
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;
  }
  cs = c;
  bParamsDirty = true;
}

void Dx11CSVideoRender::setHdrMeta(const HdrMeta& meta) {
  if (!meta.valid) {
    return;
  }
  hdrMeta = meta;
  bParamsDirty = true;
}

void Dx11CSVideoRender::setHdrMode(HdrMode mode) {
  if (mode == hdrMode) {
    return;
  }
  hdrMode = mode;
  bParamsDirty = true;
}

void Dx11CSVideoRender::createProgram() {
  if (!device || !d3dcontext) {
    return;
  }
  // 编译着色器
  ID3DBlob* shaderBlob = nullptr;
  ID3DBlob* errorBlob = nullptr;
  HRESULT hr =
      D3DCompile(yuvToRgbaShader, strlen(yuvToRgbaShader), nullptr, nullptr,
                 nullptr, "main", "cs_5_0", 0, 0, &shaderBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      log(LogLevel::warn,
          "Dx11Graph D3DCompile error: ", (char*)errorBlob->GetBufferPointer());
      errorBlob->Release();
    }
    if (shaderBlob) {
      shaderBlob->Release();
    }
    return;
  }
  // 创建计算着色器
  hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                   shaderBlob->GetBufferSize(), nullptr,
                                   &computeShader);
  if (FAILED(hr)) {
    shaderBlob->Release();
    return;
  }
  shaderBlob->Release();
  // 创建常量缓冲区(32B: 尺寸+格式+transfer+hdrMode+峰值)
  constBuf = std::make_unique<Dx11Constant>();
  constBuf->setBufferSize(sizeof(constData));
  constBuf->cpuData = (uint8_t*)constData;
  constBuf->initResource(device);
  // 创建输出计算着色器资源
  // outTexture = std::make_unique<Dx11Texture>();
  // // outTexture->setOnlyUAV(true);
  // outTexture->setTextureSize(imageWidth, imageHeight,
  //                            DXGI_FORMAT_R8G8B8A8_UNORM);
  // outTexture->initResource(device);
  outSharedTex = std::make_unique<Dx11SharedTex>();
  outTexture = outSharedTex->getDx11Texture();
  outTexture->setTextureSize(imageWidth, imageHeight,
                             DXGI_FORMAT_R8G8B8A8_UNORM);
  outSharedTex->initTexture(device);
  // 创建输入复制纹理
  D3D11_TEXTURE2D_DESC inCopyDesc = yuvDesc;
  inCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  inCopyDesc.MipLevels = 1;
  inCopyDesc.ArraySize = 1;
  inCopyDesc.SampleDesc.Count = 1;
  inCopyDesc.Usage = D3D11_USAGE_DEFAULT;
  device->CreateTexture2D(&inCopyDesc, nullptr, &inTexture);
  // 创建输入纹理的SRV: NV12 用 8bit 视图; P010 必须用 16bit 视图
  // (类型不兼容的 cast 会创建失败且无 SRV -> 渲染静默全黑)
  bool bP010 = inCopyDesc.Format == DXGI_FORMAT_P010;
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = inCopyDesc.MipLevels;
  srvDesc.Format = bP010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
  if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                              &yView))) {
    LOGFLF(LogLevel::warn, "create y srv failed, dxgi:",
           (int32_t)inCopyDesc.Format);
  }
  srvDesc.Format = bP010 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
  if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                              &uvView))) {
    LOGFLF(LogLevel::warn, "create uv srv failed, dxgi:",
           (int32_t)inCopyDesc.Format);
  }
  // 设置当前纹理
  texture = outTexture->texture.Get();
  // 设置常量缓冲区
  d3dcontext->CSSetConstantBuffers(0, 1, constBuf->buffer.GetAddressOf());
  // 设置计算着色器资源
  d3dcontext->CSSetShader(computeShader.Get(), nullptr, 0);
  // 设置输入纹理
  ID3D11ShaderResourceView* srvArray[2] = {yView.Get(), uvView.Get()};
  d3dcontext->CSSetShaderResources(0, 2, srvArray);
  // 设置输出纹理的 UAV
  ID3D11UnorderedAccessView* uavArray[1] = {outTexture->uavView.Get()};
  d3dcontext->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
}

void Dx11CSVideoRender::renderToTexture(const GpuFrame& gpuFrame) {
  Dx11Context* context = static_cast<Dx11Context*>(gpuFrame.context);
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  if (!computeShader || !outTexture || !constBuf || !inTexture || !yView ||
      !uvView) {
    return;
  }
  // 常量参数(尺寸/格式/transfer/hdrMode/峰值)变化才上传
  if (bParamsDirty) {
    bParamsDirty = false;
    float peakNits = (float)hdrPeakNits(hdrMeta);
    float sdrWhite = 100.0f;
    constData[0] = imageWidth;
    constData[1] = imageHeight;
    constData[2] = (yuvDesc.Format == DXGI_FORMAT_P010) ? 1u : 0u;
    constData[3] = (uint32_t)cs.transfer;
    constData[4] = (uint32_t)hdrMode;
    memcpy(&constData[5], &peakNits, sizeof(float));
    memcpy(&constData[6], &sdrWhite, sizeof(float));
    constData[7] = 0u;
    constBuf->updateResource(d3dcontext.Get());
  }
  if (desc.ArraySize > 1) {
    // 复制指定索引的切片到临时纹理
    d3dcontext->CopySubresourceRegion(
        inTexture.Get(), D3D11CalcSubresource(0, 0, desc.MipLevels), 0, 0, 0,
        yuvTexture,
        D3D11CalcSubresource(0, gpuFrame.queueIndex, desc.MipLevels), nullptr);
    // logTexture(device, inTexture.Get());
  } else {
    // 将 yuvTexture 的数据复制给 inTexture
    d3dcontext->CopyResource(inTexture.Get(), yuvTexture);
  }
  // 执行计算着色器
  uint32_t groupX = divUp(imageWidth / 2, 16);
  uint32_t groupY = divUp(imageHeight / 2, 16);
  d3dcontext->Dispatch(groupX, groupY, 1);
  // 解绑所有可能的冲突,outTexture在这做UAV，不解绑，后面不能做SRV
  // ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
  // ID3D11UnorderedAccessView* nullUAVs[1] = {nullptr};
  // d3dcontext->CSSetShaderResources(0, 2, nullSRVs);
  // d3dcontext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
  // 准备输出到Vk上下文
  texture = outTexture->texture.Get();
}

bool Dx11CSVideoRender::fetchFrame(ImageBuffer* imageBuffer) {
  if (!outTexture) {
    return false;
  }
  Dx11Context context = {};
  context.setDevice(device);
  context.setTexture(outTexture->texture.Get());
  return fetchTexture(&context, imageBuffer);
}

bool Dx11CSVideoRender::getCpuFrameBuffer(IImageBuffer** buffer,
                                          YuvType& yuvType, int64_t* pts) {
  // CPU输入(软解)不经过GPU,交基类packed视图
  if (cpuIn) {
    return VideoRender::getCpuFrameBuffer(buffer, yuvType, pts);
  }
  if (!bOutCpuYuv || !device || !d3dcontext || !gpuFrame.buffer) {
    return false;
  }
  // 本帧未回读过才做staging拷贝+Map,一帧最多一次
  if (publishedTick != renderTick && !mapStagingFrame()) {
    return false;
  }
  publishedTick = renderTick;
  *buffer = &stagingBuffer;
  // 交付解码直出格式: 硬解10bit是p010(高位对齐), 8bit是nv12
  yuvType = (yuvDesc.Format == DXGI_FORMAT_P010) ? YuvType::p010
                                                 : YuvType::nv12;
  if (pts) {
    *pts = gpuFrame.pts;
  }
  return true;
}

bool Dx11CSVideoRender::mapStagingFrame() {
  ID3D11Texture2D* src = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  src->GetDesc(&desc);
  bool bP010 = desc.Format == DXGI_FORMAT_P010;
  if (desc.Format != DXGI_FORMAT_NV12 && !bP010) {
    LOGFLF(LogLevel::warn, "cpu yuv out not support dxgi format:",
           (int32_t)desc.Format);
    return false;
  }
  // 解码 surface 是按对齐扩过的(如 1280x720 -> 1280x768),padding 行解码器
  // 从不写入(Y/U/V=0,转RGB呈绿带)。CPU 交付必须按显示尺寸裁剪。
  const int32_t outW = gpuFrame.format.width;
  const int32_t outH = gpuFrame.format.height;
  if (outW <= 0 || outH <= 0 || outW > (int32_t)desc.Width ||
      outH > (int32_t)desc.Height) {
    LOGFLF(LogLevel::warn, "cpu yuv out bad display size:", outW, "x", outH,
           " surface:", (int32_t)desc.Width, "x", (int32_t)desc.Height);
    return false;
  }
  // 解上一帧映射(发布指针随Unmap失效,消费者须在当帧窗口内使用)
  if (bStagingMapped) {
    d3dcontext->Unmap(stagingTexture.Get(), 0);
    bStagingMapped = false;
  }
  if (!stagingTexture || stagingWidth != outW || stagingHeight != outH) {
    stagingTexture.Reset();
    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Width = (UINT)outW;
    sdesc.Height = (UINT)outH;
    sdesc.MipLevels = 1;
    sdesc.ArraySize = 1;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;
    if (FAILED(device->CreateTexture2D(&sdesc, nullptr,
                                       stagingTexture.GetAddressOf()))) {
      LOGFLF(LogLevel::warn, "create nv12 staging texture failed");
      return false;
    }
    stagingWidth = outW;
    stagingHeight = outH;
  }
  const UINT srcSub =
      D3D11CalcSubresource(0, (UINT)gpuFrame.queueIndex, desc.MipLevels);
  // 平面格式用 box 的 z 选平面: z=0 Y面(outH行), z=1 UV面(outH/2行)
  D3D11_BOX yBox = {0, 0, 0, (UINT)outW, (UINT)outH, 1};
  D3D11_BOX uvBox = {0, 0, 1, (UINT)outW / 2, (UINT)outH / 2, 2};
  d3dcontext->CopySubresourceRegion(stagingTexture.Get(), 0, 0, 0, 0, src,
                                    srcSub, &yBox);
  d3dcontext->CopySubresourceRegion(stagingTexture.Get(), 0, 0, 0, 1, src,
                                    srcSub, &uvBox);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(d3dcontext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                             &mapped))) {
    LOGFLF(LogLevel::warn, "map nv12 staging texture failed");
    return false;
  }
  bStagingMapped = true;
  // packed布局约定: r8/r16 + height*3/2 + rowPitch(字节), UV起始=rowPitch*height
  // NV12: 8bit Y面+8bit交错UV; P010: 16bit(高10位)Y面+16bit交错UV
  ImageFormat fmt = {};
  fmt.width = outW;
  fmt.height = outH * 3 / 2;
  fmt.imageType = bP010 ? ImageType::r16 : ImageType::r8;
  fmt.rowPitch = (int32_t)mapped.RowPitch;
  stagingBuffer.setData((uint8_t*)mapped.pData, fmt, false);
  return true;
}

}