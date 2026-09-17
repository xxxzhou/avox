#include "SurfaceRenderVk.hpp"

#include "../muxer/RawMuxer.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#endif

namespace avox {

SurfaceRenderVk::SurfaceRenderVk() {
#ifdef AVOX_ENABLE_VULKAN
  if (canVulkan()) {
    bVulkan = true;
    vkVideoRender = std::make_unique<VkVideoRender>();
  } else {
    bVulkan = false;
    LOGFLF(LogLevel::warn, "not support vulkan");
  }
#else
  bVulkan = false;
#endif
}

SurfaceRenderVk::~SurfaceRenderVk() {}

VideoRender* SurfaceRenderVk::getVkVideoRender() {
#ifdef AVOX_ENABLE_VULKAN
  return vkVideoRender.get();
#endif
  return nullptr;
}

void SurfaceRenderVk::setVulkan(bool bVulkan) {
  LOGFLF(LogLevel::warn, "not support set vulkan,now vulkan:", bVulkan);
}

void SurfaceRenderVk::setOffSurface(YuvType ytype) {
  bOffSurface = true;
  enableYuvOut(ytype);
  onSurfaceChange();
}

void SurfaceRenderVk::setSurface(void* surface_) {
  surface = getNativeSurface(surface_);
  bOffSurface = false;
  onSurfaceChange();
}

void* SurfaceRenderVk::getSurface() { return surface; }

void SurfaceRenderVk::enableYuvOut(YuvType ytype) {
  outCpuYuv = ytype;
  if (vkVideoRender) {
    if (outCpuYuv != YuvType::other) {
      vkVideoRender->enableYuvOut(outCpuYuv);
    } else {
      vkVideoRender->disableYuvOut();
    }
  }
}

void SurfaceRenderVk::disableYuvOut() {
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->disableYuvOut();
}

void SurfaceRenderVk::enableImage(IImageBuffer* buf) {
  if (vkVideoRender) {
    vkVideoRender->enableImage(buf);
  }
}

void SurfaceRenderVk::disableImage() {
  if (vkVideoRender) {
    vkVideoRender->disableImage();
  }
}

bool SurfaceRenderVk::screenShot(IImageBuffer* imageBuffer) {
  ImageBuffer* imgBuffer = dynamic_cast<ImageBuffer*>(imageBuffer);
  if (!imgBuffer) {
    LOGFLF(LogLevel::warn, "invalid imageBuffer");
    return false;
  }
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return false;
  }
  return vkVideoRender->screenShot(imgBuffer);
}

void SurfaceRenderVk::enableSizeChange(int32_t width, int32_t height) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  LOGFLF(LogLevel::info, "width:", width, " height:", height);
  vkVideoRender->enableSizeChange(width, height);
}

void SurfaceRenderVk::enableSizeScale(float scale) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  LOGFLF(LogLevel::info, "scale:", scale);
  vkVideoRender->enableSizeScale(scale);
}

void SurfaceRenderVk::disableSizeChange() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableSizeChange();
}

void SurfaceRenderVk::enableAnime4K(const Anime4KParamet& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  LOGFLF(LogLevel::info, "enable Anime4K mode:", (int32_t)paramet.mode);
  vkVideoRender->enableAnime4K(paramet);
}

void SurfaceRenderVk::disableAnime4K() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableAnime4K();
}

void SurfaceRenderVk::enableQualityEnhance(const QualityEnhanceParamet& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  LOGFLF(LogLevel::info, "enable QualityEnhance mode:", (int32_t)paramet.outputMode);
  vkVideoRender->enableQualityEnhance(paramet);
}

void SurfaceRenderVk::disableQualityEnhance() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableQualityEnhance();
}

void SurfaceRenderVk::enableFSR(const FSRParamet& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  LOGFLF(LogLevel::info, "enable FSR scale:", (int32_t)paramet.scale);
  vkVideoRender->enableFSR(paramet);
}

void SurfaceRenderVk::disableFSR() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableFSR();
}

