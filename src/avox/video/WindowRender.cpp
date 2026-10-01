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

void WindowRender::setHdrMode(HdrMode mode) {
  // 直通先于 shader 模式: 呈现面切不上(屏/格式不可得)时降级 follow,
  // 防 shader 跳过 tone map 落在 SDR 表面(过曝)。SDR 显示器 no-op
  HdrMode applied = mode;
  if (window) {
    const bool ok = window->setHdrPassthrough(mode == HdrMode::forceHDR);
    if (!ok && mode == HdrMode::forceHDR) {
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
  // Windows 默认定时器粒度 15.6ms 把下面 sleep_for(帧间隔-2ms) 量化成 3格/2格
  // 交替: 拍长双峰 47/32ms(23.976fps 实测), 每帧停留时长抖动呈持续微抖;
  // 1ms 粒度后拍长锁定内容帧节拍(41.7ms 单峰, 98.6%), 与在线播放器观感对齐
  timeBeginPeriod(1);
#endif
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
          // 上限钳在乘法后: 判据在前会越界(45*1.5=67.5 > srcFps*2)
          double upFps = std::min(srcFps * 2.0, fps * 1.5);
          LOGFLF(LogLevel::info, "video render slow,up fps:", fps, "-",
                 upFps);
          fps = upFps;
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
      // tick -> ms, 提前 2ms 唤醒: 睡眠目标来自帧率标签, 标签可能偏大于真实
      // PTS 步进(23.98 标签实为 24000/1001, 43.5ms vs 41.7ms), 唤醒节拍一旦
      // 低于 PTS 步进, 视频钟线性落后音频钟(0101.mkv 实测 +41ms/s); 提前量
      // 由 syncVideo 的 quick 门吸收, 节拍永不低于内容速率
      std::this_thread::sleep_for(std::chrono::milliseconds(
          sleepMs > 3 ? sleepMs - 2 : 1));
#ifdef WIN32
      // 尾段自旋到下帧预期时刻: 1ms 定时器粒度残余 ±0.7ms 噪声会让提交相位
      // 在 vsync 重排临界点乱闪(60.00Hz 屏播 23.976, 实测 std 706us→目标<200);
      // 自旋段 ≤2ms, PC 空闲核代价可忽略
      {
        int64_t targetUs = nextFrameTime / 10;
        for (;;) {
          int64_t nowUs =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
          if (nowUs >= targetUs) {
            break;
          }
          std::this_thread::yield();
        }
      }
#endif
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    } else {
      // 兜底短睡仅移动端(Android/iOS): 追帧/余量<5ms 全速自旋会在手机钉满
      // 一个大核发热(9/25 真机定案); PC 保原全速行为(快速转码吃吞吐)。
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
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
  // (贴图腿/离屏), 基类判空即安全
  if (pVideoRender) {
    pVideoRender->renderWindow(window.get());
  }
  if (bVulkan && vkVideoRender) {
    vkVideoRender->renderWindow(window.get());
  }
}

}