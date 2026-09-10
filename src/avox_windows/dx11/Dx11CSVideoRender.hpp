#pragma once

#include "../dx12/Dx12Helper.hpp"
#include "Dx11Resource.hpp"
#include "avox/video/VideoBuffer.hpp"
#include "avox/video/VideoRender.hpp"

namespace avox {

// 把NV12纹理转换为RGBA8纹理
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