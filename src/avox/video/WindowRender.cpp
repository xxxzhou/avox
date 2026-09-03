#include "WindowRender.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FontRender.hpp"
#endif

#include "../module/HighClock.hpp"
#include "avox/module/AvoxManager.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#include "avox_vulkan/vulkan/VkWindow.hpp"
#endif
#ifdef WIN32
#include "avox_windows/dx11/Dx11Window.hpp"
#endif
#ifdef __ANDROID__
#include "avox_egl/EglWindow.hpp"
#endif
#ifdef __APPLE__
#include "avox_ios/MetalWindow.hpp"
#endif

namespace avox {

const char* getSyncResultStr(SyncResult syncResult) {
  switch (syncResult) {
#define XX(name, value, str) \
  case SyncResult::name:     \
    return str;
    AVOX_MAP_SYNC_RESULT(XX)
#undef XX
    default:
      return "unknow";
  }
}

WindowRender::WindowRender() { taskName = "surface render"; }

WindowRender::~WindowRender() { stop(); }

void WindowRender::setVulkan(bool bVulkan_) {
#ifndef AVOX_ENABLE_VULKAN
  bVulkan_ = false;
#endif
  if (window) {
    // 运行中改变窗口类型,容易出问题,故改动
    // 如果已有窗口,不能改变类型
    LOGFLF(LogLevel::warn, "window alreay create,not set,now vulkan:", bVulkan);
    return;
  }
  // 调用父类的setVulkan
  SurfaceRenderNative::setVulkan(bVulkan_);
}

void WindowRender::onSurfaceChange() {
  std::lock_guard<std::mutex> lck(mtx);
  if (bOffSurface) {
    if (window) {
      window.reset();
    }
    LOGFLF(LogLevel::info, "use empty surface, cpu out:", outCpuYuv);
  } else {
    // 如果已有窗口,检查是否匹配的窗口类型
    if (window && surface) {
      bool bMatch =
          ((window->getRenderType() == RenderType::Vulkan) == bVulkan);
      // 窗口使用新的surface渲染
      if (bMatch) {
        window->initSurface(surface);
        LOGFLF(LogLevel::info, "window use new suface:", surface);
      }
    }
    if (!window) {
      if (bVulkan) {
        window = std::make_unique<VkWindow>();
        LOGFLF(LogLevel::info, "use vulkan window:", this,
               " surface:", surface);
      } else {
#ifdef WIN32
        window = std::make_unique<Dx11Window>();
#elif __ANDROID__
        window = std::make_unique<EglWindow>();
#elif __APPLE__
        window = std::make_unique<MetalWindow>();
#else
        window = std::make_unique<Window>();
#endif
        LOGFLF(LogLevel::info, "use native window:", this,
               " surface:", surface);
      }
      if (window) {
        window->initSurface(surface);
      }
    }
    window->setObserver(this);
  }
  if (window && window->getSurface()) {
    setVideoSurface(window->getSurface());
  } else {
    setVideoSurface(nullptr);
  }
}

void* WindowRender::getSurface() {
  if (window) {
    return window->getSurface();
  }
  return SurfaceRenderNative::getSurface();
}

void WindowRender::setAutoAspect(bool bEnable) {
  if (vkVideoRender) {
    vkVideoRender->setFullScreen(!bEnable);
  }
  if (pVideoRender) {
    pVideoRender->setFullScreen(!bEnable);
  }
  std::lock_guard<std::mutex> lck(mtx);
  if (window) {
    window->setFullScreen(!bEnable);
  }
}

void WindowRender::render(const avox::VideoFrame& frame) {
  if (!frame.buffer) {
    return;
  }
  // 原生dx11/opengles/metal渲染
  if (pVideoRender) {
    pVideoRender->renderFrame(frame);
#ifdef WIN32
    if (!bVulkan && window) {
      window->renderContext(pVideoRender->getGpuContext());
    }
#endif
  }
  // 如果由vulkan渲染到窗口，把上面GPU结果渲染到vulkan管线中
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderFrame(frame, pVideoRender->getGpuContext());
  }
  onRenderOut();
}

void WindowRender::render(const GpuFrame& frame) {
  if (frame.format.type != YuvType::other) {
    // NV12需要先经原生dx11/opengles/metal处理
    if (pVideoRender) {
      pVideoRender->renderFrame(frame);
    }
  }
  // 如果由vulkan渲染到窗口，把上面GPU结果渲染到vulkan管线中
  if (bVulkan && vkVideoRender) {
    // vulkan不渲染frame,但要用frame来检查vaildAndInitGraph
    vkVideoRender->renderFrame(frame);
    if (frame.format.type != YuvType::other) {
      vkVideoRender->renderGpuFrame(pVideoRender->getGpuContext());
    } else {
      // DX11直接返回RGBA的数据
      vkVideoRender->renderGpuFrame(frame.context);
    }
  }
#ifdef WIN32
  if (!bVulkan && window) {
    window->renderContext(frame.context);
  }
#endif
  onRenderOut();
}

