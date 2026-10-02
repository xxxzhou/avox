#include "AudioLeveler.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace avox {

namespace {

// 格式表(AVOX_MAP_AUDIO_FMT)本地生成: 与 Audio.cpp 的
// audioFormatSize/bAPlaneFormat 同表, 留一份让本模块可独立编入单测目标
int32_t sampleBytes(AudioFormat format) {
  switch (format) {
#define XX(name, value, size, plane, str) \
  case AudioFormat::name:                 \
    return size;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return 0;
  }
}

bool isPlaneFormat(AudioFormat format) {
  switch (format) {
#define XX(name, value, size, plane, str) \
  case AudioFormat::name:                 \
    return plane != 0;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return false;
  }
}

// 采样归一化到 float(-1~1, 整数按满量程); 浮点格式原样
template <typename T> inline float sampleToFloat(T v);

template <> inline float sampleToFloat<uint8_t>(uint8_t v) {
  return (float)((int32_t)v - 128) / 128.0f;
}
template <> inline float sampleToFloat<int16_t>(int16_t v) {
  return (float)v / 32768.0f;
}
template <> inline float sampleToFloat<int32_t>(int32_t v) {
  return (float)((double)v / 2147483648.0);
}
template <> inline float sampleToFloat<int64_t>(int64_t v) {
  return (float)((double)v / 9223372036854775808.0);
}
template <> inline float sampleToFloat<float>(float v) { return v; }
template <> inline float sampleToFloat<double>(double v) { return (float)v; }

// float → 采样(整数格式饱和钳位, 不产生回绕)
template <typename T> inline T floatToSample(float v);

template <> inline uint8_t floatToSample<uint8_t>(float v) {
  int32_t iv = (int32_t)std::lround(v * 128.0f) + 128;
  if (iv < 0) iv = 0;
  if (iv > 255) iv = 255;
  return (uint8_t)iv;
}
template <> inline int16_t floatToSample<int16_t>(float v) {
  int32_t iv = (int32_t)std::lround(v * 32768.0f);
  if (iv > 32767) iv = 32767;
  if (iv < -32768) iv = -32768;
  return (int16_t)iv;
}
template <> inline int32_t floatToSample<int32_t>(float v) {
  double dv = std::round((double)v * 2147483648.0);
  if (dv > 2147483647.0) dv = 2147483647.0;
  if (dv < -2147483648.0) dv = -2147483648.0;
  return (int32_t)dv;
}
template <> inline int64_t floatToSample<int64_t>(float v) {
  double dv = std::round((double)v * 9223372036854775808.0);
  if (dv >= 9223372036854775807.0) return (int64_t)9223372036854775807LL;
  if (dv <= -9223372036854775808.0) return (int64_t)(-9223372036854775807LL - 1);
  return (int64_t)dv;
}
template <> inline float floatToSample<float>(float v) { return v; }
template <> inline double floatToSample<double>(float v) { return (double)v; }

template <typename T>
void unpackTyped(const uint8_t* src, int32_t frames, int32_t channels,
                 bool planar, float* out) {
  const T* s = reinterpret_cast<const T*>(src);
  for (int32_t f = 0; f < frames; ++f) {
    for (int32_t c = 0; c < channels; ++c) {
      size_t idx = planar ? (size_t)c * frames + f : (size_t)f * channels + c;
      out[(size_t)f * channels + c] = sampleToFloat<T>(s[idx]);
    }
  }
}

template <typename T>
void applyTyped(uint8_t* dst, const float* src, int32_t frames,
                int32_t channels, bool planar, double fromGain,
                double toGain, double peakCeil) {
  T* d = reinterpret_cast<T*>(dst);
  const double step = (toGain - fromGain) / frames;
  double g = fromGain;
  for (int32_t f = 0; f < frames; ++f) {
    g += step;
    const float* frame = src + (size_t)f * channels;
    // 峰值护栏按帧取最大(同帧各声道同一增益, 不动声像)
    double peak = 0.0;
    for (int32_t c = 0; c < channels; ++c) {
      double a = std::fabs((double)frame[c]);
      if (a > peak) {
        peak = a;
      }
    }
    double gain = g;
    if (peak * gain > peakCeil) {
      gain = peakCeil / peak;
    }
    for (int32_t c = 0; c < channels; ++c) {
      size_t idx = planar ? (size_t)c * frames + f : (size_t)f * channels + c;
      d[idx] = floatToSample<T>(frame[c] * (float)gain);
    }
  }
}

}  // namespace

bool AudioLeveler::init(const AudioDesc& desc_, double targetLufs_) {
  desc = desc_;
  targetLufs = targetLufs_;
  bValid = false;
  gainDb = 0.0;
  scratch.clear();
  outBuffer.clear();
  if (!desc.bValid() || sampleBytes(desc.format) <= 0) {
    return false;
  }
  if (!meter.init(desc.sampleRate, desc.channels)) {
    return false;
  }
  bValid = true;
  return true;
}

void AudioLeveler::reset() {
  meter.reset();
  gainDb = 0.0;
  scratch.clear();
  outBuffer.clear();
}

