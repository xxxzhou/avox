#include "WindowRender.hpp"

#include <algorithm>
#include <chrono>
#ifdef WIN32
#include <windows.h>
#endif

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
#include <TargetConditionals.h>
#include "avox_apple/MetalWindow.hpp"
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

bool WindowRender::canRecordSizeChange() {
  // resize 层只挂在 VK 图里: 原生腿(bVulkan=false)与 HDR 直通/VR 拓扑都没有
  return bVulkan && vkVideoRender != nullptr && vkVideoRender->canSizeChange();
}

void WindowRender::saveSizeState() {
#ifdef AVOX_ENABLE_VULKAN
  if (!vkVideoRender || bSavedSize) {
    return;
  }
  vkVideoRender->saveSizeState(savedUseNew, savedScale, savedW, savedH);
  bSavedSize = true;
#endif
}

void WindowRender::restoreSizeState() {
#ifdef AVOX_ENABLE_VULKAN
  if (!vkVideoRender || !bSavedSize) {
    return;
  }
  vkVideoRender->restoreSizeState(savedUseNew, savedScale, savedW, savedH);
  bSavedSize = false;
#endif
}

void WindowRender::setHdrMode(HdrMode mode) {
  // vkHDR(链F 意图值)按车道归一化, 不能一律折 forceHDR(§4.3):
  // lane=0(VK 腿) 折 forceHDR —— 与 forceHDR 在 VK 腿既有行为一致, 正是链F 要的;
  // lane=1(原生腿) 折 follow —— 宿主意图是「交给 VK 做图像处理」, 原生腿无 VK 处理链,
  // 若按 forceHDR 执行会误开原生直通交换链(Dx11Window.cpp:294)并把 SDR 载荷当 PQ 码
  // 送上 G2084 面 = 直接发灰。属宿主路由错误, 降回 follow 走 SDR 呈现并告警
  HdrMode applied = mode;
  if (mode == HdrMode::vkHDR) {
    if (bVulkan) {
      applied = HdrMode::forceHDR;
    } else {
      applied = HdrMode::follow;
      LOGFLF(LogLevel::warn,
             "vkHDR on native lane(no VK process chain), downgrade follow");
    }
  }
  // 直通先于 shader 模式: 呈现面切不上(屏/格式不可得)时降级 follow,
  // 防 shader 跳过 tone map 落在 SDR 表面(过曝)。SDR 显示器 no-op
  if (window) {
    const bool ok = window->setHdrPassthrough(applied == HdrMode::forceHDR);
    if (!ok && applied == HdrMode::forceHDR) {
      applied = HdrMode::follow;
      LOGFLF(LogLevel::warn, "hdr passthrough refused, downgrade follow");
    }
  }
  SurfaceRenderNative::setHdrMode(applied);
}

void WindowRender::setHdrMeta(const HdrMeta &meta) {
  SurfaceRenderNative::setHdrMeta(meta);
  if (window) {
    window->setHdrMeta(meta);
  }
}

