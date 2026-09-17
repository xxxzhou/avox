// 离线超分转码探针: 转码录制器 + Real-ESRGAN 逐帧(skipFrames=0)出增强 mp4
// 用法: enhancetest <input> [output] [mode=restore|2x|4x|auto] [maxMediaSec=0不限] [codec=h264|h265] [hardEnc=1|0]
// 判据: 输出尺寸=档位声明、进度回调连续、墙钟/媒体时长=超实时倍率(方案 P0-T1 回写)
// 流程: MediaPlayer 探测源分辨率 → setVideoDesc 声明档位输出 → enableQualityEnhance → 转码
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include <windows.h>
#include <dbghelp.h>

#include "avox/AvoxLayer.h"
#include "avox/AvoxMuxer.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {

// 段错误现场: 捕获异常并解析调用栈(带符号), 定位 heisenbug 用
LONG WINAPI segvHandler(EXCEPTION_POINTERS* info) {
  fprintf(stderr, "\n[SEGV] code=0x%lX addr=%p tid=%lu\n",
          (unsigned long)info->ExceptionRecord->ExceptionCode,
          info->ExceptionRecord->ExceptionAddress,
          GetCurrentThreadId());
  // AV 细节: [0]=0读/1写/8执行(DEP), [1]=目标地址 → 一眼分辨 src/dst 失效
  if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
      info->ExceptionRecord->NumberParameters >= 2) {
    ULONG_PTR op = info->ExceptionRecord->ExceptionInformation[0];
    void* target = (void*)info->ExceptionRecord->ExceptionInformation[1];
    fprintf(stderr, "  access=%s target=%p\n",
            op == 0 ? "READ" : (op == 1 ? "WRITE" : "EXEC"), target);
    HMODULE tmod = nullptr;
    char tmodName[MAX_PATH] = {};
    if (target && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)target, &tmod)) {
      GetModuleFileNameA(tmod, tmodName, MAX_PATH);
      fprintf(stderr, "  target in module: %s (base=%p offset=0x%llX)\n",
              tmodName, (void*)tmod,
              (unsigned long long)((uintptr_t)target - (uintptr_t)tmod));
    } else {
      fprintf(stderr, "  target in module: <heap/unmapped>\n");
    }
  }
  void* stack[24] = {};
  WORD n = CaptureStackBackTrace(0, 24, stack, nullptr);
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  // 崩溃地址落在哪个模块 (坏函数指针的常见定位手段)
  {
    HMODULE mod = nullptr;
    char modName[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)info->ExceptionRecord->ExceptionAddress,
                           &mod)) {
      GetModuleFileNameA(mod, modName, MAX_PATH);
      fprintf(stderr, "  crash addr in module: %s\n", modName);
    } else {
      fprintf(stderr, "  crash addr in module: <unknown>\n");
    }
  }
  // 沿 ContextRecord 回溯故障线程真实调用栈 (CaptureStack 只能看到 handler 帧)
  {
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame = {};
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rsp;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    HANDLE thread = GetCurrentThread();
    for (int i = 0; i < 24; ++i) {
      if (!StackWalk64(machine, GetCurrentProcess(), thread, &frame, &ctx,
                       nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                       nullptr)) {
        break;
      }
      if (frame.AddrPC.Offset == 0) {
        break;
      }
      char buf[512] = {};
      SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 400;
      DWORD64 off = 0;
      if (SymFromAddr(GetCurrentProcess(), frame.AddrPC.Offset, &off, sym)) {
        fprintf(stderr, "  #%02d %s +0x%llX\n", i, sym->Name,
                (unsigned long long)off);
      } else {
        fprintf(stderr, "  #%02d pc=%p\n", i, (void*)frame.AddrPC.Offset);
      }
    }
  }
  fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// 档位 → 输出倍率(与 VkQEnhanceLayer::calcOutputSize 的 Auto 规则一致)
int32_t scaleOf(const std::string& mode, int32_t srcW, int32_t srcH) {
  if (mode == "restore") {
    return 1;
  }
  if (mode == "2x") {
    return 2;
  }
  if (mode == "4x") {
    return 4;
  }
  return (srcW >= 1920 || srcH >= 1080) ? 2 : 4;  // auto
}

