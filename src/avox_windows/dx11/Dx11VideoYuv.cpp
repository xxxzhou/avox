#include "Dx11VideoYuv.hpp"

#include "avox/module/AvoxManager.hpp"

#pragma comment(lib, "D3DCompiler.lib")

namespace avox {

static const char* rgbaToNv12Shader = R"(
Texture2D rgbaTex: register(t0);
RWTexture2D<float> outYTex: register(u0);
RWTexture2D<float2> outUVTex: register(u1);

cbuffer CBParameters : register(b0)
{
    uint2 inputSize;
};

// RGB 转 YUV 函数
float3 rgb2Yuv(float3 rgb) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float u = -0.14713 * rgb.r - 0.28886 * rgb.g + 0.436 * rgb.b;
    float v = 0.615 * rgb.r - 0.51499 * rgb.g - 0.10001 * rgb.b;
    return float3(y, u + 0.5f, v + 0.5f);
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint2 size = inputSize;    
    if(DTid.x >= size.x/2 || DTid.y >= size.y/2){
        return;
    }
    
    // 读取四个RGBA像素
    float4 rgba1 = rgbaTex.Load(int3(DTid.x*2, DTid.y*2, 0));
    float4 rgba2 = rgbaTex.Load(int3(DTid.x*2+1, DTid.y*2, 0));
    float4 rgba3 = rgbaTex.Load(int3(DTid.x*2, DTid.y*2+1, 0));
    float4 rgba4 = rgbaTex.Load(int3(DTid.x*2+1, DTid.y*2+1, 0));
    
    // 转换为YUV
    float3 yuv1 = rgb2Yuv(rgba1.rgb);
    float3 yuv2 = rgb2Yuv(rgba2.rgb);
    float3 yuv3 = rgb2Yuv(rgba3.rgb);
    float3 yuv4 = rgb2Yuv(rgba4.rgb);
    
    // 写入Y分量
    outYTex[int2(DTid.x*2, DTid.y*2)] = yuv1.x;
    outYTex[int2(DTid.x*2+1, DTid.y*2)] = yuv2.x;
    outYTex[int2(DTid.x*2, DTid.y*2+1)] = yuv3.x;
    outYTex[int2(DTid.x*2+1, DTid.y*2+1)] = yuv4.x;
    
    // 计算平均UV分量
    float2 avgUV = float2(
        (yuv1.y + yuv2.y + yuv3.y + yuv4.y) / 4.0f,
        (yuv1.z + yuv2.z + yuv3.z + yuv4.z) / 4.0f
    );
    
    // 写入UV分量
    outUVTex[DTid] = avgUV;
}
)";

Dx11VideoYuv::Dx11VideoYuv() { renderType = RenderType::D3D11; }

bool Dx11VideoYuv::vaildAndInitGraph(const GpuFrame& frame) {
  if (computeShader) {
    return true;
  }
  IDx11Context* context = static_cast<IDx11Context*>(frame.context);
  ID3D11Texture2D* rgbaTexture = (ID3D11Texture2D*)context->getTexture();
  D3D11_TEXTURE2D_DESC desc = {};
  rgbaTexture->GetDesc(&desc);
  if (desc.Width == 0 || desc.Height == 0) {
    return false;
  }
  if (device != context->getDevice() || imageWidth != desc.Width ||
      imageHeight != rgbaDesc.Height || !computeShader) {
    setDevice(context->getDevice());
    imageWidth = desc.Width;
    imageHeight = desc.Height;
    rgbaDesc = desc;
    createProgram();
  }
  return computeShader != nullptr;
}

void Dx11VideoYuv::releaseGraph() {
  if (computeShader) {
    computeShader->Release();
    computeShader = nullptr;
  }
}

void Dx11VideoYuv::renderGpuFrame(const GpuFrame& frame) {
  renderToTexture(frame);
}

