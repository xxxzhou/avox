#pragma once

#include "../dx12/Dx12Helper.hpp"
#include "Dx11Resource.hpp"
#include "avox/video/VideoBuffer.hpp"
#include "avox/video/VideoRender.hpp"

namespace avox {

// 把NV12/P010纹理转换为RGBA8纹理(P010路径内置 HDR tone map, 与 glsl
// yuv2rgbaV5.comp 同源: PQ/HLG 解码 + ACES + BT.2020->BT.709)
class Dx11CSVideoRender : public VideoRender, public Dx11Context {
 public:
  Dx11CSVideoRender();
  virtual ~Dx11CSVideoRender() {};

 protected:
  // YUV 2 RGBA8 shader
  MComPtr<ID3D11ComputeShader> computeShader = nullptr;
  // 计算着色器资源
  // std::unique_ptr<Dx11Texture> outTexture = nullptr;
  // 输出共享纹理
  std::unique_ptr<Dx11SharedTex> outSharedTex = nullptr;
  Dx11Texture* outTexture = nullptr;
  // 常量缓冲区
  std::unique_ptr<Dx11Constant> constBuf = nullptr;
  // 解码的纹理没有D3D11_BIND_SHADER_RESOURCE,不能直接生成SRV
  MComPtr<ID3D11Texture2D> inTexture = nullptr;
  MComPtr<ID3D11ShaderResourceView> yView = nullptr;
  MComPtr<ID3D11ShaderResourceView> uvView = nullptr;
  uint32_t imageWidth = 0;
  uint32_t imageHeight = 0;
  D3D11_TEXTURE2D_DESC yuvDesc = {};
  // 颜色/HDR 参数(setColorSpace/setHdrMeta/setHdrMode 注入, 随脏标记进常量):
  // P010 硬解的 tone map 在本 CS 内完成(与 glsl yuv2rgbaV5.comp 同源)
  ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
  HdrMeta hdrMeta = {};
  HdrMode hdrMode = HdrMode::follow;
  bool bParamsDirty = true;
  uint32_t constData[24] = {};  // 8 标量(32B) + colorMat(64B) = 96B, 与 cbuffer 对齐
  // CPU NV12直取(bOutCpuYuv时): 复用staging纹理,映射指针零拷发布
  // Unmap顺延到下一帧回读,消费者须在当帧窗口内使用
  MComPtr<ID3D11Texture2D> stagingTexture = nullptr;
  ImageBuffer stagingBuffer;
  bool bStagingMapped = false;
  uint32_t publishedTick = 0;
  int32_t stagingWidth = 0;
  int32_t stagingHeight = 0;

 protected:
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual bool fetchFrame(ImageBuffer* imageBuffer) override;
  // 颜色/HDR 参数(VideoRender 虚接口), 触发常量脏标记
  virtual void setColorSpace(const ColorSpaceDesc& c) override;
  virtual void setHdrMeta(const HdrMeta& meta) override;
  virtual void setHdrMode(HdrMode mode) override;
  // bOutCpuYuv时把当前NV12帧staging回读,渲染线程内按需调用,一帧最多一次
  virtual bool getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType,
                                 int64_t* pts) override;

 public:
  virtual IRenderContext* getGpuContext() override {
    return outSharedTex.get();
  }

 public:
  void createProgram();
  void renderToTexture(const GpuFrame& gpuFrame);
  // staging拷贝+Map+零拷发布到stagingBuffer,失败返回false
  bool mapStagingFrame();
};
}