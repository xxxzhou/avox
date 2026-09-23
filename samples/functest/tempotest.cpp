// 音频变速不变调冒烟: avox_tempo 插件(SoundTouch) + ARenderTask tempo 分支。
// 动机: speed!=1 时音频走 IAudioTempo("soundtouch") 变速不变调, 插件缺位降级
//       变调重采样 —— 本测试分两段隔离判定:
//   1. DSP 段(不依赖素材): audioTempoHub.create("soundtouch") 触发插件懒加载,
//      48k/stereo/s16 正弦 5s 按 40ms 桶喂入 tempo=2.0, 输出时长应≈2.5s(±5%);
//      reset 后 tempo=1.0 喂 1s 出≈1s(比例回归)。缺插件/比例不对即 FAIL。
//   2. 管线段: MediaPlayer 开带音轨素材 speed(2.0), 6s 墙钟后位置推进应
//      ≥9s(1.5x), 期间无 IO/解码错误。素材缺失记 SKIP 不判死。
// 用法: tempotest [媒体路径]
//   缺省素材按 avox-test 仓约定路径查找; 插件 dll 须在 avox.dll 同级 plugins/ 下
// 判定: 末尾打印 [AVOX][TEST] case=tempotest result=PASS|FAIL ...
#include <cmath>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "avox/AvoxPlayer.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/audio/IAudioTempo.hpp"

using namespace avox;

namespace {

bool fileExists(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);
  return size > 0;
}

// 生成 40ms 48k/stereo/s16 正弦块
std::vector<uint8_t> sineS16(int32_t sampleRate, int32_t channels,
                             double freq, int32_t ms, int64_t& phase) {
  int32_t frames = sampleRate * ms / 1000;
  std::vector<uint8_t> buf((size_t)frames * channels * 2);
  int16_t* p = (int16_t*)buf.data();
  for (int32_t i = 0; i < frames; i++) {
    double v = std::sin(2.0 * 3.14159265358979 * freq *
                        ((double)(phase + i) / sampleRate)) * 8000;
    int16_t s = (int16_t)v;
    for (int32_t c = 0; c < channels; c++) {
      *p++ = s;
    }
  }
  phase += frames;
  return buf;
}

// 喂 inMs 毫秒正弦并拉干输出, 返回输出毫秒(拉不动返回 -1)
int64_t feedAndDrain(IAudioTempo* tempo, int32_t sampleRate, int32_t channels,
                     int32_t inMs, double freq, int64_t& phase) {
  int64_t fed = 0;
  while (fed < inMs) {
    int32_t chunk = inMs - fed > 40 ? 40 : inMs - fed;
    std::vector<uint8_t> in = sineS16(sampleRate, channels, freq, chunk, phase);
    AvoxData data = {in.data(), (int32_t)in.size(), true};
    if (tempo->process(data) <= 0) {
      return -1;
    }
    fed += chunk;
  }
  int64_t outBytes = 0;
  AvoxData out = {};
  while (tempo->receive(out) > 0) {
    outBytes += out.size;
  }
  return outBytes * 1000 / (sampleRate * channels * 2);
}

bool dspStage(bool& tempoMissing) {
  // create 内部触发插件懒加载扫描; 未装插件返回 nullptr
  IAudioTempo* tempo = AvoxManager::Get().audioTempoHub.create("soundtouch");
  if (!tempo) {
    tempoMissing = true;
    std::printf("[FAIL] audioTempoHub.create(soundtouch) 为空: plugins/avox_tempo.dll 未装或未注册\n");
    return false;
  }
  AudioDesc desc;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = 48000;
  if (!tempo->init(desc)) {
    std::printf("[FAIL] init(48k/stereo/s16) 失败\n");
    delete tempo;
    return false;
  }
  tempo->setTempo(2.0);
  int64_t phase = 0;
  int64_t outMs = feedAndDrain(tempo, 48000, 2, 5000, 440.0, phase);
  double ratio = outMs > 0 ? (double)outMs / 2500.0 : 0.0;
  bool ok1 = outMs > 0 && ratio > 0.95 && ratio < 1.05;
  std::printf("tempo=2.0 in=5000ms out=%lldms ratio=%.3f lat=%dms %s\n",
              (long long)outMs, ratio, (int)tempo->latencyMs(),
              ok1 ? "OK" : "BAD");
  // reset + 回常速: 比例应回 1.0
  tempo->reset();
  tempo->setTempo(1.0);
  int64_t outMs1 = feedAndDrain(tempo, 48000, 2, 1000, 660.0, phase);
  double ratio1 = outMs1 > 0 ? (double)outMs1 / 1000.0 : 0.0;
  bool ok2 = outMs1 > 0 && ratio1 > 0.95 && ratio1 < 1.05;
  std::printf("tempo=1.0 in=1000ms out=%lldms ratio=%.3f %s\n",
              (long long)outMs1, ratio1, ok2 ? "OK" : "BAD");
  delete tempo;
  if (!ok1) {
    std::printf("[FAIL] tempo=2.0 输出比例越界 (期待≈2500ms)\n");
  }
  if (!ok2) {
    std::printf("[FAIL] tempo=1.0 输出比例越界 (期待≈1000ms)\n");
  }
  return ok1 && ok2;
}

