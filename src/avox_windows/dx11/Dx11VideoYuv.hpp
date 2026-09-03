#pragma once

#include "Dx11Resource.hpp"
#include "avox/video/VideoBuffer.hpp"
#include "avox/video/VideoYuv.hpp"

namespace avox {

// 把RGBA8纹理转换为NV12纹理
class Dx11VideoYuv : public VideoYuv, public Dx11Context {
public:
  Dx11VideoYuv();
  virtual ~Dx11VideoYuv() {};

protected:
  // RGBA8 2 NV12 shader
  MComPtr<ID3D11ComputeShader> computeShader = nullptr;
  // 输入RGBA纹理
  std::unique_ptr<Dx11Texture> inTexture = nullptr;
  // 输出的NV12纹理
  // std::unique_ptr<Dx11Texture> nv12Texture = nullptr;
  MComPtr<ID3D11Texture2D> nv12Texture = nullptr;
  MComPtr<ID3D11UnorderedAccessView> yView = nullptr;
  MComPtr<ID3D11UnorderedAccessView> uvView = nullptr;
  // 常量缓冲区
  std::unique_ptr<Dx11Constant> constBuf = nullptr;
  uint32_t imageWidth = 0;
  uint32_t imageHeight = 0;
  D3D11_TEXTURE2D_DESC rgbaDesc = {};

protected:
  // 初始化图形管线
  virtual bool vaildAndInitGraph(const GpuFrame& frame) override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;

public:
  void createProgram();
  void renderToTexture(const GpuFrame& frame);
  ID3D11Texture2D *getNv12Texture() { return nv12Texture.Get(); }
};
}