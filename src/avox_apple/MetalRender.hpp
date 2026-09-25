#include "avox/video/VideoRender.hpp"
#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include "MetalContext.hpp"

namespace avox {

class MetalRender : public VideoRender, public MetalContext {
public:
  MetalRender();
  virtual ~MetalRender();

 protected:
  id<MTLRenderPipelineState> pipelineState = nil;
  CVMetalTextureCacheRef cacheTexture = nullptr;
  id<MTLSamplerState> samplerState = nil;
  CAMetalLayer *metalLayer = nullptr;
  // forceHDR 直通管线态: 色附 RGBA16Float(与层格式同帧对齐, 翻转即重建)
  bool bF16Pipeline = false;
  id<MTLTexture> outputTexture = nil;
  // 上一帧真正画过的目标纹理(有 layer 时是那次 present 的 drawable 纹理),
  // 供 fetchFrame 抓帧读取; checkShot 紧跟渲染在同一线程调, 内容即最新一帧
  id<MTLTexture> lastTargetTexture = nil;
  IOSurfaceRef ioSurface = nullptr;
  // CPU NV12直取(bOutCpuYuv时): 持有锁定中的CVPixelBuffer零拷发布
  // releaseGpuFrame会CVBufferRelease,故在renderGpuFrame内buffer存活时锁定
  CVPixelBufferRef cpuPb = nullptr;
  ImageBuffer cpuBuffer;
  // VT biplanar平面间常有对齐间隙, 不连续时聚合到紧凑packed的暂存
  std::vector<uint8_t> cpuPack;
  bool bCpuPublished = false;
  uint32_t publishedTick = 0;
  // 已发布CPU帧的真实类型: VT 硬解 P010(x420) 时是 p010 而非 nv12 ——
  // 交付 type 若硬编码 nv12 会把 10bit 帧谎报成 8bit (yuvout-h264-hi10p
  // 哨兵用例在 macOS 实证 type-mismatch)。publishCpuFrame 按 pbType 填写
  YuvType cpuPublishedType = YuvType::nv12;

 protected:
  virtual void onSetSurface() override;
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame &frame) override;
  // 软解CPU帧(mpeg1/2/4、wmv等无硬解车道格式): planar 420P 收进 NV12
  // CVPixelBuffer 走既有绘制管线, 不补则这些格式在 mac 恒黑屏
  virtual void renderCpuFrame(const YUVFrame &frame) override;
  virtual bool fetchFrame(ImageBuffer *imageBuffer) override;
  // 交付本帧已发布的CPU NV12,渲染线程内调用
  virtual bool getCpuFrameBuffer(IImageBuffer **buffer, YuvType &yuvType,
                                 int64_t *pts) override;

public:
  virtual IRenderContext *getGpuContext() override;
  virtual ImageFormat getImageFormat() override;
  virtual IOSurfaceRef getIOSurface() override;

 protected:
  // 颜色/HDR 参数(与 Dx11CSVideoRender 同策略): 随帧进 setFragmentBytes,
  // 无需重建管线。transfer=pq/hlg 且非 forceHDR 时走 tone map
  ColorSpaceDesc cs;
  HdrMeta hdrMeta;
  HdrMode hdrMode = HdrMode::follow;
  // 颜色矩阵: buildYuvToRgb 已含标准系数+limited 量程展开, 行优先 16 浮点,
  // 逐帧经 setFragmentBytes(buffer 1)下发, 替换 shader 内硬编码的 BT.601
  float colorMatData[16] = {};
  virtual void setColorSpace(const ColorSpaceDesc& c) override;
  virtual void setHdrMeta(const HdrMeta& meta) override;
  virtual void setHdrMode(HdrMode mode) override;

private:
  // 把 buildYuvToRgb(cs) 展开成行优先 16 浮点, 写入 colorMatData
  void updateColorMat();
  void createPipelineState();
  void createTextureCache();
  void closePipelineState();
  void closeTextureCache();

 public:
  void renderCVPixelBuffer(CVImageBufferRef imageBuffer);
  // 锁定并零拷发布当前NV12/x420 CVPixelBuffer到cpuBuffer(每帧最多一次)
  void publishCpuFrame(CVImageBufferRef imageBuffer);

private:
  void logIOSurface();
};

}
