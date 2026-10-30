#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "avox/module/RunTask.hpp"
#include "avox/source/VideoSource.hpp"

#include <CoreGraphics/CoreGraphics.h>

namespace avox {

// macOS 屏幕采集基类(ScreenCaptureKit, .mm 实现):
// 契约与 Windows WinCaptureBase 对齐 —— CaptureVideoParam(fps CFR/关光标/区域
// 虚拟屏绝对物理像素)、onCaptureInfo 回报、missedTicks; 帧通路与 IOSCameraSource
// 同构(GpuFrame.buffer=CVPixelBufferRef, NV12), 编码/渲染下游零改动
class MacCaptureBase : public VideoSource {
 public:
  MacCaptureBase() = default;
  virtual ~MacCaptureBase() = default;

 public:
  // IVideoSource: 采集策略(open 前设置)。未调用的旧路径=30fps/带光标/整屏
  void setCaptureParam(const CaptureVideoParam& param) override {
    captureParam = param;
    bCaptureParamSet = true;
  }
  // 采集节拍跳过的槽位数(慢帧累计, 会话统计用)
  int64_t getMissedTicks() const { return missedTicks.load(); }
  // 光标实际状态: disabled(已关)/baked(未能关)
  const std::string& getCaptureCursorMode() const { return cursorMode; }
  // mgr 枚举身份(刷新去重/占用 pin 用)
  uint32_t getDisplayId() const { return displayId; }
  void setDisplay(CGDirectDisplayID display, int32_t index);
  // SCK 回调线程入口(ObjC 桥调用): 最新帧暂存(单槽 retain, 旧帧让位)
  void onScreenFrame(void* pixelBuffer);

 protected:
  CGDirectDisplayID displayId = 0;
  int32_t displayIndex = 0;
  CaptureVideoParam captureParam = {};
  bool bCaptureParamSet = false;
  std::atomic<int64_t> missedTicks{0};
  std::string cursorMode = "baked";
  // 帧交接(SCK 回调线程 → 节拍线程): 单槽 pending + 节拍侧 lastEmitted
  std::mutex frameMtx;
  void* pendingBuffer = nullptr;   // CVPixelBufferRef, retained
  void* lastEmitted = nullptr;     // CVPixelBufferRef, retained(补帧重发)
};

// SCK 采集器: onOpen 建 SCStream(信号量同步), RunTask 线程按 CFR 绝对到期
// 节拍出帧(pts=排期网格, 慢帧跳拍), 屏幕静止时重发最后一帧(不拷贝)
class MacCapture : public MacCaptureBase, public RunTask {
 public:
  MacCapture() = default;
  virtual ~MacCapture() { stopTask(); }

 protected:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override { return running(); }
  virtual void onRunTask() override;

 private:
  friend class MacStreamOutputBridge;
  void* stream = nullptr;       // SCStream *
  void* streamOutput = nullptr; // MacStreamOutputBridge *
  void* outputQueue = nullptr;  // dispatch_queue_t
  // 首帧尺寸(显示设置变化检测, 同 Windows 契约)
  bool bContentInit = false;
  int32_t contentW = 0;
  int32_t contentH = 0;
};

// 设备管理器: 枚举在线显示器; 刷新清空重建+占用源 pin(与 CaptureWindowsMgr 同构)
class MacCaptureMgr : public VideoManager<MacCaptureBase> {
 public:
  MacCaptureMgr();
  virtual ~MacCaptureMgr() = default;

 protected:
  virtual void onRefreshDevices() override;
};

void regMacCaptureDevice();

}