QualityOutputMode outputModeOf(const std::string& mode) {
  if (mode == "restore") {
    return QualityOutputMode::Restore;
  }
  if (mode == "2x") {
    return QualityOutputMode::Upscale2x;
  }
  if (mode == "4x") {
    return QualityOutputMode::Upscale4x;
  }
  return QualityOutputMode::Auto;
}

// MediaPlayer 探测源分辨率(转码声明输出尺寸需在 open 前知道源尺寸)
bool probeResolution(const char* url, int32_t& width, int32_t& height,
                     int64_t& durationMs) {
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    return false;
  }
  player->setHardDecode(false);
  player->open(url);
  bool ready = false;
  for (int i = 0; i < 150; ++i) {  // 15s
    if (player->getState() == PlayerState::playing) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (ready) {
    ISourceInfo* info = player->getSourceInfo();
    if (info && info->videoSize() > 0) {
      VTrackDesc vdesc = info->getVideoDesc(0);
      width = vdesc.desc.width;
      height = vdesc.desc.height;
      durationMs = player->getDuration();
    } else {
      ready = false;
    }
  }
  player->close();
  delete player;
  return ready && width > 0 && height > 0;
}

class EnhanceOb : public IRecorderOb, public ISurfaceRenderOb {
 public:
  void onStateChange(RecorderState preState, RecorderState state) override {
    std::printf("[enh] state %d -> %d\n", (int32_t)preState, (int32_t)state);
  }
  void onIoError(AVError error, const char* msg) override {
    std::printf("[enh] ioError %d %s\n", (int32_t)error, msg ? msg : "");
    hasError = true;
  }
  void onProgress(const RecorderProgress& progress) override {
    mediaMs = progress.currentTimeMs;
    totalMs = progress.totalTimeMs;
  }
  void onComplete() override { completed = true; }
  // 离屏直出通道每处理完一帧派发一次, 用作逐帧计数
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    (void)buf;
    (void)yuvType;
    frames++;
  }
  std::atomic<int64_t> frames{0};
  std::atomic<int64_t> mediaMs{0};
  int64_t totalMs = 0;
  bool completed = false;
  bool hasError = false;
};

}  // namespace

