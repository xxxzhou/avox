// 采集会话探针: 录主显示器 N 秒(60fps/无光标/区域裁剪) + 双音轨分轨落盘。
// 用法: capturetest [输出目录] [录制秒数] [h264/h265] [WxH] [mode]
//   mode: 缺省=单次录制(中途 pause/resume); loop=重复 start/stop 20 次;
//         multi=主显示器 + 第一个窗口设备双视频腿
// 产物在 <dir>/screen.mkv + audio/*.wav + session.json
#include <chrono>
#include <string>
#include <thread>

#include "avox/AvoxCapture.h"
#include "avox/Avox.hpp"
#include "avox/module/LogHelper.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

using namespace avox;

namespace {

#ifdef _WIN32
struct MonMatch {
  const char* id = nullptr;
  RECT rc = {};
  bool found = false;
};

BOOL CALLBACK monProc(HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
  MonMatch* m = (MonMatch*)lparam;
  MONITORINFOEXW mi = {};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(hmon, &mi)) {
    char dev[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, dev, sizeof(dev), nullptr,
                        nullptr);
    if (m->id && strcmp(dev, m->id) == 0) {
      m->rc = mi.rcMonitor;
      m->found = true;
      return FALSE;
    }
  }
  return TRUE;
}
#endif

class SessionOb : public ICaptureSessionOb {
 public:
  void onCaptureState(CaptureState pre, CaptureState cur) override {
    LOGFLF(LogLevel::info, "[ob] state:", getCaptureStateStr(pre), "->",
           getCaptureStateStr(cur));
  }
  void onCaptureError(const char* id, AVError err, const char* msg) override {
    LOGFLF(LogLevel::error, "[ob] error [", id ? id : "?", "]:", (int32_t)err,
           " ", msg ? msg : "");
  }
  void onCaptureStats(const CaptureStats& stats) override {
    LOGFLF(LogLevel::info, "[ob] fps:", stats.achievedFps,
           " frames:", stats.pushedFrames);
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  const char* outDir = argc > 1 ? argv[1] : "D:/tmp/capturetest";
  int32_t seconds = argc > 2 ? atoi(argv[2]) : 8;
  // 编码器: h264(缺省, 兼容性最好) / h265(需系统 HEVC 扩展)
  VCodecId codec = VCodecId::h264;
  if (argc > 3 && std::string(argv[3]) == "h265") {
    codec = VCodecId::h265;
  }
  std::string mode = argc > 5 ? argv[5] : "";
#ifdef _WIN32
  // 区域坐标是虚拟屏物理像素: 测试进程必须 PMv2(与产品 runner 的 manifest 一致),
  // 否则 GetMonitorInfo 返回 DPI 虚拟化后的逻辑矩形
  using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  auto setCtx =
      (SetCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                               "SetProcessDpiAwarenessContext");
  if (setCtx) {
    setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
#endif

  fprintf(stderr, "[probe] start\n");
  ICaptureSession* session = createCaptureSession();
  if (!session) {
    LOGFLF(LogLevel::error, "createCaptureSession failed");
    return 1;
  }
  SessionOb ob;
  session->setObserver(&ob);

  // 主显示器(与设备 id 匹配取矩形)
#if defined(_WIN32)
  IVideoManager* videoMgr = getVideoManager(VDeviceSdk::win_capture);
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::wasapi);
#elif defined(__APPLE__)
  IVideoManager* videoMgr = getVideoManager(VDeviceSdk::mac_capture);
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::ios);
#else
  LOGFLF(LogLevel::error, "capture not supported on this platform");
  return 1;
#endif
  if (!videoMgr || videoMgr->getDeviceCount() == 0) {
    LOGFLF(LogLevel::error, "no capture device");
    return 1;
  }
  fprintf(stderr, "[probe] mgrs: video=%p count=%d audio=%p\n", (void*)videoMgr,
          videoMgr ? videoMgr->getDeviceCount() : -1, (void*)audioMgr);
  IVideoSource* monitor = nullptr;
  IVideoSource* window = nullptr;
  for (int32_t i = 0; i < videoMgr->getDeviceCount(); i++) {
    IVideoSource* dev = videoMgr->getDevice(i);
    LOGFLF(LogLevel::info, "video device[", i, "]:", dev->getDeviceName(),
           " id:", dev->getDeviceId(), " kind:", (int32_t)dev->getDeviceKind());
    if (!monitor && dev->getDeviceKind() == VDeviceKind::monitor) {
      monitor = dev;
    }
    if (!window && dev->getDeviceKind() == VDeviceKind::window) {
      window = dev;
    }
  }
  if (!monitor) {
    LOGFLF(LogLevel::error, "no monitor device");
    return 1;
  }
  CaptureConfig cfg = {};
  cfg.video.fps = 60.0;
  cfg.video.captureCursor = false;
  cfg.video.border = false;
#if defined(_WIN32)
  MonMatch match;
  match.id = monitor->getDeviceId();
  ::EnumDisplayMonitors(NULL, NULL, monProc, (LPARAM)&match);
  if (!match.found) {
    LOGFLF(LogLevel::error, "monitor rect not found:", match.id);
    return 1;
  }
  cfg.video.regionX = match.rc.left;
  cfg.video.regionY = match.rc.top;
  cfg.video.regionW = match.rc.right - match.rc.left;
  cfg.video.regionH = match.rc.bottom - match.rc.top;
#elif defined(__APPLE__)
  // 区域=虚拟屏绝对物理像素: CGDisplayBounds(点)×本屏缩放
  {
    CGDirectDisplayID dispId = (CGDirectDisplayID)atoi(monitor->getDeviceId() + 7);
    CGRect bounds = CGDisplayBounds(dispId);
    double scale = bounds.size.width > 0
                       ? (double)CGDisplayPixelsWide(dispId) / bounds.size.width
                       : 1.0;
    cfg.video.regionX = (int32_t)(bounds.origin.x * scale);
    cfg.video.regionY = (int32_t)(bounds.origin.y * scale);
    cfg.video.regionW = (int32_t)(bounds.size.width * scale);
    cfg.video.regionH = (int32_t)(bounds.size.height * scale);
  }
#else
  cfg.video.regionW = 1920;
  cfg.video.regionH = 1080;
#endif
  cfg.videoCodec = codec;
  if (mode == "cursor") {
    // 光标入画验证模式: 开系统光标烧进画面(对照跑=关光标), 区域中心钉住光标
    cfg.video.captureCursor = true;
  }
#if defined(_WIN32)
  // argv[4] "WxH": 子矩形裁剪(显示器原点起), 验证 srcBox 路径; 缺省 = 整屏
  if (argc > 4) {
    int32_t rw = 0;
    int32_t rh = 0;
    if (sscanf(argv[4], "%dx%d", &rw, &rh) == 2 && rw > 1 && rh > 1) {
      cfg.video.regionX = match.rc.left;
      cfg.video.regionY = match.rc.top;
      cfg.video.regionW = rw;
      cfg.video.regionH = rh;
    }
  }
#elif defined(__APPLE__)
  if (argc > 4) {
    int32_t rw = 0;
    int32_t rh = 0;
    if (sscanf(argv[4], "%dx%d", &rw, &rh) == 2 && rw > 1 && rh > 1) {
      // mac: atoi 出来的 regionX/Y 已是本屏原点(单屏=0,0)
      cfg.video.regionW = rw;
      cfg.video.regionH = rh;
    }
  }
#endif
  if (!session->setConfig(cfg)) {
    return 1;
  }