void SurfaceRenderVk::enableWatermark(const Watermark& paramet,
                                      IImageBuffer* imageBuffer) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->enableWatermark(paramet, imageBuffer);
}

void SurfaceRenderVk::disableWatermark() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableWatermark();
}

void SurfaceRenderVk::enableLut(const LutParamet& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->enableLut(paramet);
}

void SurfaceRenderVk::disableLut() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableLut();
}

void SurfaceRenderVk::enableBasicAdjust(const BasicAdjustParamet& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->enableBasicAdjust(paramet);
}

void SurfaceRenderVk::disableBasicAdjust() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableBasicAdjust();
}

void SurfaceRenderVk::updateSharpen(const SharpenVideo& paramet) {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->updateSharpen(paramet);
}

void SurfaceRenderVk::disableSharpen() {
  if (!vkVideoRender) {
    LOGFLF(LogLevel::warn, "invalid vkVideoRender");
    return;
  }
  vkVideoRender->disableSharpen();
}

void SurfaceRenderVk::enableVr(const VrParamet& paramet) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->enableVr(paramet);
  }
#endif
}

void SurfaceRenderVk::disableVr() {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->disableVr();
  }
#endif
}

void SurfaceRenderVk::rotateView(float deltaYaw, float deltaPitch) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->rotateView(deltaYaw, deltaPitch);
  }
#endif
}

void SurfaceRenderVk::zoomView(float deltaFov) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->zoomView(deltaFov);
  }
#endif
}

void SurfaceRenderVk::resetView() {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->resetView();
  }
#endif
}

void SurfaceRenderVk::getViewAngles(float* yaw, float* pitch, float* fov) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->getViewAngles(yaw, pitch, fov);
  }
#endif
}

void SurfaceRenderVk::setVrOutMode(VrOutMode mode) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->setVrOutMode(mode);
  }
#endif
}

void SurfaceRenderVk::setVrStereoStrength(float strengthDeg) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->setVrStereoStrength(strengthDeg);
  }
#endif
}

void SurfaceRenderVk::setColorSpace(const ColorSpaceDesc& c) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->setColorSpace(c);
  }
#else
  (void)c;
#endif
}

void SurfaceRenderVk::setHdrMeta(const HdrMeta& meta) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->setHdrMeta(meta);
  }
#else
  (void)meta;
#endif
}

void SurfaceRenderVk::setHdrMode(HdrMode mode) {
#ifdef AVOX_ENABLE_VULKAN
  if (vkVideoRender) {
    vkVideoRender->setHdrMode(mode);
  }
#else
  (void)mode;
#endif
}

void SurfaceRenderVk::setAutoAspect(bool bEnable) {
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->setFullScreen(!bEnable);
}

void SurfaceRenderVk::onSurfaceChange() {
  if (vkVideoRender) {
    vkVideoRender->setSurface(bOffSurface ? nullptr : surface);
  }
  dispatch(&ISurfaceRenderOb::onSurface);
}

void SurfaceRenderVk::render(const VideoFrame& frame) {
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->renderFrame(frame, nullptr);
  onRenderOut();
}

void SurfaceRenderVk::render(const GpuFrame& frame) {
  // 能渲染的只有直接的RGBA的原生GPU资源
  if (frame.format.type == YuvType::other) {
    vkVideoRender->renderGpuFrame(frame.context);
  }
  onRenderOut();
}

void SurfaceRenderVk::render(const YUVFrame& frame) {
  if (vkVideoRender) {
    vkVideoRender->renderFrame(frame);
  }
  onRenderOut();
}

void SurfaceRenderVk::renderOut(IRenderContext* frame) {
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->getOutputLayer()->outputGpuData(frame);
}

YuvType SurfaceRenderVk::getOutYuv() {
  // 离屏CPU输出时才返回有效YuvType
  if (bVulkan) {
    return outCpuYuv;
  }
  return YuvType::other;
}

bool SurfaceRenderVk::getCpuFrame(YUVFrame& frame) {
  if (vkVideoRender && vkVideoRender->bCpuOut()) {
    return vkVideoRender->getCpuFrame(frame);
  }
  return false;
}