class TempoPlayerOb : public IMediaPlayerOb {
 public:
  int ioErrors = 0;
  int decodeErrors = 0;
  void onIoError(AVError, const char*) override { ioErrors++; }
  void onDecodeError(TrackType, DecodeResult) override { decodeErrors++; }
};

// 管线段: 返回 true=过 / false=挂; skip 置位表示素材缺失不算挂
bool pipelineStage(const std::string& media, bool& skip) {
  static const char* kCandidates[] = {
      "../../../avox-test/assets/video/selfcheck_av_beep_640x360_30s.mp4",
      "../../../../avox-test/assets/video/selfcheck_av_beep_640x360_30s.mp4",
      "D:/Work/github/avox-test/assets/video/selfcheck_av_beep_640x360_30s.mp4",
  };
  std::string url = media;
  if (url.empty()) {
    for (const char* cand : kCandidates) {
      if (fileExists(cand)) {
        url = cand;
        break;
      }
    }
  }
  if (url.empty() || !fileExists(url)) {
    skip = true;
    std::printf("[SKIP] 未找到带音轨素材(传参或放 avox-test/assets/video)\n");
    return true;
  }
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[FAIL] createMediaPlayer-null\n");
    return false;
  }
  TempoPlayerOb ob;
  addMediaPlayerOb(player, &ob);
  std::printf("pipeline open %s speed=2.0\n", url.c_str());
  std::fflush(stdout);
  player->open(url.c_str());
  // 等起播(至多 30s)
  bool playing = false;
  for (int i = 0; i < 300 && !playing; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    playing = player->getState() == PlayerState::playing;
  }
  if (!playing) {
    std::printf("[FAIL] 起播 30s 未到 playing\n");
    player->close();
    removeMediaPlayerOb(player, &ob);
    delete player;
    return false;
  }
  player->speed(2.0);
  int64_t pos0 = player->getPosition();
  auto t0 = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(6));
  int64_t posDelta = player->getPosition() - pos0;
  auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  double rate = wallMs > 0 ? (double)posDelta / wallMs : 0.0;
  std::printf("speed=2.0 wall=%lldms posDelta=%lldms rate=%.2f ioErr=%d decErr=%d\n",
              (long long)wallMs, (long long)posDelta, rate, ob.ioErrors,
              ob.decodeErrors);
  player->close();
  removeMediaPlayerOb(player, &ob);
  delete player;
  bool ok = rate >= 1.5 && ob.ioErrors == 0 && ob.decodeErrors == 0;
  if (!ok) {
    std::printf("[FAIL] 2x 速推进 rate<1.5 或出现错误回调\n");
  }
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  const char* media = argc > 1 ? argv[1] : "";
  std::printf("=== tempotest: 变速不变调冒烟 ===\n");
  bool tempoMissing = false;
  bool dspOk = dspStage(tempoMissing);
  bool skip = false;
  bool pipeOk = pipelineStage(media ? media : "", skip);
  bool ok = dspOk && pipeOk;
  std::printf("[AVOX][TEST] case=tempotest result=%s dsp=%s pipeline=%s%s\n",
              ok ? "PASS" : "FAIL", dspOk ? "PASS" : "FAIL",
              skip ? "SKIP" : (pipeOk ? "PASS" : "FAIL"),
              tempoMissing ? " (插件缺位)" : "");
  return ok ? 0 : 1;
}
