#pragma once

#include "SurfaceRenderNative.hpp"

namespace avox {

// 前向声明
class FontRender;

// 由WindowRender更新去拿取帧数据
class FrameSourceOb {
 public:
  virtual ~FrameSourceOb() {}

 public:
  // 窗口如何刷新
  virtual void onWinUpdate() {};
};

// 同步结果,0 正常,1 慢了,2 快了,3 没有数据
#define AVOX_MAP_SYNC_RESULT(XX) \
  XX(none, 0, "none")           \
  XX(slow, 1, "slow")           \
  XX(quick, 2, "quick")         \
  XX(nodata, 3, "nodata")

enum class SyncResult {
#define XX(name, value, str) name = value,
  AVOX_MAP_SYNC_RESULT(XX)
#undef XX
};

const char* getSyncResultStr(SyncResult syncResult);

// 窗口渲染 - 继承 SurfaceRenderNative，覆盖需要 window 的方法
// 管理多平台窗口与处理渲染的关系
// VideoRender不同RenderType不同渲染
// VideoFrame到具体的VideoRender的渲染分支不同
// 比如CPUFrame肯定是Vk来渲染
// egl/metal是先由eglrender/metalrender先把YUV转RGBA8
// 然后根据选择窗口来决定是直接给窗口还是转Vk图像处理
// 以及窗口的Tick刷新，以及没有窗口时的处理
class AVOX_EXPORT WindowRender : public SurfaceRenderNative,
                                public RunTask,
                                public IWindowOb {
 public:
  WindowRender();
  virtual ~WindowRender();

 protected:
  std::unique_ptr<Window> window = nullptr;
  // 窗口多线程同步操作
  std::mutex mtx;
  // 可以设置数据源,如果有,则在更新时尝试去拿取数据
  FrameSourceOb* frameSource = nullptr;
  // 如果一秒内丢帧超过5次,自动提升刷新频率
  StateCounter dropCounter = {1000};
  // 如果过快,降低刷新频率
  StateCounter quickCounter = {1000};

  // 暂停渲染
  // 如果是本地文件,直接暂停无事
  // 如果是直播,暂停渲染,IO不暂停
  // 最后导致IO队列堵塞,Socket会超时关闭
  // bool bPause = false;
  // 默认
  double fps = 40.0;
  double srcFps = 40.0;
  double speed = 1.0;
  int32_t syncType = 0;
  SyncResult syncResult = SyncResult::none;
  //
  int32_t winWidth = 0;
  int32_t winHeight = 0;

 public:
  // 覆盖 setVulkan已有窗口时不能切换
  virtual void setVulkan(bool bVulkan) override;
  virtual void* getSurface() override;
  //
  virtual void onSurfaceChange() override;
  virtual void render(const VideoFrame& frame) override;
  virtual void render(const GpuFrame& frame) override;
  virtual void render(const YUVFrame& frame) override;
  virtual void setAutoAspect(bool bEnable) override;

 public:
  void setFrameSource(FrameSourceOb* source);
  void setFPS(double fps);
  double getFPS() const { return fps; }
  void setSpeed(double speed);
  double getSpeed() const { return speed; }
  void setSyncResult(SyncResult syncResult);
  void start();
  void stop();
  void pause(bool bPause);

  // RunTask
 protected:
  virtual void onRunTask() override;

  // IWindowOb
 public:
  virtual void onRenderWindow() override;
};

}