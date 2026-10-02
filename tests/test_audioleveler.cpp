// 播放响度均衡单元测试: BS.1770 计量精度 + 增益级行为
//
// 参考值由 ffmpeg ebur128 滤镜对同参数合成信号实测(命令形如
//   ffmpeg -i x.wav -filter_complex ebur128 -f null -
// 信号定义即下方 makeTone/make51/Lcg, 48kHz float32 交错):
//   立体声 1kHz 正弦 amp0.1  → -20.0 LUFS   amp0.01 → -40.0   amp0.5 → -6.0
//   立体声 LCG 噪声 amp0.2   → -12.6 LUFS
//   5.1(LFE 独占 0.5)        → -21.7 LUFS
#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "audio/AudioLeveler.hpp"
#include "audio/LoudnessMeter.hpp"

namespace avox {

namespace {

constexpr int32_t kSampleRate = 48000;
constexpr double kPi = 3.14159265358979323846;

// 参考脚本同款 LCG(uint32 回绕), 保证噪声样本与参考逐位同源
struct Lcg {
  uint32_t state = 12345;
  float next() {
    state = state * 1664525u + 1013904223u;
    return (float)((double)state / 4294967295.0 * 2.0 - 1.0);
  }
};

// 每声道同值的正弦(交错)
std::vector<float> makeTone(double amp, double freq, int32_t channels = 2,
                            double durSec = 10.0) {
  const int32_t frames = (int32_t)(kSampleRate * durSec);
  std::vector<float> out((size_t)frames * channels);
  for (int32_t i = 0; i < frames; ++i) {
    float v = (float)(amp * std::sin(2.0 * kPi * freq * i / kSampleRate));
    for (int32_t c = 0; c < channels; ++c) {
      out[(size_t)i * channels + c] = v;
    }
  }
  return out;
}

// 5.1: FL/FR/FC 1kHz amp0.05, LFE 60Hz amp0.5, BL/BR 300Hz amp0.05
std::vector<float> make51(bool bLfe) {
  const int32_t frames = kSampleRate * 10;
  std::vector<float> out((size_t)frames * 6);
  for (int32_t i = 0; i < frames; ++i) {
    double t = (double)i / kSampleRate;
    float fl = (float)(0.05 * std::sin(2.0 * kPi * 1000.0 * t));
    float lf = (float)(0.5 * std::sin(2.0 * kPi * 60.0 * t));
    float bl = (float)(0.05 * std::sin(2.0 * kPi * 300.0 * t));
    out[(size_t)i * 6 + 0] = fl;
    out[(size_t)i * 6 + 1] = fl;
    out[(size_t)i * 6 + 2] = fl;
    out[(size_t)i * 6 + 3] = bLfe ? lf : 0.0f;
    out[(size_t)i * 6 + 4] = bl;
    out[(size_t)i * 6 + 5] = bl;
  }
  return out;
}

// 按固定块长喂入(模拟 40ms 渲染帧)
void feedInChunks(LoudnessMeter& meter, const std::vector<float>& data,
                  int32_t channels, int32_t chunkFrames) {
  const int32_t frames = (int32_t)(data.size() / channels);
  for (int32_t i = 0; i < frames; i += chunkFrames) {
    int32_t n = chunkFrames < frames - i ? chunkFrames : frames - i;
    meter.feed(data.data() + (size_t)i * channels, n);
  }
}

bool nearLufs(double value, double expected, double tol = 0.15) {
  return std::fabs(value - expected) < tol;
}

}  // namespace

TEST_CASE("响度计: 与 ffmpeg ebur128 参考值对照") {
  SUBCASE("立体声 1kHz 正弦 amp0.1") {
    LoudnessMeter meter;
    REQUIRE(meter.init(kSampleRate, 2));
    feedInChunks(meter, makeTone(0.1, 1000.0), 2, 1920);
    CHECK(nearLufs(meter.integratedLufs(), -19.99));
  }
  SUBCASE("立体声 LCG 噪声 amp0.2") {
    LoudnessMeter meter;
    REQUIRE(meter.init(kSampleRate, 2));
    const int32_t frames = kSampleRate * 10;
    std::vector<float> data((size_t)frames * 2);
    Lcg lcg;
    for (int32_t i = 0; i < frames; ++i) {
      float v = lcg.next() * 0.2f;
      data[(size_t)i * 2] = v;
      data[(size_t)i * 2 + 1] = v;
    }
    feedInChunks(meter, data, 2, 1920);
    CHECK(nearLufs(meter.integratedLufs(), -12.60));
  }
  SUBCASE("5.1 混合") {
    LoudnessMeter meter;
    REQUIRE(meter.init(kSampleRate, 6));
    feedInChunks(meter, make51(true), 6, 1920);
    CHECK(nearLufs(meter.integratedLufs(), -21.74));
  }
  SUBCASE("立体声 1kHz 正弦 amp0.01") {
    LoudnessMeter meter;
    REQUIRE(meter.init(kSampleRate, 2));
    feedInChunks(meter, makeTone(0.01, 1000.0), 2, 1920);
    CHECK(nearLufs(meter.integratedLufs(), -39.99));
  }
  SUBCASE("立体声 1kHz 正弦 amp0.5") {
    LoudnessMeter meter;
    REQUIRE(meter.init(kSampleRate, 2));
    feedInChunks(meter, makeTone(0.5, 1000.0), 2, 1920);
    CHECK(nearLufs(meter.integratedLufs(), -6.01));
  }
}

TEST_CASE("响度计: 切分粒度无关") {
  auto data = makeTone(0.1, 1000.0);
  LoudnessMeter whole;
  LoudnessMeter chunked;
  REQUIRE(whole.init(kSampleRate, 2));
  REQUIRE(chunked.init(kSampleRate, 2));
  whole.feed(data.data(), (int32_t)(data.size() / 2));
  feedInChunks(chunked, data, 2, 1920);
  CHECK(std::fabs(whole.integratedLufs() - chunked.integratedLufs()) < 1e-9);
  // 非整块(7 帧)切分也必须一致
  LoudnessMeter odd;
  REQUIRE(odd.init(kSampleRate, 2));
  feedInChunks(odd, data, 2, 7);
  CHECK(std::fabs(whole.integratedLufs() - odd.integratedLufs()) < 1e-9);
}

TEST_CASE("响度计: LFE 不计入") {
  LoudnessMeter withLfe;
  LoudnessMeter withoutLfe;
  REQUIRE(withLfe.init(kSampleRate, 6));
  REQUIRE(withoutLfe.init(kSampleRate, 6));
  feedInChunks(withLfe, make51(true), 6, 1920);
  feedInChunks(withoutLfe, make51(false), 6, 1920);
  CHECK(std::fabs(withLfe.integratedLufs() - withoutLfe.integratedLufs()) <
        1e-6);
}

TEST_CASE("响度计: 静音无响度且峰值清零") {
  LoudnessMeter meter;
  REQUIRE(meter.init(kSampleRate, 2));
  std::vector<float> silence(1920 * 2, 0.0f);
  for (int32_t i = 0; i < 250; ++i) {
    meter.feed(silence.data(), 1920);
  }
  CHECK(std::isnan(meter.integratedLufs()));
  CHECK_FALSE(meter.hasLoudness());
  CHECK(meter.samplePeak() == 0.0);
}

TEST_CASE("响度计: 非法参数拒绝") {
  LoudnessMeter meter;
  CHECK_FALSE(meter.init(0, 2));
  CHECK_FALSE(meter.init(kSampleRate, 0));
  CHECK_FALSE(meter.init(kSampleRate, 17));
  CHECK(meter.init(44100, 2));
  CHECK(meter.init(kSampleRate, 8));
}

TEST_CASE("均衡级: 静音不放大且零拷贝") {
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  AudioLeveler leveler;
  REQUIRE(leveler.init(desc, -18.0));
  std::vector<int16_t> silence(1920 * 2, 0);
  AvoxData out = {};
  for (int32_t i = 0; i < 250; ++i) {
    CHECK_FALSE(leveler.process((const uint8_t*)silence.data(),
                                (int32_t)(silence.size() * 2), out));
  }
  CHECK(leveler.getGainDb() == 0.0);
}

TEST_CASE("均衡级: 轻片增益上升, 上升速率 ≤3dB/s") {
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_FLT;
  AudioLeveler leveler;
  REQUIRE(leveler.init(desc, -18.0));
  auto data = makeTone(0.01, 1000.0);
  const int32_t frames = (int32_t)(data.size() / 2);
  const int32_t chunk = 1920;
  double preGain = leveler.getGainDb();
  double maxOutPeak = 0.0;
  for (int32_t i = 0; i < frames; i += chunk) {
    int32_t n = chunk < frames - i ? chunk : frames - i;
    AvoxData out = {};
    const uint8_t* src = (const uint8_t*)(data.data() + (size_t)i * 2);
    if (leveler.process(src, n * 2 * (int32_t)sizeof(float), out)) {
      const float* o = (const float*)out.data;
      for (int32_t k = 0; k < n * 2; ++k) {
        double a = std::fabs((double)o[k]);
        if (a > maxOutPeak) {
          maxOutPeak = a;
        }
      }
    }
    double gain = leveler.getGainDb();
    // 每次调用上升不超过 3dB/s × 帧时长
    CHECK(gain - preGain <= 3.0 * n / kSampleRate + 1e-6);
    preGain = gain;
  }
  // 目标 = -18 - (-39.99) = +21.99 → 钳到 +18dB(峰值护栏未触发)
  CHECK(std::fabs(leveler.getGainDb() - 18.0) < 0.2);
  CHECK(maxOutPeak <= 0.891 + 1e-6);
}

TEST_CASE("均衡级: 响片增益下降, 下降速率 ≤20dB/s") {
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_FLT;
  AudioLeveler leveler;
  REQUIRE(leveler.init(desc, -18.0));
  auto data = makeTone(0.5, 1000.0);
  const int32_t frames = (int32_t)(data.size() / 2);
  const int32_t chunk = 1920;
  double preGain = leveler.getGainDb();
  double maxOutPeak = 0.0;
  for (int32_t i = 0; i < frames; i += chunk) {
    int32_t n = chunk < frames - i ? chunk : frames - i;
    AvoxData out = {};
    const uint8_t* src = (const uint8_t*)(data.data() + (size_t)i * 2);
    if (leveler.process(src, n * 2 * (int32_t)sizeof(float), out)) {
      const float* o = (const float*)out.data;
      for (int32_t k = 0; k < n * 2; ++k) {
        double a = std::fabs((double)o[k]);
        if (a > maxOutPeak) {
          maxOutPeak = a;
        }
      }
    }
    double gain = leveler.getGainDb();
    CHECK(preGain - gain <= 20.0 * n / kSampleRate + 1e-6);
    preGain = gain;
  }
  // 目标 = -18 - (-6.01) = -11.99dB
  CHECK(std::fabs(leveler.getGainDb() + 11.99) < 0.3);
  CHECK(maxOutPeak <= 0.891 + 1e-6);
}

TEST_CASE("均衡级: 峰值护栏不削顶, 峰值封顶后回落") {
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_FLT;
  AudioLeveler leveler;
  REQUIRE(leveler.init(desc, -18.0));
  auto data = makeTone(0.01, 1000.0, 2, 10.0);
  const int32_t frames = (int32_t)(data.size() / 2);
  const int32_t chunk = 1920;
  // 第 5 秒插入一个满刻度样本
  data[(size_t)(kSampleRate * 5) * 2] = 1.0f;
  double maxOutPeak = 0.0;
  for (int32_t i = 0; i < frames; i += chunk) {
    int32_t n = chunk < frames - i ? chunk : frames - i;
    AvoxData out = {};
    const uint8_t* src = (const uint8_t*)(data.data() + (size_t)i * 2);
    if (leveler.process(src, n * 2 * (int32_t)sizeof(float), out)) {
      const float* o = (const float*)out.data;
      for (int32_t k = 0; k < n * 2; ++k) {
        double a = std::fabs((double)o[k]);
        if (a > maxOutPeak) {
          maxOutPeak = a;
        }
      }
    }
  }
  // 满刻度样本出现前增益已升到 +15dB 以上, 该帧被逐样本护栏压到 -1dBFS 以内
  CHECK(maxOutPeak <= 0.891 + 1e-6);
  // 峰值封顶 = 20log10(0.891/1.0) ≈ -1dB, 增益回落后停在那里
  CHECK(std::fabs(leveler.getGainDb() + 1.0) < 0.1);
}

TEST_CASE("均衡级: s16 交错/平面同结果且增益正确") {
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  auto flt = makeTone(0.5, 1000.0);
  const int32_t frames = (int32_t)(flt.size() / 2);
  // 转 s16 交错
  std::vector<int16_t> inter((size_t)frames * 2);
  for (size_t k = 0; k < inter.size(); ++k) {
    inter[k] = (int16_t)std::lround(flt[k] * 32768.0f);
  }
  // 转 s16 平面(整片口径, 仅用于生成源样本)
  std::vector<int16_t> planar((size_t)frames * 2);
  for (int32_t f = 0; f < frames; ++f) {
    planar[(size_t)f] = inter[(size_t)f * 2];
    planar[(size_t)frames + f] = inter[(size_t)f * 2 + 1];
  }
  AudioDesc descPlanar = desc;
  descPlanar.format = AudioFormat::AVOX_AUDIO_S16P;
  AudioLeveler levelerA;
  AudioLeveler levelerB;
  REQUIRE(levelerA.init(desc, -18.0));
  REQUIRE(levelerB.init(descPlanar, -18.0));
  const int32_t chunk = 1920;
  const int32_t lastIndex = frames - chunk;
  std::vector<int16_t> outInter, outPlanarInter;
  for (int32_t i = 0; i < frames; i += chunk) {
    int32_t n = chunk < frames - i ? chunk : frames - i;
    // 平面块内布局: plane0 的 n 个样本在前, plane1 在后(与引擎帧缓冲口径一致)
    std::vector<int16_t> planarChunk((size_t)n * 2);
    for (int32_t f = 0; f < n; ++f) {
      planarChunk[(size_t)f] = inter[(size_t)(i + f) * 2];
      planarChunk[(size_t)n + f] = inter[(size_t)(i + f) * 2 + 1];
    }
    AvoxData outA = {};
    AvoxData outB = {};
    bool modA = levelerA.process((const uint8_t*)(inter.data() + (size_t)i * 2),
                                 n * 2 * 2, outA);
    bool modB = levelerB.process((const uint8_t*)planarChunk.data(), n * 2 * 2,
                                 outB);
    REQUIRE(modA == modB);
    if (i == lastIndex) {
      outInter.assign((int16_t*)outA.data, (int16_t*)outA.data + (size_t)n * 2);
      const int16_t* p = (const int16_t*)outB.data;
      outPlanarInter.resize((size_t)n * 2);
      for (int32_t f = 0; f < n; ++f) {
        outPlanarInter[(size_t)f * 2] = p[f];
        outPlanarInter[(size_t)f * 2 + 1] = p[(size_t)n + f];
      }
    }
  }
  // 两种布局输出逐样本一致
  REQUIRE(outInter.size() == outPlanarInter.size());
  CHECK(outInter == outPlanarInter);
  // 收敛后末帧 = 输入 × 增益(±2LSB 舍入)
  const double gain = std::pow(10.0, levelerA.getGainDb() / 20.0);
  for (int32_t k = 0; k < (int32_t)outInter.size(); ++k) {
    int16_t expect = (int16_t)std::lround(inter[(size_t)lastIndex * 2 + k] * gain);
    CHECK(std::abs((int)outInter[(size_t)k] - (int)expect) <= 2);
  }
}

TEST_CASE("均衡级: 非法格式/参数拒绝") {
  AudioLeveler leveler;
  AudioDesc desc;
  desc.sampleRate = kSampleRate;
  desc.channels = 2;
  desc.format = AudioFormat::other;
  CHECK_FALSE(leveler.init(desc, -18.0));
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = 0;
  CHECK_FALSE(leveler.init(desc, -18.0));
  desc.sampleRate = kSampleRate;
  desc.channels = 0;
  CHECK_FALSE(leveler.init(desc, -18.0));
  desc.channels = 2;
  CHECK(leveler.init(desc, -18.0));
  AvoxData out = {};
  CHECK_FALSE(leveler.process(nullptr, 100, out));
}

}
