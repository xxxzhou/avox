#include "ImageRender.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkWindow.hpp"
#endif

namespace avox {

ImageRender::ImageRender() {}

ImageRender::~ImageRender() {
  if (window) {
    window->close();
  }
}

ISurfaceRender* ImageRender::getSurfaceRender() {
  // SurfaceRenderVk 继承 ISurfaceRender,直接返回this
  return this;
}

void ImageRender::setSurface(void* surface_) {
  // 获取平台原生surface
  AvoxSurfaceType nativeSurface = getNativeSurface(surface_);
  this->surface = nativeSurface;
  this->bOffSurface = false;
  // 创建或更新窗口
  if (!surface_) {
    // nullptr = 自动创建Vulkan窗口
    if (window) {
      window->close();
    }
#ifdef AVOX_ENABLE_VULKAN
    if (bVulkan) {
      window = std::make_unique<VkWindow>();
      LOGFLF(LogLevel::info, "ImageRender: use vulkan window");
    }
#endif
    if (window) {
      window->initSurface(nullptr);
      window->setObserver(this);
    }
  } else {
    // 使用外部surface
    if (window) {
      bool bMatch = (window->getRenderType() == RenderType::Vulkan) == bVulkan;
      if (bMatch) {
        window->initSurface(surface_);
      } else {
        window.reset();
      }
    }
    if (!window) {
#ifdef AVOX_ENABLE_VULKAN
      if (bVulkan) {
        window = std::make_unique<VkWindow>();
        LOGFLF(LogLevel::info, "ImageRender: use vulkan window, surface:",
               surface_);
      }
#endif
      if (window) {
        window->initSurface(surface_);
        window->setObserver(this);
      }
    }
  }
  // 设置VkVideoRender的surface
  if (window && window->getSurface()) {
    vkVideoRender->setSurface(window->getSurface());
  } else {
    vkVideoRender->setSurface(nullptr);
  }
  dispatch(&ISurfaceRenderOb::onSurface);
}

void* ImageRender::getSurface() {
  if (window) {
    return window->getSurface();
  }
  return SurfaceRenderVk::getSurface();
}

void ImageRender::setAutoAspect(bool bEnable) {
  SurfaceRenderVk::setAutoAspect(bEnable);
  if (window) {
    window->setFullScreen(!bEnable);
  }
}

void ImageRender::render(IImageBuffer* imageBuffer) {
  if (!imageBuffer || !vkVideoRender) {
    return;
  }
  // 阶段1: 计算渲染(将图像数据输入Vulkan管线)
  vkVideoRender->renderFrame(imageBuffer);
  // 阶段2: 窗口呈现(获取swapchain, blit输出纹理, present)
  if (window && window->getSurface()) {
    window->updateSize();
    if (window->preTick()) {
      window->tick();
    }
  }
}

void ImageRender::render(const YUVFrame& frame) {
  // frame是const引用无法判空,用格式有效性做前置校验
  if (frame.format.width <= 0 || frame.format.height <= 0 || !vkVideoRender) {
    return;
  }
  // 阶段1: 计算渲染(将YUV数据经yuv2RGBA层输入Vulkan管线)
  vkVideoRender->renderFrame(frame);
  // 阶段2: 窗口呈现(获取swapchain, blit输出纹理, present)
  if (window && window->getSurface()) {
    window->updateSize();
    if (window->preTick()) {
      window->tick();
    }
  }
}

void ImageRender::onRenderWindow() {
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderWindow(window.get());
  }
}

IImageRender* createImageRender() { return new ImageRender(); }

// 按帧宽高比猜VR参数(启发式, AvoxLayer.h 声明):
//   >3:1 -> SBS等距柱状360        ~2:1 -> OU等距柱状360(单目全景)
//   16:9等宽幅 -> SBS鱼眼180(VR180主流, 与2:1的分界取1.85)
//   近方形 -> OU鱼眼180
// 判错由宿主手改再 enableVr; 圆心/半径交默认值(内切每眼画幅高、居中)
bool guessVrParamet(int32_t width, int32_t height, VrParamet* out) {
  if (width <= 0 || height <= 0 || !out) {
    return false;
  }
  float aspect = (float)width / (float)height;
  if (aspect > 3.0f) {
    out->projection = VrProjection::equirect360;
    out->eyeLayout = VrEyeLayout::sbs;
  } else if (aspect > 1.85f) {
    out->projection = VrProjection::equirect360;
    out->eyeLayout = VrEyeLayout::ou;
  } else if (aspect > 1.3f) {
    out->projection = VrProjection::fisheye180;
    out->eyeLayout = VrEyeLayout::sbs;
  } else if (aspect > 0.7f) {
    out->projection = VrProjection::fisheye180;
    out->eyeLayout = VrEyeLayout::ou;
  } else {
    out->projection = VrProjection::fisheye180;
    out->eyeLayout = VrEyeLayout::sbs;
  }
  out->fisheyeFov = 180.0f;
  out->centerL[0] = 0.0f;
  out->centerL[1] = 0.0f;
  out->centerR[0] = 0.0f;
  out->centerR[1] = 0.0f;
  out->radiusL = 0.0f;
  out->radiusR = 0.0f;
  return true;
}

}