  auto printStats = [&](const char* tag) {
    CaptureStats st = session->getStats();
    LOGFLF(LogLevel::info, "[", tag, "] fps:", st.achievedFps,
           " frames:", st.pushedFrames, " missed:", st.missedTicks,
           " dropSize:", st.droppedBySize, " dropGate:", st.droppedByFpsGate,
           " dropEnc:", st.encoderDropped, " clock:", session->getClockMs(),
           "ms bytes:", st.bytesWritten);
  };

  if (!session->addVideoInput({monitor, "screen", cfg.video})) {
    return 1;
  }
  if (mode == "multi") {
    // 双视频腿: 第二腿=第一个窗口设备(不传参数, 尺寸首帧回报, 懒 ready)
    if (!window) {
      LOGFLF(LogLevel::error, "multi mode: no window device");
      return 1;
    }
    if (!session->addVideoInput({window, "win"})) {
      LOGFLF(LogLevel::error, "multi mode: add window leg failed");
      return 1;
    }
  }
  // 同一源二次添加必须被拒(占用登记)
  if (session->addVideoInput({monitor, "screen2"})) {
    LOGFLF(LogLevel::error, "busy check failed: same source added twice");
    return 1;
  }
  // 音频: 第一路 mic + 第一路 loopback(枚举序: 默认麦/默认回环/其余麦)
  IAudioSource* mic = nullptr;
  IAudioSource* loop = nullptr;
  for (int32_t i = 0; i < audioMgr->getDeviceCount(); i++) {
    IAudioSource* dev = audioMgr->getDevice(i);
    LOGFLF(LogLevel::info, "audio device[", i, "]:", dev->getDeviceName(),
           " kind:", (int32_t)dev->getDeviceKind());
    if (!mic && dev->getDeviceKind() == ADeviceKind::mic) {
      mic = dev;
    }
    if (!loop && dev->getDeviceKind() == ADeviceKind::loopback) {
      loop = dev;
    }
  }
  if (mic) {
    session->addAudioInput({mic, "mic"});
  }
  if (loop) {
    session->addAudioInput({loop, "sys"});
  }

