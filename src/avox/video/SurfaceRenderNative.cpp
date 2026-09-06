#include "SurfaceRenderNative.hpp"

#include "avox/module/AvoxManager.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#endif
#ifdef WIN32
#include "avox_windows/dx11/Dx11Window.hpp"
#endif
#ifdef __ANDROID__
#include "avox_egl/EglWindow.hpp"
#endif
#ifdef __APPLE__
#include "avox_apple/MetalWindow.hpp"
#endif

namespace avox {

SurfaceRenderNative::SurfaceRenderNative() {
  RenderType rtype = RenderType::other;
#ifdef WIN32
  rtype = RenderType::D3D11;
#elif __ANDROID__
  rtype = RenderType::OpenGLES;
#elif __APPLE__
  rtype = RenderType::Metal;
#endif
  if (rtype != RenderType::other) {
    // 不管是否vulkan,都需要平台本身SDK把NV12转换为RGBA
    const auto& rendClass = AvoxManager::Get().vRender.initFunc(rtype);
    if (rendClass.initFunc) {
      pVideoRender = std::unique_ptr<VideoRender>(rendClass.initFunc());
    }
    if (!pVideoRender) {
      LOGFLF(LogLevel::warn,
             "no initFunc for rtype:", getVRenderTypeStr(rtype));
    }
  }
}

SurfaceRenderNative::~SurfaceRenderNative() {}

void SurfaceRenderNative::setVulkan(bool bVulkan_) {
#ifdef AVOX_ENABLE_VULKAN
  if (!canVulkan() && bVulkan_) {
    bVulkan_ = false;
    LOGFLF(LogLevel::warn, "not support vulkan");
  }
#else
  LOGFLF(LogLevel::warn, "not use vulkan");
  bVulkan_ = false;
#endif
  if (bVulkan != bVulkan_) {
    bVulkan = bVulkan_;
    LOGFLF(LogLevel::info, "change use vulkan:", bVulkan);
  }
}

void SurfaceRenderNative::setVideoSurface(AvoxSurfaceType surface) {
  if (bOffSurface) {
    LOGFLF(LogLevel::info, "use empty surface, cpu out:", outCpuYuv);
    if (pVideoRender) {
      pVideoRender->setSurface(nullptr);
    }
    if (vkVideoRender) {
      vkVideoRender->setSurface(nullptr);
    }
  } else {
    if (pVideoRender) {
      if (surface && !bVulkan) {
        pVideoRender->setSurface(surface);
      } else {
        // 如果用vulkan窗口,还是用离屏渲染
        pVideoRender->setSurface(nullptr);
      }
    }
    if (vkVideoRender) {
      vkVideoRender->setSurface(surface);
    }
  }
  dispatch(&ISurfaceRenderOb::onSurface);
}

void SurfaceRenderNative::onSurfaceChange() { setVideoSurface(surface); }

bool SurfaceRenderNative::screenShot(IImageBuffer* imageBuffer) {
  ImageBuffer* imgBuffer = dynamic_cast<ImageBuffer*>(imageBuffer);
  if (!imgBuffer) {
    LOGFLF(LogLevel::warn, "invalid imageBuffer");
    return false;
  }
  if (bVulkan) {
    if (!vkVideoRender) {
      LOGFLF(LogLevel::warn, "invalid vkVideoRender");
      return false;
    }
    return vkVideoRender->screenShot(imgBuffer);
  } else {
    if (!pVideoRender) {
      LOGFLF(LogLevel::warn, "invalid pVideoRender");
      return false;
    }
    return pVideoRender->screenShot(imgBuffer);
  }
}

void SurfaceRenderNative::render(const VideoFrame& frame) {
  if (!frame.buffer) {
    return;
  }
  // 原生dx11/opengles/metal渲染
  if (pVideoRender) {
    pVideoRender->renderFrame(frame);
  }
  // 如果由vulkan渲染到窗口，把上面GPU资源映射到vulkan管线中
  if (bVulkan && vkVideoRender) {
    IRenderContext* rxt = nullptr;
    if (pVideoRender) {
      rxt = pVideoRender->getGpuContext();
    }
    vkVideoRender->renderFrame(frame, rxt);
  }
  onRenderOut();
}

void SurfaceRenderNative::render(const GpuFrame& frame) {
  if (frame.format.type != YuvType::other) {
    // NV12需要先经原生dx11/opengles/metal处理
    if (pVideoRender) {
      pVideoRender->renderFrame(frame);
    }
  }
  // 如果由vulkan渲染到窗口，把上面GPU结果渲染到vulkan管线中
  if (bVulkan && vkVideoRender) {
    // vulkan不直接渲染原生GPU,只是引发vaildAndInitGraph
    vkVideoRender->renderFrame(frame);
    // YUV资源,先映射原生dx11/opengles/metal处理结果到管线中
    if (frame.format.type != YuvType::other && pVideoRender) {
      vkVideoRender->renderGpuFrame(pVideoRender->getGpuContext());
    } else {
      // 如果是RGBA资源,直接映射
      vkVideoRender->renderGpuFrame(frame.context);
    }
  }
  onRenderOut();
}

void SurfaceRenderNative::render(const YUVFrame& frame) {
  // Vulkan时走Vk，非Vulkan时走pVideoRender
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderFrame(frame);
  } else {
    if (pVideoRender) {
      pVideoRender->renderFrame(frame);
    }
  }
  onRenderOut();
}

void SurfaceRenderNative::setAutoAspect(bool bEnable) {
  if (vkVideoRender) {
    vkVideoRender->setFullScreen(!bEnable);
  }
  if (pVideoRender) {
    pVideoRender->setFullScreen(!bEnable);
  }
}

vec2i SurfaceRenderNative::getOutSize() {
  if (bVulkan) {
    return vkVideoRender->getOutSize();
  }
  if (pVideoRender) {
    ImageFormat format = pVideoRender->getImageFormat();
    return vec2i(format.width, format.height);
  }
  return vec2i(0, 0);
}

bool SurfaceRenderNative::getCpuFrame(YUVFrame& frame) {
  if (!SurfaceRenderVk::getCpuFrame(frame)) {
    if (pVideoRender) {
      return pVideoRender->getCpuFrame(frame);
    }
    return false;
  }
  return true;
}

}