int main(int argc, char* argv[]) {
  SetUnhandledExceptionFilter(segvHandler);
  std::string input = argc > 1 ? argv[1] : "";
  if (input.empty()) {
    std::printf(
        "usage: enhancetest <input> [output] [mode=restore|2x|4x|auto] "
        "[maxMediaSec=0] [codec=h264|h265] [hardEnc=1|0]\n");
    return 1;
  }
  std::string output = argc > 2 ? argv[2] : "enhance_output.mp4";
  std::string mode = argc > 3 ? argv[3] : "auto";
  int maxMediaSec = argc > 4 ? std::atoi(argv[4]) : 0;
  std::string codec = argc > 5 ? argv[5] : "h264";
  bool hardEnc = argc > 6 ? std::atoi(argv[6]) != 0 : true;

  int32_t srcW = 0, srcH = 0;
  int64_t srcDurationMs = 0;
  // [dbg] 竞态排查: ENH_NOPROBE=1 跳过 MediaPlayer 探测(用假尺寸), 验证探测
  // 的快速 open/close 是否与后续 recorder 打开同一文件冲突
  static const bool noProbe = std::getenv("ENH_NOPROBE") != nullptr;
  if (noProbe) {
    srcW = 640;
    srcH = 360;
  } else if (!probeResolution(input.c_str(), srcW, srcH, srcDurationMs)) {
    std::printf("[enh] probe failed: %s\n", input.c_str());
    return 1;
  }
  int32_t scale = scaleOf(mode, srcW, srcH);
  std::printf("[enh] source %dx%d duration=%lldms mode=%s -> output %dx%d\n",
              srcW, srcH, (long long)srcDurationMs, mode.c_str(), srcW * scale,
              srcH * scale);

  EnhanceOb ob = {};
  IRecorder* recorder = createRecorder(true);
  if (!recorder) {
    std::printf("[enh] createRecorder failed\n");
    return 1;
  }
  recorder->setVideoCodec(codec == "h265" ? VCodecId::h265 : VCodecId::h264);
  // [dbg] 竞态排查: ENH_NOAUD=1 丢弃音轨, 验证音频重采样链路
  static const bool noAud = std::getenv("ENH_NOAUD") != nullptr;
  if (noAud) {
    recorder->setAudioCodec(ACodecId::none);
  } else {
    AudioDesc adesc = {};
    adesc.channels = 2;
    adesc.format = AudioFormat::AVOX_AUDIO_S16;
    adesc.sampleRate = 48000;
    recorder->setAudioDesc(adesc);
  }
  // 离线增强: 走录制器级 API(队列消费侧推理), 不往渲染图里挂层。
  // 图只做 yuv→rgba, rgba 帧入队(满则反压解码), 编码线程逐帧推理→yuv→编码;
  // 输出尺寸由增强器按档位在 onReady 自动声明(setVideoDesc 不用)
  recorder->getOption()->setBool("rec.hard.encode", hardEnc);
  QualityEnhanceParamet qparamet = {};
  qparamet.model = QualityModel::RealESRGanX4V3;
  qparamet.outputMode = outputModeOf(mode);
  qparamet.skipFrames = 0;
  recorder->enableQualityEnhance(qparamet);
  addRecorderOb(recorder, &ob);
  ISurfaceRender* render = recorder->getSurfaceRender();
  addSurfaceRenderOb(render, &ob);

  int64_t wallStart = nowMs();
  if (!recorder->open(input.c_str(), output.c_str())) {
    std::printf("[enh] open failed\n");
    removeSurfaceRenderOb(render, &ob);
    removeRecorderOb(recorder, &ob);
    delete recorder;
    return 1;
  }
  int64_t lastPrint = 0;
  while (true) {
    RecorderState state = recorder->getState();
    if (state == RecorderState::completed || state == RecorderState::failed ||
        ob.completed || ob.hasError) {
      break;
    }
    if (maxMediaSec > 0 && ob.mediaMs >= (int64_t)maxMediaSec * 1000) {
      std::printf("[enh] reach maxMediaSec %ds, stop early\n", maxMediaSec);
      break;
    }
    int64_t wall = nowMs() - wallStart;
    if (wall - lastPrint >= 2000) {
      lastPrint = wall;
      double fps = ob.frames.load() * 1000.0 / (wall > 0 ? wall : 1);
      std::printf("[enh] wall=%lldms media=%lld/%lldms frames=%lld %.1ffps\n",
                  (long long)wall, (long long)ob.mediaMs.load(),
                  (long long)ob.totalMs, (long long)ob.frames.load(), fps);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  recorder->close();
  int64_t wallMs = nowMs() - wallStart;
  removeSurfaceRenderOb(render, &ob);
  removeRecorderOb(recorder, &ob);
  delete recorder;

  int64_t mediaMs = ob.mediaMs.load();
  int64_t frames = ob.frames.load();
  double speed = wallMs > 0 ? (double)mediaMs / wallMs : 0;
  double perFrameMs = frames > 0 ? (double)wallMs / frames : 0;
  std::error_code ec;
  uintmax_t outBytes = std::filesystem::file_size(output, ec);
  std::printf(
      "[enh] === summary ===\n"
      "  output: %s (%lld bytes, %s)\n"
      "  %dx%d -> %dx%d, codec=%s hardEnc=%d\n"
      "  media=%lldms wall=%lldms speed=%.2fx realtime\n"
      "  frames=%lld perFrame=%.1fms\n",
      output.c_str(), (long long)(ec ? 0 : outBytes),
      ob.hasError ? "ERROR" : "ok", srcW, srcH, srcW * scale, srcH * scale,
      codec.c_str(), hardEnc ? 1 : 0, (long long)mediaMs, (long long)wallMs,
      speed, (long long)frames, perFrameMs);
  return ob.hasError ? 1 : 0;
}