bool SurfaceRenderVk::getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType) {
  if (vkVideoRender && vkVideoRender->bCpuOut()) {
    return vkVideoRender->getCpuFrameBuffer(buffer, yuvType);
  }
  return false;
}

bool SurfaceRenderVk::getGpuFrame(GpuFrame& frame) {
  // IOS/ANDROID经vulkan处理后对应的metal/opengles资源
  if (vkVideoRender) {
    return vkVideoRender->getGpuFrame(frame);
  }
  return false;
}

vec2i SurfaceRenderVk::getOutSize() {
  if (vkVideoRender) {
    return vkVideoRender->getOutSize();
  }
  return vec2i(0, 0);
}

void SurfaceRenderVk::pushFrame(RawMuxer* muxer) {
  if (!vkVideoRender) {
    return;
  }
  // CPU输出
  if (vkVideoRender->bCpuOut()) {
    YUVFrame yframe = {};
    bool bGet = vkVideoRender->getCpuFrame(yframe);
    if (bGet) {
      // 转发解码推流
      muxer->pushFrame(yframe);
    }
  } else {
    GpuFrame vframe = {};
    bool bGet = vkVideoRender->getGpuFrame(vframe);
    if (bGet) {
      muxer->pushFrame(vframe);
    }
  }
}

void SurfaceRenderVk::onRenderOut() {
  IImageBuffer* buf = nullptr;
  YuvType yuvType = YuvType::other;
  // 给上层透传packed CPU帧,由观察者自行决定是否转split
  if (getCpuFrameBuffer(&buf, yuvType)) {
    dispatch(&ISurfaceRenderOb::onFrame, buf, yuvType);
  }
  // 渲染输出事件: 世代/格式/句柄全为内存读, outputLayer 重建窗口期为空值
  SurfaceRenderEvent ev = {};
#ifdef AVOX_ENABLE_VULKAN
  VkVideoRender* vkRender = vkVideoRender.get();
  VkOutputLayer* outputLayer = vkRender ? vkRender->getOutputLayer() : nullptr;
  if (outputLayer) {
    ev.generation = outputLayer->getGeneration();
    ev.format = outputLayer->getOutFormat();
    if (vkRender->getHdrMode() == HdrMode::forceHDR) {
      ev.hdr = 1;
    }
#ifdef WIN32
    if (outputLayer->getWinImage() && outputLayer->getWinImage()->getInit()) {
      ev.dx11Handle =
          (uint64_t)(uintptr_t)outputLayer->getWinImage()->getHandle();
      ev.dx11Fence =
          (uint64_t)(uintptr_t)outputLayer->getWinImage()->getFenceHandle();
    }
#endif
#ifdef __APPLE__
    if (outputLayer->getIosImage() &&
        outputLayer->getIosImage()->getIOSurface()) {
      ev.ioSurface = outputLayer->getIosImage()->getIOSurface();
      ev.ioSurfaceId = outputLayer->getIosImage()->getIOSurfaceId();
    }
#endif
    if (ev.generation != 0 && ev.generation != lastEventGeneration) {
      ev.rebuilt = 1;
      lastEventGeneration = ev.generation;
    }
  }
#endif
  dispatch(&ISurfaceRenderOb::onRender, &ev);
}

#ifdef AVOX_ENABLE_VULKAN
VkVideoRender* getVkVideoRender(ISurfaceRender* render) {
  if (!render) {
    return nullptr;
  }
  SurfaceRenderVk* wrender = dynamic_cast<SurfaceRenderVk*>(render);
  if (!wrender) {
    return nullptr;
  }
  VideoRender* pVideoRender = wrender->getVkVideoRender();
  if (!pVideoRender) {
    return nullptr;
  }
  VkVideoRender* vkVideoRender = dynamic_cast<VkVideoRender*>(pVideoRender);
  if (!vkVideoRender) {
    return nullptr;
  }
  return vkVideoRender;
}
#endif
}