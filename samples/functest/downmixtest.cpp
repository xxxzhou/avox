// 下混系数/对白增强端到端探针: 直接驱动 FFResample(真实 SDK 代码 + 真实
// swresample), 用合成的 5.1 信号量测下混输出 RMS, 校验各档系数与热切。
//
// 判据(none 档 = 音频库默认 = 历史行为, 逐位不变):
//   FC 独占信号下, custom center 0dB 相对 none 应 +3dB(1.4125x)
//   LFE 独占信号下, none 应≈0(丢弃), dolby 应≈ -3dB 混入
//   热切: 中途换 custom 档, 后半段 RMS 应抬到 +3dB
//
// 同时落盘 FC 独占 5.1 WAV, 供 ffmpeg 命令行对照(默认下混应与 none 档一致):
//   ffmpeg -i downmixtest_51fc.wav -ac 2 -f null -
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "avox/AvoxAudio.h"
#include "avox_ffmpeg/FFResample.hpp"

using namespace avox;

static const int32_t kRate = 48000;
static const int64_t kChunkFrames = 1920;  // 40ms
static int32_t gFail = 0;

static void fail(const std::string& msg) {
  std::printf("[FAIL] %s\n", msg.c_str());
  gFail++;
}

static bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// 合成 5.1 交错 float32: activeCh 放 1kHz 正弦, 其余静音
// 声道序 = av_channel_layout_default(6) 的 5.1: FL FR FC LFE BL BR
static void make51(std::vector<float>& out, int64_t frames, int activeCh,
                   float amp) {
  out.assign((size_t)frames * 6, 0.0f);
  const double w = 2.0 * M_PI * 1000.0 / (double)kRate;
  for (int64_t i = 0; i < frames; i++) {
    out[(size_t)i * 6 + activeCh] = (float)(amp * std::sin(w * (double)i));
  }
}

static AudioDesc desc51() {
  AudioDesc d;
  d.channels = 6;
  d.format = AudioFormat::AVOX_AUDIO_FLT;
  d.sampleRate = kRate;
  return d;
}

static AudioDesc desc20() {
  AudioDesc d;
  d.channels = 2;
  d.format = AudioFormat::AVOX_AUDIO_FLT;
  d.sampleRate = kRate;
  return d;
}

// 喂 totalChunks 块(每块 40ms), 返回输出 RMS(none/custom 对比用);
// hotSwitchAfter>0 时在该块之后热切到尾段参数, 并分别返回前后段 RMS
struct RunResult {
  double rms = 0.0;
  double rmsHead = 0.0;
  double rmsTail = 0.0;
};
static RunResult runCase(std::vector<float> in51, const DownmixParamet& dm,
                         int32_t totalChunks, const DownmixParamet* tailDm = nullptr,
                         int32_t switchAfter = -1) {
  RunResult r;
  FFResample rs;
  if (!rs.init(desc51(), desc20(), dm)) {
    fail("resample init failed");
    return r;
  }
  double headSum = 0, tailSum = 0;
  int64_t headN = 0, tailN = 0, allN = 0;
  double allSum = 0;
  for (int32_t c = 0; c < totalChunks; c++) {
    if (tailDm && switchAfter >= 0 && c == switchAfter) {
      rs.setDownmix(*tailDm);  // 热切: 由 resample 内部惰性重建兑现
    }
    AvoxData in = {};
    in.data = (uint8_t*)in51.data();
    in.size = (int32_t)(kChunkFrames * 6 * sizeof(float));
    int32_t out = rs.resample(in);
    if (out <= 0) {
      fail("resample returned " + std::to_string(out) + " at chunk " +
           std::to_string(c));
      return r;
    }
    const float* f = (const float*)in.data;
    int64_t n = out / (int32_t)sizeof(float);
    double s = 0;
    for (int64_t i = 0; i < n; i++) {
      s += (double)f[i] * (double)f[i];
    }
    allSum += s;
    allN += n;
    if (switchAfter >= 0 && c < switchAfter) {
      headSum += s;
      headN += n;
    } else if (switchAfter >= 0) {
      tailSum += s;
      tailN += n;
    }
  }
  r.rms = allN ? std::sqrt(allSum / (double)allN) : 0.0;
  r.rmsHead = headN ? std::sqrt(headSum / (double)headN) : 0.0;
  r.rmsTail = tailN ? std::sqrt(tailSum / (double)tailN) : 0.0;
  return r;
}

