#pragma once

#include "VideoRender.hpp"
#include "ColorSpace.hpp"
#include "avox/module/Observer.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#endif

namespace avox {

// 前向声明
class RawMuxer;

// Vulkan 计算管线 - 管理 Vulkan GPU 图像处理
// 每帧计算结果在 VkOutputLayer 里
// 不持有平台渲染器，不持有窗口
class AVOX_EXPORT SurfaceRenderVk : public ISurfaceRender,
                                   public Observer<ISurfaceRenderOb> {
 public:
  SurfaceRenderVk();
  virtual ~SurfaceRenderVk();

 protected:
  // 如果为true,表明是vulkan窗口,结合bNoSurface
  // 有三种情况,vulkan窗口,原生窗口,空窗口
  bool bVulkan = true;
  // 如果为true,表明是空窗口
  bool bOffSurface = false;
  // 注意windows可传空,由窗口自身生成surface
  AvoxSurfaceType surface = nullptr;
  YuvType outCpuYuv = YuvType::other;
#ifdef AVOX_ENABLE_VULKAN
  // Vulkan 渲染管线（唯一渲染器）
  std::unique_ptr<VkVideoRender> vkVideoRender = nullptr;
#else
  // 未启用 Vulkan 时，vkVideoRender 为空指针
  VideoRender* vkVideoRender = nullptr;
#endif

 public:
  // setVulkan: 固定 Vulkan，不支持切换
  virtual void setVulkan(bool bVulkan) override;
  virtual void setSurface(void* surface) override;
  virtual void* getSurface() override;
  // 离屏 CPU 输出
  virtual void setOffSurface(YuvType ytype) override;
  // 启用/禁用YUV输出
  virtual void enableYuvOut(YuvType ytype) override;
  virtual void disableYuvOut() override;
  // 启用/禁用每帧图像输出 (enableImage) - 委托给 VkVideoRender
  virtual void enableImage(IImageBuffer* buf) override;
  virtual void disableImage() override;
  virtual bool screenShot(IImageBuffer* imageBuffer) override;
  // 图像处理 API - 委托给 VkVideoRender
  virtual void enableSizeChange(int32_t width, int32_t height) override;
  virtual void enableSizeScale(float scale) override;
  virtual void disableSizeChange() override;
  virtual void enableAnime4K(const Anime4KParamet& paramet) override;
  virtual void disableAnime4K() override;
  virtual void enableQualityEnhance(const QualityEnhanceParamet& paramet) override;
  virtual void disableQualityEnhance() override;
  virtual void enableFSR(const FSRParamet& paramet) override;
  virtual void disableFSR() override;
  virtual void enableWatermark(const Watermark& paramet,
                               IImageBuffer* imageBuffer) override;
  virtual void disableWatermark() override;
  virtual void enableLut(const LutParamet& paramet) override;
  virtual void disableLut() override;
  virtual void enableBasicAdjust(const BasicAdjustParamet& paramet) override;
  virtual void disableBasicAdjust() override;
  virtual void updateSharpen(const SharpenVideo& paramet) override;
  virtual void disableSharpen() override;
  // 颜色空间(矩阵), 转发 VkVideoRender, 不重建 graph
  void setColorSpace(const ColorSpaceDesc& c);
  virtual void setAutoAspect(bool bEnable) override;

  // 获取底层 VideoRender
  VideoRender* getVkVideoRender();

 public:
  virtual void onSurfaceChange();
  // VideoFrame必需是下面二种GpuFrame/YUVFrame
  virtual void render(const VideoFrame& frame);
  // 这个GPUFrame只有DX11 RGBA/BGRA格式支持
  // 如果是NV12的GPU资源,需要SurfaceRenderNative处理
  virtual void render(const GpuFrame& frame);
  // 渲染接口 - 纯 Vulkan 路径
  virtual void render(const YUVFrame& frame);
  // Vk管线结果输出到dx11/metal/opengles原生资源里
  void renderOut(IRenderContext* frame);
  // 输出的帧大小
  virtual vec2i getOutSize();
  // 拿到处理过后的CPU资源
  virtual bool getCpuFrame(YUVFrame& frame);
  // 直接取packed CPU帧(不经split重排),供ISurfaceRenderOb::onFrame透传
  virtual bool getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType);
  // 拿到处理过后的GPU资源
  virtual bool getGpuFrame(GpuFrame& frame);
  // 如果有输出CPU资源,得到CPU的帧的YUV类型
  virtual YuvType getOutYuv();
  // 帧输出（供 RawMuxer 录制，纯 Vulkan 操作）
  virtual void pushFrame(RawMuxer* muxer);

 protected:
  // 如果设置输出YUV CPU数据,输出到electron
  virtual void onRenderOut();
};

#ifdef AVOX_ENABLE_VULKAN
VkVideoRender* getVkVideoRender(ISurfaceRender* render);
#endif

}