void WindowRender::onSurfaceChange() {
  std::lock_guard<std::mutex> lck(mtx);
  if (bOffSurface) {
    if (window) {
      // 析构前先清渲染器的 targetWindow 裸指针: 渲染线程每帧检查点解引用旧窗口即UAF
      if (pVideoRender) {
        pVideoRender->renderWindow(nullptr);
      }
      if (vkVideoRender) {
        vkVideoRender->renderWindow(nullptr);
      }
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
    // 只在当前帧是硬解GPU帧时才把原生RT上下文交给vulkan(NV12已在此转RGBA)。
    // CPU帧由vulkan自行上传平面; 而track跨媒体复用, getGpuContext()在换源后
    // 仍是上一场硬解遗留的旧RT, 无条件喂入会把新场输入槽格式翻成旧尺寸/格式,
    // 管线协商失败整场黑屏(2026-09-21 panvox 硬解→软解切换实证)
    IRenderContext* gpuContext =
        (pVideoRender && frame.buffer->getBufferType() != VBufferType::cpu)
            ? pVideoRender->getGpuContext()
            : nullptr;
    vkVideoRender->renderFrame(frame, gpuContext);
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
#ifdef WIN32
      // CPU 帧腿(G9): 原生腿就地吃软解帧, 共享纹理经 CS 出图后须下发窗口,
      // 否则 Dx11Window::onTickWin 因 sharedTexture 为空直接早退不上屏
      if (!bVulkan && window) {
        window->renderContext(pVideoRender->getGpuContext());
      }
#endif
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
#ifdef WIN32
// Windows 默认定时器粒度 15.6ms, 1ms 粒度下 sleep 才不发散
  timeBeginPeriod(1);
#endif
  // 唤醒节拍由视频帧自己的到期时刻决定: onWinUpdate 返回距下一帧还有多少毫秒。
  // 不再维护 fps 网格, 也不做 *1.5 升频 —— 早醒只补睡余量, 不会整拍跳过
  while (running()) {
    // TEMP 取证探针(定案后删): 5 秒一行, 看渲染线程自己是「被睡过头」还是「没被唤醒」
    static int64_t wStatMs = 0, wMaxWork = 0, wMaxSleep = 0, wMaxLate = 0;
    static int32_t wIter = 0, wLate100 = 0, wTick = 0, wNoTick = 0;
    const int64_t wT0 = timeStampMS();
    if (!wStatMs) wStatMs = wT0;
    if (wT0 - wStatMs >= 5000) {
      LOGFLF(LogLevel::info, "[wstat] iter:", wIter, " tick:", wTick,
          " notick:", wNoTick, " workmax:", (int32_t)wMaxWork,
          " sleepmax:", (int32_t)wMaxSleep, " latemax:", (int32_t)wMaxLate,
          " late>100:", wLate100);
      wStatMs = wT0;
      wIter = wTick = wNoTick = wLate100 = 0;
      wMaxWork = wMaxSleep = wMaxLate = 0;
    }
    if (pauseing()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      // 暂停也能截图
      if (bVulkan && vkVideoRender) {
        vkVideoRender->checkShot();
      } else if (pVideoRender) {
        pVideoRender->checkShot();
      }
      continue;
    }
    int64_t dueMs = 0;
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
        if (frameSource) {
          dueMs = frameSource->onWinUpdate();
        }
        if (syncResult == SyncResult::none || syncResult == SyncResult::slow) {
          // 窗口是否还有效
          bool bTick = window->preTick();
          if (bTick) {
            window->tick();
            wTick++;
          } else {
            wNoTick++;
          }
        }
      } else {
        // 保持消费数据，但不渲染到窗口
        if (frameSource) {
          dueMs = frameSource->onWinUpdate();
        }
      }
    }
    wIter++;
    {
      const int64_t wWork = timeStampMS() - wT0;
      if (wWork > wMaxWork) wMaxWork = wWork;
    }
    // 到期时刻是绝对值: 每拍从它重算余量, 毫秒取整与渲染耗时不会累积成漂移
    int64_t sleepMs = dueMs > 0 ? dueMs - timeStampMS() : 10;
    if (sleepMs < 0) {
      sleepMs = 0;
    }
    if (sleepMs > wMaxSleep) wMaxSleep = sleepMs;
    const int64_t wSlp0 = timeStampMS();
#ifdef WIN32
    // 尾段自旋保留: 1ms 粒度残余会让提交相位在 vsync 临界点乱闪
    const auto wakeAt =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(sleepMs);
    if (sleepMs > 2) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs - 2));
    }
    while (std::chrono::steady_clock::now() < wakeAt) {
      std::this_thread::yield();
    }
#else
    // 移动端不自旋(钉核发热): 晚到 1ms 无感, 早到由下一拍余量补齐
    if (sleepMs > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    } else {
      std::this_thread::yield();
    }
#endif
    // 睡过头 = 线程没被及时调度(CPU 抢占/温度保护), 与"自己算错到期时刻"区分开
    {
      const int64_t late = timeStampMS() - wSlp0 - sleepMs;
      if (late > wMaxLate) wMaxLate = late;
      if (late > 100) wLate100++;
    }
  }
  if (pVideoRender) {
    pVideoRender->closeResource();
  }
  if (vkVideoRender) {
    vkVideoRender->closeResource();
  }
#ifdef WIN32
  timeEndPeriod(1);
#endif
}

void WindowRender::onRenderWindow() {
  if (pauseing()) {
    return;
  }
// 两路都下发窗口指针: 派生据此每帧比对 hdrPassthroughActive() 实态,
// 翻转时置 bResetFlag 重建输出端(§4.2 统一检查点)。window 可能为空
  if (pVideoRender) {
    pVideoRender->renderWindow(window.get());
  }
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderWindow(window.get());
  }
}

}
