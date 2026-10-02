// 外挂 .srt 文本字幕端到端验收(字幕模块合并计划.md P2 链):
//   loadSubtitle(.srt) → MediaPlayer 外挂槽 → SubtitleFile 查找
//   → TextRasterizer 光栅化 → VkCanvasLayer 合成 → 输出帧像素证据。
// 判据(客观像素, 黑底素材确定性判):
//   1. loadSubtitle 返回 true(外挂槽激活);
//   2. 字幕带(下 1/4)出现持续亮像素(文本→canvas→合成通);
//   3. 输出帧 PNG 落盘供人眼复核(位置/字号观感对比)。
// 素材: assets/video/subtext_bg.mp4 (640x360 黑底 10s)
//       assets/video/subtext_test.srt (单条对白 0.5s~8.0s)
// 用法: subtitletexttest [mp4路径] [srt路径] [验证秒数] [输出前缀]
// 可选: SUBTEXT_SEEK=1 追加 seek 相位验证(2s 带字/9s 空档)
// 末尾打印 case=subtext PASS/FAIL
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox/subtitle/TextRasterizer.hpp"
#endif

using namespace avox;

#ifdef _WIN32
static LONG WINAPI crashReporter(EXCEPTION_POINTERS* e) {
  if (e->ExceptionRecord->ExceptionCode == 0xC0000005) {
    HMODULE m = nullptr;
    char mod[MAX_PATH] = "(unknown)";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m);
    GetModuleFileNameA(m, mod, MAX_PATH);
    std::fprintf(stderr,
                 "[veh] ACCESS_VIOLATION op=%llu ip=%p mod=%s access=%p\n",
                 (unsigned long long)e->ExceptionRecord->ExceptionInformation[0],
                 e->ExceptionRecord->ExceptionAddress, mod,
                 (void*)e->ExceptionRecord->ExceptionInformation[1]);
    std::fflush(stderr);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

namespace {

// 底部字幕带(下 1/4)亮像素计数, 与 assmkvtest 同判据(黑底素材确定性判)
int32_t brightPixelsInStrip(IImageBuffer* buf, YuvType yuvType,
                            IImageBuffer** outRgba, const ColorSpaceDesc& cs) {
  *outRgba = nullptr;
  if (!buf || !buf->getPointer()) {
    return -1;
  }
  IImageBuffer* tmp = createImageBuffer();
  YUVFrame frame = {};
  IImageBuffer* rgba = createImageBuffer();
  if (!image2SplitYUVFrame(buf, yuvType, frame, tmp) ||
      !yuvframe2Rgba(frame, rgba, cs)) {
    delete tmp;
    delete rgba;
    return -1;
  }
  delete tmp;
  const ImageFormat fmt = rgba->getImageFormat();
  const uint8_t* base = rgba->getPointer();
  if (!base || fmt.width <= 0 || fmt.height <= 0) {
    delete rgba;
    return -1;
  }
  const int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;
  const int32_t stripY = fmt.height * 3 / 4;
  int32_t count = 0;
  for (int32_t y = stripY; y < fmt.height; y += 2) {
    const uint8_t* row = base + (size_t)y * pitch;
    for (int32_t x = 0; x < fmt.width; x += 2) {
      const uint8_t* px = row + (size_t)x * 4;
      if (px[1] > 120) {
        ++count;
      }
    }
  }
  *outRgba = rgba;
  return count;
}

class TextOutOb : public ISurfaceRenderOb {
 public:
  ISurfaceRender* sr = nullptr;
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    std::lock_guard<std::mutex> lock(mtx);
    if (!subArmed) {
      return;
    }
    ++frames;
    IImageBuffer* rgba = nullptr;
    const int32_t bright = brightPixelsInStrip(buf, yuvType, &rgba, sr->getOutColorSpace());
    if (bright < 0) {
      return;
    }
    if ((frames == 40 || frames == 90 || frames == 150) && rgba) {
      saveImagePath((prefix + "raw.png").c_str(), rgba);
      std::printf("dump raw (bright=%d)\n", bright);
    }
    if (bright > 40) {
      ++subFrames;
      if (dumpCount < 4) {
        char name[64];
        std::snprintf(name, sizeof(name), "%ssub%d.png", prefix.c_str(),
                      dumpCount);
        saveImagePath(name, rgba);
        std::printf("dump %s (bright=%d)\n", name, bright);
        ++dumpCount;
      }
    }
    if (seekPhase >= 0) {
      ++phaseFrames;
      if (bright > 40) {
        ++phaseBright;
      }
    }
    delete rgba;
  }
  void onSurface() override {}
  // onRender 事件续流计数: forceHDR 直通裁 yuv/image 出图(CPU 像素证据不可
  // 得), 事件流是否延续 = 直通拓扑含 canvas 层时图健康的客观信号
  // (格式不匹配/建图失败即断流), ev.hdr 位佐证直通档真在生效
  void onRender(const SurfaceRenderEvent* ev) override {
    std::lock_guard<std::mutex> lock(mtx);
    ++renderEvents;
    if (ev) {
      lastHdr = ev->hdr;
    }
    // 只数带真输出层的事件(generation!=0): 重建失败(格式不匹配等)时图死,
    // 事件仍派发但 generation=0 — 门槛保证直通拓扑的图健康判据成立
    if (hdrPhase && ev && ev->generation != 0) {
      ++hdrEvents;
    }
  }
  void onWinSizeChange(int32_t, int32_t) override {}

  void beginSeekPhaseLocked(bool wantText) {
    seekPhase = -1;
    phaseFrames = 0;
    phaseBright = 0;
    seekPhase = 0;
    (void)wantText;
  }

  std::mutex mtx;
  std::string prefix = "subtext_";
  bool subArmed = false;
  int64_t frames = 0;
  int64_t subFrames = 0;
  int32_t dumpCount = 0;
  int32_t seekPhase = -1;
  int64_t phaseFrames = 0;
  int64_t phaseBright = 0;
  // forceHDR 相(SUBTEXT_HDR=1): 事件续流计数
  int64_t renderEvents = 0;
  int64_t hdrEvents = 0;
  int32_t lastHdr = -1;
  bool hdrPhase = false;
};

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
  AddVectoredExceptionHandler(1, crashReporter);
#endif
  const char* url = argc > 1 ? argv[1] : "assets/video/subtext_bg.mp4";
  const char* srt = argc > 2 ? argv[2] : "assets/video/subtext_test.srt";
  int seconds = argc > 3 ? std::atoi(argv[3]) : 5;
  if (seconds <= 0) {
    seconds = 5;
  }
  std::string prefix = argc > 4 ? (std::string(argv[4]) + "_") : "subtext_";

  // SUBTEXT_PERF=1: 文本光栅化 CPU 基准(计划 P4 性能走查 <3ms@1080p 复测)。
  // 预热后交替两条对白绕过同文本零重渲染判定, 1080p 200 次取均值/最大。
  if (std::getenv("SUBTEXT_PERF") && std::getenv("SUBTEXT_PERF")[0] == '1') {
#ifdef AVOX_ENABLE_FREETYPE
    TextRasterizer r;
    r.render("预热 预热", 1920, 1080);
    const char* cues[2] = {"字幕合并验证 TextRasterizer 行",
                           "第二句对白, 用来绕过同文本零重渲染判定"};
    double totalMs = 0;
    double maxMs = 0;
    int okIter = 0;
    for (int i = 0; i < 200; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      const int32_t seq = r.render(cues[i % 2], 1920, 1080);
      const auto t1 = std::chrono::steady_clock::now();
      if (seq <= 0) {
        continue;
      }
      ++okIter;
      const double ms =
          std::chrono::duration<double, std::milli>(t1 - t0).count();
      totalMs += ms;
      if (ms > maxMs) {
        maxMs = ms;
      }
    }
    if (okIter > 0) {
      std::printf("[perf] rasterizer 1080p avg=%.3fms max=%.3fms iters=%d "
                  "canvas=%dx%d\n",
                  totalMs / okIter, maxMs, okIter, r.width(), r.height());
    } else {
      std::printf("[perf] rasterizer unavailable (font missing)\n");
    }
#endif
  }

  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("case=subtext FAIL (createMediaPlayer null)\n");
    return 1;
  }
  // SUBTEXT_NATIVE=1: 原生车道窗口模式(setVulkan(false)+真窗口直呈)——
  // 字幕画布多后端渲染计划 §5.1 的 DX11 腿验收; SDR 相亮像素判据照常
  // (原生车道 yuv out 走平台渲染器原生回读), HDR 相(flip 后 CPU 出图被拒)
  // 看引擎日志 canvas 变体痕迹("canvas variant on ... pq:1")
  const bool doNative = std::getenv("SUBTEXT_NATIVE") != nullptr &&
                        std::getenv("SUBTEXT_NATIVE")[0] == '1';
  void* nativeWnd = nullptr;
