#pragma once

#include "SurfaceRenderVk.hpp"

namespace avox {

// 平台渲染对接 - 管理 pVideoRender 与 SurfaceRenderVk 的数据流
// 持有平台 VideoRender（DX11/EGL/Metal），负责原生 GPU NV12→RGBA 转换和 Vulkan
// 对接 setVulkan 控制数据流走向： 开启 Vulkan → RGBA 给 Vk 接手处理 关闭 Vulkan
// → RGBA 给对应 surface 显示
class AVOX_EXPORT SurfaceRenderNative : public SurfaceRenderVk {
 public:
  SurfaceRenderNative();
  virtual ~SurfaceRenderNative();

 protected:
  // 平台渲染器（DX11/EGL/Metal）- 原生 NV12→RGBA 转换
  std::unique_ptr<VideoRender> pVideoRender = nullptr;

 public:
  // setVulkan: 控制双路对接
  // true: pVideoRender 做 NV12→RGBA，交给 Vk 接手处理
  // false: pVideoRender 做 NV12→RGBA，给对应 surface 显示
  virtual void setVulkan(bool bVulkan) override;
  // screenShot: Vulkan时走vkVideoRender，非Vulkan时走pVideoRender
  virtual bool screenShot(IImageBuffer* imageBuffer) override;

 protected:
  void setVideoSurface(AvoxSurfaceType surface);

 public:
  virtual void onSurfaceChange() override;
  // 启用/禁用YUV输出 - 按bVulkan分流: vulkan走vk管线(处理后帧),
  // 非vulkan走平台渲染器原生回读(解码直出NV12)
  virtual void enableYuvOut(YuvType ytype) override;
  virtual void disableYuvOut() override;
  virtual YuvType getOutYuv() override;
  // 录制取帧: 按bVulkan分流
  virtual void pushFrame(RawMuxer* muxer) override;
  // 渲染接口 - 双路对接
  // NV12→RGBA → Vulkan开启时Vk接手，关闭时给surface
  virtual void render(const VideoFrame& frame) override;
  // GpuFrame: NV12需pVideoRender转换，RGBA需配合
  virtual void render(const GpuFrame& frame) override;
  // YUVFrame: Vulkan时走Vk，非Vulkan时走pVideoRender
  virtual void render(const YUVFrame& frame) override;
  // setAutoAspect: 同时设置 Vk 和 pVideoRender
  virtual void setAutoAspect(bool bEnable) override;
  virtual vec2i getOutSize() override;
  virtual bool getCpuFrame(YUVFrame& frame) override;
  // 按bVulkan分流: vulkan走vk管线, 非vulkan走平台渲染器原生回读
  virtual bool getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType) override;
};

}