

#include "Dx11CSVideoRender.hpp"

#include "Dx11Window.hpp"
#include "avox/module/AvoxManager.hpp"

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
    uint2 inputSize;
};

// YUV 转 RGB 函数
float4 yuv2Rgb(float y, float u, float v, float a) {
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    return float4(clamp(r, 0, 1), clamp(g, 0, 1), clamp(b, 0, 1), a);
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint2 size = inputSize;    
    if(DTid.x >= size.x/2 || DTid.y >= size.y/2){
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
  // 如果上下文或是大小变化，重新创建
  if (device != context->getDevice() || bResetFlag) {
    releaseGraph();
  }
  if (computeShader && !bResetFlag) {
    return true;
  }
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
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
  // 创建常量缓冲区
  constBuf = std::make_unique<Dx11Constant>();
  constBuf->setBufferSize(sizeof(uint32_t) * 2);
  std::vector<uint32_t> constData = {imageWidth, imageHeight};
  constBuf->cpuData = (uint8_t*)constData.data();
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
  // 创建输入纹理的SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = inCopyDesc.MipLevels;
  device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &yView);
  srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
  device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &uvView);
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
  yuvType = YuvType::nv12;
  if (pts) {
    *pts = gpuFrame.pts;
  }
  return true;
}

bool Dx11CSVideoRender::mapStagingFrame() {
  ID3D11Texture2D* src = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  src->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_NV12) {
    LOGFLF(LogLevel::warn, "cpu yuv out not support dxgi format:",
           (int32_t)desc.Format);
    return false;
  }
  // 解上一帧映射(发布指针随Unmap失效,消费者须在当帧窗口内使用)
  if (bStagingMapped) {
    d3dcontext->Unmap(stagingTexture.Get(), 0);
    bStagingMapped = false;
  }
  if (!stagingTexture || stagingWidth != (int32_t)desc.Width ||
      stagingHeight != (int32_t)desc.Height) {
    stagingTexture.Reset();
    D3D11_TEXTURE2D_DESC sdesc = desc;
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
    stagingWidth = (int32_t)desc.Width;
    stagingHeight = (int32_t)desc.Height;
  }
  if (desc.ArraySize > 1) {
    d3dcontext->CopySubresourceRegion(
        stagingTexture.Get(), 0, 0, 0, 0, src,
        D3D11CalcSubresource(0, (UINT)gpuFrame.queueIndex, desc.MipLevels),
        nullptr);
  } else {
    d3dcontext->CopyResource(stagingTexture.Get(), src);
  }
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(d3dcontext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                             &mapped))) {
    LOGFLF(LogLevel::warn, "map nv12 staging texture failed");
    return false;
  }
  bStagingMapped = true;
  // nv12 packed布局约定: r8 + height*3/2 + rowPitch(字节), UV起始=rowPitch*height
  ImageFormat fmt = {};
  fmt.width = (int32_t)desc.Width;
  fmt.height = (int32_t)desc.Height * 3 / 2;
  fmt.imageType = ImageType::r8;
  fmt.rowPitch = (int32_t)mapped.RowPitch;
  stagingBuffer.setData((uint8_t*)mapped.pData, fmt, false);
  return true;
}

}