void Dx11VideoYuv::createProgram() {
  if (!device || !d3dcontext) {
    return;
  }
  // 编译着色器
  ID3DBlob* shaderBlob = nullptr;
  ID3DBlob* errorBlob = nullptr;
  HRESULT hr =
      D3DCompile(rgbaToNv12Shader, strlen(rgbaToNv12Shader), nullptr, nullptr,
                 nullptr, "main", "cs_5_0", 0, 0, &shaderBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      log(LogLevel::warn, "Dx11VideoYuv D3DCompile error: ",
          (char*)errorBlob->GetBufferPointer());
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
  // 创建输入复制纹理
  inTexture = std::make_unique<Dx11Texture>();
  inTexture->setTextureSize(imageWidth, imageHeight,
                            DXGI_FORMAT_R8G8B8A8_UNORM);
  inTexture->initResource(device);
  // 创建一个NV12,可以供写入UAV纹理
  D3D11_TEXTURE2D_DESC nv12Desc = {};
  nv12Desc.Width = imageWidth;
  nv12Desc.Height = imageHeight;
  // NV12 格式
  nv12Desc.Format = DXGI_FORMAT_NV12;
  nv12Desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  nv12Desc.MipLevels = 1;
  nv12Desc.ArraySize = 1;
  nv12Desc.SampleDesc.Count = 1;
  nv12Desc.Usage = D3D11_USAGE_DEFAULT;
  AVOX_WIN_LOG(device->CreateTexture2D(&nv12Desc, nullptr, &nv12Texture),
              "create nv12 texture failed");
  // 创建输入纹理的SRV
  D3D11_UNORDERED_ACCESS_VIEW_DESC yDesc = {};
  yDesc.Format = DXGI_FORMAT_R8_UNORM;
  yDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  yDesc.Texture2D.MipSlice = 0;
  device->CreateUnorderedAccessView(nv12Texture.Get(), &yDesc, &yView);
  D3D11_UNORDERED_ACCESS_VIEW_DESC uvDesc = {};
  uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;  // UV 平面是双通道
  uvDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  uvDesc.Texture2D.MipSlice = 0;
  device->CreateUnorderedAccessView(nv12Texture.Get(), &uvDesc, &uvView);
}

void Dx11VideoYuv::renderToTexture(const GpuFrame& frame) {
  Dx11Context* context = static_cast<Dx11Context*>(frame.context);
  ID3D11Texture2D* rgbaTexture = context->getTexture();
  D3D11_TEXTURE2D_DESC desc = {};
  rgbaTexture->GetDesc(&desc);
  if (!computeShader || !nv12Texture || !constBuf || !inTexture) {
    return;
  }
  // 将 rgbaTexture 的数据复制给 inTexture
  if (desc.ArraySize > 1) {
    d3dcontext->CopySubresourceRegion(
        inTexture->texture.Get(), D3D11CalcSubresource(0, 0, desc.MipLevels), 0,
        0, 0, rgbaTexture,
        D3D11CalcSubresource(0, frame.queueIndex, desc.MipLevels), nullptr);
  } else {
    d3dcontext->CopyResource(inTexture->texture.Get(), rgbaTexture);
  }
  // 设置常量缓冲区
  d3dcontext->CSSetConstantBuffers(0, 1, constBuf->buffer.GetAddressOf());
  // 设置计算着色器
  d3dcontext->CSSetShader(computeShader.Get(), nullptr, 0);
  // 设置输入纹理
  ID3D11ShaderResourceView* srvArray[1] = {inTexture->srvView.Get()};
  d3dcontext->CSSetShaderResources(0, 1, srvArray);
  // 设置输出纹理的 UAV
  ID3D11UnorderedAccessView* uavArray[2] = {yView.Get(), uvView.Get()};
  d3dcontext->CSSetUnorderedAccessViews(0, 2, uavArray, nullptr);
  // 执行计算着色器
  uint32_t groupX = divUp(imageWidth / 2, 16);
  uint32_t groupY = divUp(imageHeight / 2, 16);
  d3dcontext->Dispatch(groupX, groupY, 1);
  // 解绑资源
  ID3D11ShaderResourceView* nullSRVs[1] = {nullptr};
  ID3D11UnorderedAccessView* nullUAVs[2] = {nullptr, nullptr};
  d3dcontext->CSSetShaderResources(0, 1, nullSRVs);
  d3dcontext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
}

}