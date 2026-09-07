#pragma once

#include <Windows.Graphics.Capture.Interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.System.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "../../avox/module/RunTask.hpp"
#include "../../avox/source/VideoSource.hpp"
#include "../WinCommon.hpp"
#include "../dx11/Dx11SharedTex.hpp"

namespace winrt {
using namespace Windows::Foundation;
using namespace Windows::Graphics;
using namespace Windows::Graphics::Capture;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;
}  // namespace winrt

namespace avox {

// Windows 平台窗口捕获基类:
// 两个抓取器(CaptureWindows/RTCaptureWindows)的公共字段与 setWindow。
// device/context(MComPtr) 有意与基类 Dx11Context 的裸指针同名隐藏,
// 自管生命周期, 行为同原实现。
class WinCaptureBase : public VideoSource, public Dx11Context {
 public:
  WinCaptureBase() = default;
  virtual ~WinCaptureBase() = default;

 protected:
  HWND hwnd = nullptr;
  HMONITOR hmon = nullptr;  // 显示器目标(与 hwnd 互斥)
  MComPtr<ID3D11Device> device = nullptr;
  MComPtr<ID3D11DeviceContext> context = nullptr;
  std::unique_ptr<Dx11SharedTex> outSharedTex = nullptr;
  ImageFormat frameFormat = {};
  std::string windowClass;   // Win32 窗口类名 (-l 诊断: UWP frame/content 区分)
  std::string processName;   // 拥有窗口的进程 exe (-l 诊断用)

 public:
  void setWindow(HWND hwnd_);
  void setMonitor(HMONITOR hmon_, int32_t index);
  // 窗口句柄 (供 ops 做坐标转换/屏幕定位; 显示器目标返回 nullptr)
  HWND nativeHwnd() const { return hwnd; }
  // 显示器句柄 (与 hwnd 互斥; 窗口目标返回 nullptr) — 供 ops 取 monitor 原点回填 WindowShotInfo
  HMONITOR nativeHmon() const { return hmon; }
  const std::string& nativeClass() const { return windowClass; }
  const std::string& nativeProcess() const { return processName; }
};

// GDI 抓帧(GetWindowDC+BitBlt): 普通窗口可用; 硬件加速/DWM 合成窗口抓不到,
// 仅作回退。
class CaptureWindows : public WinCaptureBase, public RunTask {
 public:
  CaptureWindows() = default;
  // 基类 ~RunTask 才 join(晚于成员销毁), 本级先停线程
  virtual ~CaptureWindows() { stopTask(); }

 protected:
  virtual void onRunTask() override;
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
};

// WinRT Graphics Capture: 能抓硬件加速窗口(Chromium/UWP 等), 主路径。
// 与 CaptureWindows 同样用 RunTask 独立线程, 在 onRunTask 里按 fps 轮询
// framePool.TryGetNextFrame(), 不依赖 FrameArrived 回调。framePool 的
// 创建/TryGetNextFrame/Recreate 全在该线程, 既回避跨线程 apartment 问题,
// 也回避回调内同步 Recreate 让帧池停止派发的禁忌。
class RTCaptureWindows : public WinCaptureBase, public RunTask {
 public:
  RTCaptureWindows();
  virtual ~RTCaptureWindows();

 private:
  winrt::IDirect3DDevice rtDevice{nullptr};
  winrt::Direct3D11CaptureFramePool framePool{nullptr};
  winrt::GraphicsCaptureSession session{nullptr};

 protected:
  virtual void onRunTask() override;
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
};

// 运行时探测 Windows Graphics Capture 是否可用(Win10 1903+,
// UniversalApiContract 7)
bool isWinRtCaptureAvailable();

// 非传统设备管理: 枚举窗口列表, 每个窗口按能力包装成 RT 或 GDI 抓取器
class CaptureWindowsMgr : public VideoManager<WinCaptureBase> {
 public:
  CaptureWindowsMgr();
  virtual ~CaptureWindowsMgr() = default;

 private:
  bool bRT = false;

 protected:
  virtual void onRefreshDevices() override;

 public:
  void addWindow(HWND hwnd);
  void addMonitor(HMONITOR hmon, int32_t index);
};

}