// 落盘 float32 WAV(fmt=3), 供 ffmpeg 对照
static bool writeWavF32(const char* path, const std::vector<float>& data,
                        int32_t channels, int32_t rate) {
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    return false;
  }
  uint32_t dataBytes = (uint32_t)(data.size() * sizeof(float));
  uint16_t ch = (uint16_t)channels;
  uint32_t sr = (uint32_t)rate;
  uint32_t byteRate = sr * ch * 4;
  uint16_t blockAlign = (uint16_t)(ch * 4);
  uint32_t riffSize = 36 + dataBytes;
  uint32_t fmtSize = 16;
  uint16_t fmt = 3;  // IEEE float
  uint16_t bits = 32;
  uint32_t fourcc;
  memcpy(&fourcc, "RIFF", 4);
  fwrite(&fourcc, 4, 1, fp);
  fwrite(&riffSize, 4, 1, fp);
  memcpy(&fourcc, "WAVE", 4);
  fwrite(&fourcc, 4, 1, fp);
  memcpy(&fourcc, "fmt ", 4);
  fwrite(&fourcc, 4, 1, fp);
  fwrite(&fmtSize, 4, 1, fp);
  fwrite(&fmt, 2, 1, fp);
  fwrite(&ch, 2, 1, fp);
  fwrite(&sr, 4, 1, fp);
  fwrite(&byteRate, 4, 1, fp);
  fwrite(&blockAlign, 2, 1, fp);
  fwrite(&bits, 2, 1, fp);
  memcpy(&fourcc, "data", 4);
  fwrite(&fourcc, 4, 1, fp);
  fwrite(&dataBytes, 4, 1, fp);
  fwrite(data.data(), 1, dataBytes, fp);
  fclose(fp);
  return true;
}

int main() {
  const int32_t chunks = 100;  // 4s
  const int64_t frames = kChunkFrames * chunks;
  const float amp = 0.5f;
  std::vector<float> fc, lfe;
  make51(fc, frames, 2, amp);   // FC 独占
  make51(lfe, frames, 3, amp);  // LFE 独占(声道序第 4 个)

  DownmixParamet none;  // preset=none
  DownmixParamet itu;
  itu.preset = DownmixPreset::itu;
  DownmixParamet dolby;
  dolby.preset = DownmixPreset::dolby;
  DownmixParamet c0;
  c0.preset = DownmixPreset::custom;
  c0.centerGainDb = 0.0f;
  DownmixParamet c6;
  c6.preset = DownmixPreset::custom;
  c6.centerGainDb = 6.0f;

  // FC 独占: none 基准(= 库默认 -3dB), 及 0dB/+6dB 自定义
  double fcNone = runCase(fc, none, chunks).rms;
  double fcItu = runCase(fc, itu, chunks).rms;
  double fcC0 = runCase(fc, c0, chunks).rms;
  double fcC6 = runCase(fc, c6, chunks).rms;
  std::printf("[fc] none=%.6f itu=%.6f custom0=%.6f custom6=%.6f\n", fcNone,
              fcItu, fcC0, fcC6);
  // 对白增强: 中置 0dB 相对库默认(-3dB)抬 3dB, +6dB 相对默认抬 9dB
  if (!near(fcC0 / fcNone, 1.41254, 0.01)) {
    fail("FC custom 0dB 未抬到 +3dB: ratio=" + std::to_string(fcC0 / fcNone));
  }
  if (!near(fcC6 / fcNone, 2.81838, 0.02)) {  // +9dB(相对默认 -3dB)
    fail("FC custom +6dB 未到 +9dB: ratio=" + std::to_string(fcC6 / fcNone));
  }
  // ITU == 库默认语义: 本式 -3dB=10^(-3/20)=0.70795, 库常量 M_SQRT1_2=0.70711,
  // 差 0.01dB(容差 0.5%); 需逐位不变请用 none(不干预)
  if (!near(fcItu / fcNone, 1.0, 0.005)) {
    fail("FC ITU 偏离库默认超过 0.5%: " + std::to_string(fcItu / fcNone));
  }
  // LFE 独占: none 丢弃, dolby 保留
  double lfeNone = runCase(lfe, none, chunks).rms;
  double lfeDolby = runCase(lfe, dolby, chunks).rms;
  std::printf("[lfe] none=%.6f dolby=%.6f\n", lfeNone, lfeDolby);
  if (lfeNone > 1e-6) {
    fail("LFE 在 none 档应丢弃, 实测 RMS=" + std::to_string(lfeNone));
  }
  if (lfeDolby < 0.1) {
    fail("LFE 在 dolby 档应保留, 实测 RMS=" + std::to_string(lfeDolby));
  }
  // 热切: 前 50 块 none, 后 50 块 custom 0dB → 尾段 +3dB
  RunResult hot = runCase(fc, none, chunks, &c0, chunks / 2);
  std::printf("[hot] head=%.6f tail=%.6f ratio=%.4f\n", hot.rmsHead,
              hot.rmsTail, hot.rmsTail / hot.rmsHead);
  if (!near(hot.rmsTail / hot.rmsHead, 1.41254, 0.01)) {
    fail("热切未生效: ratio=" + std::to_string(hot.rmsTail / hot.rmsHead));
  }

  // 落盘 ffmpeg 对照件(FC 独占 5.1)
  const char* wav = "downmixtest_51fc.wav";
  if (writeWavF32(wav, fc, 6, kRate)) {
    std::printf("[dump] %s (ffmpeg -i %s -ac 2 -f null - 应与 none 档一致)\n",
                wav, wav);
  } else {
    fail("WAV 落盘失败");
  }

  if (gFail) {
    std::printf("=== downmix probe FAIL (%d) ===\n", gFail);
    return 1;
  }
  std::printf("=== downmix probe PASS ===\n");
  return 0;
}