bool AudioLeveler::unpack(const uint8_t* src, int32_t frames) {
  const int32_t channels = desc.channels;
  const bool planar = isPlaneFormat(desc.format);
  float* out = scratch.data();
  switch (desc.format) {
    case AudioFormat::AVOX_AUDIO_U8:
      unpackTyped<uint8_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S16:
      unpackTyped<int16_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S32:
      unpackTyped<int32_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S64:
      unpackTyped<int64_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_FLT:
      unpackTyped<float>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_DBL:
      unpackTyped<double>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_U8P:
      unpackTyped<uint8_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S16P:
      unpackTyped<int16_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S32P:
      unpackTyped<int32_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_S64P:
      unpackTyped<int64_t>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_FLTP:
      unpackTyped<float>(src, frames, channels, planar, out);
      break;
    case AudioFormat::AVOX_AUDIO_DBLP:
      unpackTyped<double>(src, frames, channels, planar, out);
      break;
    default:
      return false;
  }
  return true;
}

bool AudioLeveler::applyTo(uint8_t* dst, int32_t frames, double fromDb,
                           double toDb) {
  const int32_t channels = desc.channels;
  const bool planar = isPlaneFormat(desc.format);
  const float* src = scratch.data();
  const double fromGain = std::pow(10.0, fromDb / 20.0);
  const double toGain = std::pow(10.0, toDb / 20.0);
  switch (desc.format) {
    case AudioFormat::AVOX_AUDIO_U8:
      applyTyped<uint8_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S16:
      applyTyped<int16_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S32:
      applyTyped<int32_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S64:
      applyTyped<int64_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_FLT:
      applyTyped<float>(dst, src, frames, channels, planar, fromGain, toGain,
                        kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_DBL:
      applyTyped<double>(dst, src, frames, channels, planar, fromGain, toGain,
                         kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_U8P:
      applyTyped<uint8_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S16P:
      applyTyped<int16_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S32P:
      applyTyped<int32_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_S64P:
      applyTyped<int64_t>(dst, src, frames, channels, planar, fromGain, toGain,
                          kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_FLTP:
      applyTyped<float>(dst, src, frames, channels, planar, fromGain, toGain,
                        kPeakCeil);
      break;
    case AudioFormat::AVOX_AUDIO_DBLP:
      applyTyped<double>(dst, src, frames, channels, planar, fromGain, toGain,
                         kPeakCeil);
      break;
    default:
      return false;
  }
  return true;
}

bool AudioLeveler::process(const uint8_t* data, int32_t size, AvoxData& out) {
  if (!bValid || !data || size <= 0) {
    return false;
  }
  const int32_t elementBytes = sampleBytes(desc.format);
  if (elementBytes <= 0 || desc.channels <= 0) {
    return false;
  }
  const int32_t frameBytes = elementBytes * desc.channels;
  const int32_t frames = size / frameBytes;
  if (frames <= 0) {
    return false;
  }
  scratch.resize((size_t)frames * desc.channels);
  if (!unpack(data, frames)) {
    return false;
  }
  // 计量在输入域: 增益不改测量, 无反馈回路
  meter.feed(scratch.data(), frames);
  // 目标增益: 无有效响度(开头<400ms/静音)保持 unity, 不放大也不衰减
  double target = 0.0;
  const double lufs = meter.integratedLufs();
  if (!std::isnan(lufs)) {
    target = targetLufs - lufs;
    // 峰值封顶: 已听到的最大峰值推过 -1dBFS 就不抬, 该降照降
    const double ceiling = 20.0 * std::log10(kPeakCeil / meter.samplePeak());
    if (target > ceiling) {
      target = ceiling;
    }
    if (target > kMaxGainDb) {
      target = kMaxGainDb;
    }
    if (target < kMinGainDb) {
      target = kMinGainDb;
    }
  }
  // 快路: 无需增益且输入峰值未顶护栏 —— 原帧直接用(零拷贝)
  if (gainDb == 0.0 && target == 0.0 && meter.samplePeak() <= kPeakCeil) {
    return false;
  }
  // 慢滑动: 上升慢(无抽气), 下降快(防削顶)
  const double maxUp = kRiseDbPerSec * frames / desc.sampleRate;
  const double maxDown = kFallDbPerSec * frames / desc.sampleRate;
  double next = gainDb;
  if (target > gainDb) {
    next = std::min(target, gainDb + maxUp);
  } else if (target < gainDb) {
    next = std::max(target, gainDb - maxDown);
  }
  if (next > kMaxGainDb) {
    next = kMaxGainDb;
  }
  if (next < kMinGainDb) {
    next = kMinGainDb;
  }
  outBuffer.resize(size);
  // 末尾不足一帧的残余字节原样保留(不改增益)
  const int32_t doneBytes = frames * frameBytes;
  if (doneBytes < size) {
    memcpy(outBuffer.data() + doneBytes, data + doneBytes,
           (size_t)(size - doneBytes));
  }
  if (!applyTo(outBuffer.data(), frames, gainDb, next)) {
    outBuffer.clear();
    return false;
  }
  gainDb = next;
  out.data = outBuffer.data();
  out.size = size;
  out.bRef = true;
  return true;
}

}
