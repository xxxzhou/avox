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

}
