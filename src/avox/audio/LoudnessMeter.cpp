#include "LoudnessMeter.hpp"

#include <cmath>
#include <limits>

namespace avox {

bool LoudnessMeter::init(int32_t sampleRate_, int32_t channels_) {
  if (sampleRate_ < 8000 || sampleRate_ > 384000) {
    return false;
  }
  if (channels_ < 1 || channels_ > kMaxChannels) {
    return false;
  }
  sampleRate = sampleRate_;
  channels = channels_;
  // 与参考实现(libebur128/ffmpeg)同口径: 100ms hop 半样本取整, 门限块尺寸对齐
  hopSamples = (sampleRate + 5) / 10;
  designFilters();
  reset();
  return true;
}

void LoudnessMeter::designFilters() {
  const double pi = 3.14159265358979323846;
  // 第一级高架(1681.97Hz/+4dB/Q0.707) + 第二级高通(38.14Hz/Q0.5),
  // 参数取 BS.1770 参考实现值; 48kHz 下系数与标准公开值逐位一致
  double f0 = 1681.974450955533;
  double gain = 3.999843853973347;
  double q = 0.7071752369554196;
  double k = std::tan(pi * f0 / sampleRate);
  double vh = std::pow(10.0, gain / 20.0);
  double vb = std::pow(vh, 0.4996667741545416);
  double a0 = 1.0 + k / q + k * k;
  Biquad shelf;
  shelf.b0 = (vh + vb * k / q + k * k) / a0;
  shelf.b1 = 2.0 * (k * k - vh) / a0;
  shelf.b2 = (vh - vb * k / q + k * k) / a0;
  shelf.a1 = 2.0 * (k * k - 1.0) / a0;
  shelf.a2 = (1.0 - k / q + k * k) / a0;
  f0 = 38.13547087602444;
  q = 0.5003270373238773;
  k = std::tan(pi * f0 / sampleRate);
  Biquad hp;
  hp.b0 = 1.0;
  hp.b1 = -2.0;
  hp.b2 = 1.0;
  hp.a1 = 2.0 * (k * k - 1.0) / (1.0 + k / q + k * k);
  hp.a2 = (1.0 - k / q + k * k) / (1.0 + k / q + k * k);
  for (int32_t c = 0; c < channels; ++c) {
    filters[c][0] = shelf;
    filters[c][1] = hp;
  }
  // 声道权重按 FFmpeg 默认布局排位(av_channel_layout_default): 3=2.1 4=quad
  // 5=5.0 6=5.1 7=6.1 8=7.1, LFE 不计, 环绕 +1.5dB(1.41); 9+ 未知布局全 1
  for (int32_t c = 0; c < kMaxChannels; ++c) {
    channelWeight[c] = 1.0;
  }
  const double surroundGain = 1.41;
  switch (channels) {
    case 1:
    case 2:
      break;
    case 3:
      channelWeight[2] = 0.0;
      break;
    case 4:
      channelWeight[2] = surroundGain;
      channelWeight[3] = surroundGain;
      break;
    case 5:
      channelWeight[3] = surroundGain;
      channelWeight[4] = surroundGain;
      break;
    case 6:
      channelWeight[3] = 0.0;
      channelWeight[4] = surroundGain;
      channelWeight[5] = surroundGain;
      break;
    case 7:
      channelWeight[3] = 0.0;
      channelWeight[5] = surroundGain;
      channelWeight[6] = surroundGain;
      break;
    case 8:
      channelWeight[3] = 0.0;
      for (int32_t c = 4; c < 8; ++c) {
        channelWeight[c] = surroundGain;
      }
      break;
    default:
      break;
  }
}

void LoudnessMeter::reset() {
  for (int32_t c = 0; c < kMaxChannels; ++c) {
    filters[c][0].clear();
    filters[c][1].clear();
  }
  for (int32_t h = 0; h < kHopPerBlock; ++h) {
    hopLen[h] = 0;
    for (int32_t c = 0; c < kMaxChannels; ++c) {
      hopSum[h][c] = 0.0;
    }
  }
  hopCount = 0;
  hopIndex = 0;
  hopFilled = 0;
  blockEnergy.clear();
  rawPeak = 0.0;
}

void LoudnessMeter::feed(const float* interleaved, int32_t frames) {
  if (!interleaved || frames <= 0 || channels <= 0) {
    return;
  }
  for (int32_t f = 0; f < frames; ++f) {
    const float* frame = interleaved + (int64_t)f * channels;
    for (int32_t c = 0; c < channels; ++c) {
      double x = frame[c];
      double a = std::fabs(x);
      if (a > rawPeak) {
        rawPeak = a;
      }
      double y = filters[c][0].process(x);
      y = filters[c][1].process(y);
      hopSum[hopIndex][c] += y * y;
    }
    if (++hopCount >= hopSamples) {
      finishHop();
    }
  }
}

void LoudnessMeter::finishHop() {
  hopLen[hopIndex] = hopSamples;
  if (hopFilled < kHopPerBlock) {
    ++hopFilled;
  }
  // 攒满 4 个 hop 即出一个 400ms 块(75% 重叠: 每 hop 滑一步)
  if (hopFilled >= kHopPerBlock) {
    pushBlock();
  }
  hopIndex = (hopIndex + 1) % kHopPerBlock;
  hopCount = 0;
  hopLen[hopIndex] = 0;
  for (int32_t c = 0; c < kMaxChannels; ++c) {
    hopSum[hopIndex][c] = 0.0;
  }
}

void LoudnessMeter::pushBlock() {
  int32_t frames = 0;
  for (int32_t h = 0; h < kHopPerBlock; ++h) {
    frames += hopLen[h];
  }
  if (frames <= 0) {
    return;
  }
  double blockSum = 0.0;
  for (int32_t c = 0; c < channels; ++c) {
    double sum = 0.0;
    for (int32_t h = 0; h < kHopPerBlock; ++h) {
      sum += hopSum[h][c];
    }
    blockSum += channelWeight[c] * (sum / frames);
  }
  blockEnergy.push_back((float)blockSum);
}

double LoudnessMeter::integratedLufs() const {
  if (blockEnergy.empty()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  // 绝对门限: 块响度 > -70 LUFS
  const double gateAbs =
      std::pow(10.0, (kAbsGateLufs - kLoudnessOffset) / 10.0);
  double sum = 0.0;
  int32_t count = 0;
  for (float z : blockEnergy) {
    if (z > gateAbs) {
      sum += z;
      ++count;
    }
  }
  if (count == 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  // 相对门限: 绝对门限后的均值再减 10 LU
  const double gateRel = (sum / count) * std::pow(10.0, -kRelGateLu / 10.0);
  double sumGated = 0.0;
  int32_t countGated = 0;
  for (float z : blockEnergy) {
    if (z > gateAbs && z > gateRel) {
      sumGated += z;
      ++countGated;
    }
  }
  if (countGated == 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return kLoudnessOffset + 10.0 * std::log10(sumGated / countGated);
}

bool LoudnessMeter::hasLoudness() const { return !std::isnan(integratedLufs()); }

double LoudnessMeter::samplePeakDbfs() const {
  if (rawPeak <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }
  return 20.0 * std::log10(rawPeak);
}

}