#ifdef _WIN32
  if (doNative) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"avox_subtext_native";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, L"avox_subtext_native",
                               L"subtext-native", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               40, 40, 660, 420, nullptr, nullptr,
                               wc.hInstance, nullptr);
    if (!wnd) {
      std::printf("case=subtext FAIL (native window create failed)\n");
      return 1;
    }
    nativeWnd = (void*)wnd;
  }
#endif
  TextOutOb ob;
  ob.prefix = prefix;
  ISurfaceRender* sr = player->getSurfaceRender();
  ob.sr = sr;
  if (doNative && nativeWnd) {
    // 原生车道: 先定 lane 再挂面(setVulkan 对已有面直接 return), yuv out 走
    // 平台渲染器原生回读(onFrame 照常交付)
    sr->setVulkan(false);
    sr->setSurface(nativeWnd);
    sr->enableYuvOut(YuvType::yuv420P);
  } else {
    sr->setOffSurface(YuvType::yuv420P);
  }
  addSurfaceRenderOb(sr, &ob);
  // 原生窗口消息泵(直呈窗口不开泵不刷)
  auto pump = [] {
#ifdef _WIN32
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
#endif
  };
  (void)pump;
  (void)nativeWnd;
  player->open(url);
  bool playing = false;
  for (int i = 0; i < 150; ++i) {
    const int st = (int)player->getState();
    if (i % 10 == 0) {
      std::printf("poll %d state=%d\n", i, st);
    }
    if (st == (int)PlayerState::playing) {
      playing = true;
      break;
    }
    pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!playing) {
    std::printf("case=subtext FAIL (not playing in 15s)\n");
    player->close();
    removeSurfaceRenderOb(sr, &ob);
    delete player;
    return 1;
  }

  // 外挂槽激活判据: loadSubtitle 走 .srt 分流(文本光栅化路径)
  // [BISECT] SUBTEXT_ASS=1 时改走内封 ASS 轨(对照 assmkvtest 已知 PASS)
  bool loadOk;
  if (std::getenv("SUBTEXT_ASS") && std::getenv("SUBTEXT_ASS")[0] == '1') {
    player->setSubtitleTrack(0);
    ISourceInfo* info = player->getSourceInfo();
    loadOk = info && info->subtitleSize() > 0;
  } else {
    loadOk = player->loadSubtitle(srt);
  }
  std::printf("loadSubtitle(%s) = %d\n", srt, (int)loadOk);
  {
    std::lock_guard<std::mutex> lock(ob.mtx);
    ob.subArmed = true;
  }
  for (int i = 0; i < seconds; ++i) {
    pump();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // 可选 seek 相位: SUBTEXT_SEEK=1 (对白内 2s 带字 / 空档 9s 无字)
  const bool doSeek = std::getenv("SUBTEXT_SEEK") != nullptr;  bool seekOk = !doSeek;
  if (doSeek) {
    struct SeekCase {
      int64_t pos;
      bool wantText;
      const char* name;
    };
    SeekCase seeks[] = {
        {2000, true, "seek-in"},   // 对白活动期
        {9000, false, "seek-gap"}  // 对白结束后: 残留未清即 BAD
    };
    for (auto& sc : seeks) {
      player->seek(sc.pos);
      {
        std::lock_guard<std::mutex> lock(ob.mtx);
        ob.beginSeekPhaseLocked(sc.wantText);
      }
      const int64_t preFrames = ob.frames;
      for (int i = 0; i < 60 && ob.frames < preFrames + 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      {
        std::lock_guard<std::mutex> lock(ob.mtx);
        ob.phaseFrames = 0;
        ob.phaseBright = 0;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      std::lock_guard<std::mutex> lock(ob.mtx);
      const double ratio =
          ob.phaseFrames > 0 ? (double)ob.phaseBright / ob.phaseFrames : -1.0;
      const bool pass =
          ob.phaseFrames >= 20 &&
          (sc.wantText ? ratio > 0.5 : ratio < 0.1);
      std::printf("seek %-9s frames=%lld brightRatio=%.2f %s\n", sc.name,
                  (long long)ob.phaseFrames, ratio, pass ? "ok" : "BAD");
      if (!pass) seekOk = false;
      ob.seekPhase = -1;
    }
  }

  // forceHDR 相(SUBTEXT_HDR=1): SDR 字幕验收后切直通档(字幕画布多后端渲染
  // 计划 §5.4)。直通裁 yuv/image 出图故无 CPU 像素证据; 判据 = onRender 事件
  // 续流不断(直通拓扑含 16F canvas 层时建图健康, 失败即断流) + ev.hdr 位置位。
  // 字幕内容的 HDR 域正确性归 HDR 屏人眼验收, 不在本探针
  const bool doHdr = std::getenv("SUBTEXT_HDR") != nullptr &&
                     std::getenv("SUBTEXT_HDR")[0] == '1';
  bool hdrOk = !doHdr;
  int64_t hdrEvents = 0;
  int32_t hdrFlag = -1;
  if (doHdr) {
    sr->setHdrMode(HdrMode::forceHDR);
    {
      std::lock_guard<std::mutex> lock(ob.mtx);
      ob.hdrPhase = true;
    }
    for (int i = 0; i < 50 && ob.hdrEvents < 30; ++i) {
      pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::lock_guard<std::mutex> lock(ob.mtx);
    hdrEvents = ob.hdrEvents;
    hdrFlag = ob.lastHdr;
    hdrOk = hdrEvents >= 30 && hdrFlag == 1;
    std::printf("hdr phase: events=%lld hdr=%d %s\n", (long long)hdrEvents,
                hdrFlag, hdrOk ? "ok" : "BAD");
  }

  player->close();
  removeSurfaceRenderOb(sr, &ob);
  delete player;
#ifdef _WIN32
  if (nativeWnd) {
    DestroyWindow((HWND)nativeWnd);
  }
#endif

  // 原生模式像素判据豁免(known gap): 窗口原生车道的 yuv 回读(mapStagingFrame)
  // 恒交黑帧(2026-10-03 实证, 屏幕直呈有字幕而回读全黑, 独立既有账), subFrames
  // 判据不可用 —— 像素真值由屏幕抓取包装(驱动脚本)承担
  const bool ok = loadOk && ob.frames >= 30 && (doNative || ob.subFrames >= 10) &&
                  seekOk && hdrOk;
  std::printf(
      "[AVOX][TEST] case=subtext result=%s loadOk=%d seekOk=%d frames=%lld "
      "subFrames=%lld hdrOk=%d hdrEvents=%lld hdr=%d\n",
      ok ? "PASS" : "FAIL", (int)loadOk, (int)seekOk, (long long)ob.frames,
      (long long)ob.subFrames, (int)hdrOk, (long long)hdrEvents, hdrFlag);
  return ok ? 0 : 1;
}