void WindowRender::render(const YUVFrame& frame) {
  // 如果由vulkan渲染到窗口，把上面GPU结果渲染到vulkan管线中
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderFrame(frame);
  } else {
    if (pVideoRender) {
      pVideoRender->renderFrame(frame);
    }
  }
  onRenderOut();
}

void WindowRender::setFPS(double fps_) {
  if (fps_ <= 0) {
    fps_ = 40;
  }
  fps = fps_;
  srcFps = fps;
}

void WindowRender::setSpeed(double speed_) {
  speed = std::max(0.1, speed_);
  LOGFLF(LogLevel::info, "speed:", speed);
}

void WindowRender::setSyncResult(SyncResult result) { syncResult = result; }

void WindowRender::start() {
  {
    std::lock_guard<std::mutex> lck(mtx);
    if (window) {
      window->setObserver(this);
    }
  }
  startTask();
}

void WindowRender::stop() {
  stopTask();
  {
    std::lock_guard<std::mutex> lck(mtx);
    if (window) {
      window->removeObserver(this);
    }
  }
}

void WindowRender::setFrameSource(FrameSourceOb* source) {
  std::lock_guard<std::mutex> lck(mtx);
  frameSource = source;
}

void WindowRender::pause(bool bPause) {
  if (bPause) {
    pauseTask();
  } else {
    resumeTask();
  }
  LOGFLF(LogLevel::info, "pause:", bPause);
}

void WindowRender::onRunTask() {
  // 窗口渲染线程，把videoRender的结果渲染到窗口
  // 无窗口也是要渲染的
  // 下帧应该渲染的绝对时间(tick)
  int64_t nextFrameTime = timeTick();
  while (running()) {
    if (pauseing()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      // 恢复时重置基准
      nextFrameTime = timeTick();
      // 暂停也能截图
      if (bVulkan && vkVideoRender) {
        vkVideoRender->checkShot();
      } else if (pVideoRender) {
        pVideoRender->checkShot();
      }
      continue;
    }
    // 绝对时间控制，使用高精度tick（1tick=100ns, 1ms=10000tick）
    int64_t frameIntervalTick = getFrameTick(fps);
    int64_t now = timeTick();
    int64_t adjustedInterval = (int64_t)(frameIntervalTick / speed);
    // 如果播放速度大于4,一般来说是只有I帧,相对2倍来说,刷新几率肯定还降低了
    if (speed > 4) {
      adjustedInterval = frameIntervalTick;
    }
    {
      // 窗口如果有surface,则由窗口推动拿画面到窗口
      std::unique_lock<std::mutex> lck(mtx);
      if (window && window->getSurface()) {
        // 检查窗口大小是否变化
        bool bUpdate = window->updateSize();
        winWidth = window->getWidth();
        winHeight = window->getHeight();
        if (bUpdate) {
          // 通知观察者窗口大小变化
          dispatch(&ISurfaceRenderOb::onWinSizeChange, winWidth, winHeight);
        }
        // 去VideoTrack取帧数据渲染并调用WindowRender::render渲染
        // 根据同步结果通知观察者修改类似syncResult等参数
        if (frameSource) {
          frameSource->onWinUpdate();
        }
        if (syncResult == SyncResult::none || syncResult == SyncResult::slow) {
          // 窗口是否还有效
          bool bTick = window->preTick();
          if (bTick) {
            // 窗口刷新,调用下面的onRenderWindow
            window->tick();
          }
        }
      } else {
        // 保持消费数据，但不渲染到窗口
        if (frameSource) {
          frameSource->onWinUpdate();
        }
      }
    }
    // 正常速度下,队列频率丢帧需提高窗口刷新率
    if (syncResult == SyncResult::slow && speed == 1.0) {
      // LOGFLF(LogLevel::info, "video render slow");
      dropCounter.record();
      // 统计时间内大于5次
      if (dropCounter.bTrigger() && dropCounter.value() > 3) {
        if (fps < srcFps * 2.0) {
          LOGFLF(LogLevel::info, "video render slow,up fps:", fps, "-",
                 fps * 1.5);
          fps = fps * 1.5;
        }
      }
    }
    // 更新下帧预期时间
    nextFrameTime += adjustedInterval;
    // 计算需要等待的时间（tick -> ms）
    int64_t sleepTick = nextFrameTime - now;
    int64_t sleepMs = sleepTick / 10000;
    // 如果偏差太大（> 2帧），重置基准
    if (sleepTick > adjustedInterval * 2 || sleepTick < 0) {
      nextFrameTime = now;
      sleepTick = 0;
    }
    if (sleepTick > 50000 && syncResult != SyncResult::slow) {
      // tick -> ms
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  }
  if (pVideoRender) {
    pVideoRender->closeResource();
  }
  if (vkVideoRender) {
    vkVideoRender->closeResource();
  }
}

void WindowRender::onRenderWindow() {
  if (pauseing()) {
    return;
  }
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderWindow(window.get());
  }
}

}