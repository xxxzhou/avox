#pragma once

#include <d3d11_1.h>

#include "Dx11SharedTex.hpp"
#include "avox/video/VideoBuffer.hpp"
#include "avox/video/VideoRender.hpp"

namespace avox {

// 使用 D3D11 Video Processor 将 NV12 纹理转换为 RGBA8 纹理
// 相比 Compute Shader，VideoProcessor 使用硬件加速的视频处理单元，GPU 占用更低
class VideoProcessRender : public VideoRender, public Dx11Context {
 public:
  VideoProcessRender();
  virtual ~VideoProcessRender();

 protected:
  // Video Processor 相关资源
  MComPtr<ID3D11VideoDevice> videoDevice = nullptr;
  MComPtr<ID3D11VideoContext> videoContext = nullptr;
  MComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnumerator = nullptr;
  MComPtr<ID3D11VideoProcessor> videoProcessor = nullptr;
  MComPtr<ID3D11VideoProcessorOutputView> outputView = nullptr;
  MComPtr<ID3D11VideoProcessorInputView> inputView = nullptr;

  // 输出纹理
  // std::unique_ptr<Dx11Texture> outTexture = nullptr;
  // 输出共享纹理
  std::unique_ptr<Dx11SharedTex> outSharedTex = nullptr;
  // 输入纹理（带有 SRV）
  MComPtr<ID3D11Texture2D> inTexture = nullptr;
  MComPtr<ID3D11ShaderResourceView> yView = nullptr;
  MComPtr<ID3D11ShaderResourceView> uvView = nullptr;

  uint32_t imageWidth = 0;
  uint32_t imageHeight = 0;
  D3D11_TEXTURE2D_DESC inDesc = {};

  // 颜色空间设置
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};

 protected:
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual bool fetchFrame(ImageBuffer* imageBuffer) override;

 public:
  virtual IRenderContext* getGpuContext() override {
    return outSharedTex.get();
  }

 public:
  void createVideoProcessor();
  void renderToTexture(const GpuFrame& gpuFrame);
};

}