  if (mode == "cursor") {
    // 光标入画验证: 开光标 + 把光标钉在区域中心录 3s, 配合关光标对照跑
#ifdef _WIN32
    SetCursorPos(cfg.video.regionX + cfg.video.regionW / 2,
                 cfg.video.regionY + cfg.video.regionH / 2);
#endif
    if (!session->start(outDir)) {
      LOGFLF(LogLevel::error, "start failed");
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printStats("cursor");
    session->stop();
    LOGFLF(LogLevel::info, "cursor mode done");
    delete session;
    return 0;
  }

  if (mode == "loop") {
    // 重复 start/stop 20 次: 设备占用/observer/源与 muxer 重建的稳定性
    for (int32_t i = 0; i < 20; i++) {
      std::string dir = std::string(outDir) + "/run" + std::to_string(i);
      if (!session->start(dir.c_str())) {
        LOGFLF(LogLevel::error, "loop start failed at", i);
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      printStats("loop");
      session->stop();
      LOGFLF(LogLevel::info, "=== loop round ", i + 1, "/20 done ===");
    }
    LOGFLF(LogLevel::info, "loop mode done: 20 rounds");
    delete session;
    return 0;
  }

  fprintf(stderr, "[probe] calling start...\n");
  bool bStarted = session->start(outDir);
  fprintf(stderr, "[probe] start returned %d\n", (int32_t)bStarted);
  if (!bStarted) {
    LOGFLF(LogLevel::error, "start failed");
    return 1;
  }
  // 录制 + 每秒打点; 中途暂停/恢复各一次验证时钟
  for (int32_t i = 0; i < seconds; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    printStats("t");
    if (i == seconds / 2) {
      LOGFLF(LogLevel::info, "--- pause 1s ---");
      session->pause();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      session->resume();
    }
  }
  CaptureGeometry geo = {};
  session->getCaptureGeometry(&geo);
  LOGFLF(LogLevel::info, "geometry:", geo.x, ",", geo.y, " ", geo.width, "x",
         geo.height);
  session->stop();
  printStats("done");
  delete session;
  return 0;
}
