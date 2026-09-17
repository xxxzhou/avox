// 离线超分转码探针: 转码录制器 + Real-ESRGAN 逐帧(skipFrames=0)出增强 mp4
// 用法: enhancetest <input> [output] [mode=restore|2x|4x|auto] [maxMediaSec=0不限] [codec=h264|h265] [hardEnc=1|0]
// 判据: 输出尺寸=档位声明、进度回调连续、墙钟/媒体时长=超实时倍率(方案 P0-T1 回写)
// 流程: MediaPlayer 探测源分辨率 → setVideoDesc 声明档位输出 → enableQualityEnhance → 转码
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "avox/AvoxLayer.h"
#include "avox/AvoxMuxer.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {

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
  if (!probeResolution(input.c_str(), srcW, srcH, srcDurationMs)) {
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
  // 音频重采显式 48k(转码器默认 32k 是历史值, 产品不直用)
  AudioDesc adesc = {};
  adesc.channels = 2;
  adesc.format = AudioFormat::AVOX_AUDIO_S16;
  adesc.sampleRate = 48000;
  recorder->setAudioDesc(adesc);
  // 声明档位输出尺寸: onReady 据此 enableSizeChange + 改编码器描述(2x 编码 2x)
  // 注意: 图链顺序是 resize(sizeChange)→超分, 声明 2x 会先放大再叠超分变成 4x。
  // 超分场景不声明输出尺寸, 由超分层出 2x/4x, 编码器跟随实际帧尺寸自愈
  // (FFVEncoder::encode 尺寸变化即重置), 故这里不调 setVideoDesc
  recorder->getOption()->setBool("rec.hard.encode", hardEnc);
  // Real-ESRGAN 逐帧: skipFrames=0(实时轨抽帧语义在离线不适用)
  ISurfaceRender* render = recorder->getSurfaceRender();
  QualityEnhanceParamet qparamet = {};
  qparamet.model = QualityModel::RealESRGanX4V3;
  qparamet.outputMode = outputModeOf(mode);
  qparamet.skipFrames = 0;
  render->enableQualityEnhance(qparamet);
  addRecorderOb(recorder, &ob);
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
