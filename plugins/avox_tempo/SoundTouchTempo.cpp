#include "SoundTouchTempo.hpp"

#include <string.h>

namespace avox {

namespace {

// packed 单样本字节数(0=不支持变速链的格式)
int32_t sampleBytes(AudioFormat fmt) {
  switch (fmt) {
    case AudioFormat::AVOX_AUDIO_U8:
      return 1;
    case AudioFormat::AVOX_AUDIO_S16:
      return 2;
    case AudioFormat::AVOX_AUDIO_S32:
    case AudioFormat::AVOX_AUDIO_FLT:
      return 4;
    case AudioFormat::AVOX_AUDIO_DBL:
      return 8;
    default:
      return 0;
  }
}

float loadSample(AudioFormat fmt, const uint8_t* p) {
  switch (fmt) {
    case AudioFormat::AVOX_AUDIO_U8:
      return ((int)p[0] - 128) / 128.0f;
    case AudioFormat::AVOX_AUDIO_S16: {
      int16_t v;
      memcpy(&v, p, 2);
      return v / 32768.0f;
    }
    case AudioFormat::AVOX_AUDIO_S32: {
      int32_t v;
      memcpy(&v, p, 4);
      return (float)(v / 2147483648.0);
    }
    case AudioFormat::AVOX_AUDIO_FLT: {
      float v;
      memcpy(&v, p, 4);
      return v;
    }
    case AudioFormat::AVOX_AUDIO_DBL: {
      double v;
      memcpy(&v, p, 8);
      return (float)v;
    }
    default:
      return 0.0f;
  }
}

void saveSample(AudioFormat fmt, uint8_t* p, float v) {
  if (v > 1.0f) {
    v = 1.0f;
  } else if (v < -1.0f) {
    v = -1.0f;
  }
  switch (fmt) {
    case AudioFormat::AVOX_AUDIO_U8:
      p[0] = (uint8_t)(v * 127.0f + 128.0f);
      break;
    case AudioFormat::AVOX_AUDIO_S16: {
      int16_t s = (int16_t)(v * 32767.0f);
      memcpy(p, &s, 2);
      break;
    }
    case AudioFormat::AVOX_AUDIO_S32: {
      int32_t s = (int32_t)((double)v * 2147483647.0);
      memcpy(p, &s, 4);
      break;
    }
    case AudioFormat::AVOX_AUDIO_FLT:
      memcpy(p, &v, 4);
      break;
    case AudioFormat::AVOX_AUDIO_DBL: {
      double d = v;
      memcpy(p, &d, 8);
      break;
    }
    default:
      break;
  }
}

// 单次从 SoundTouch 拉出的最大帧数(≈85ms@48k, 只影响批量转换粒度)
constexpr int32_t kPullFrames = 4096;

}  // namespace

bool SoundTouchTempo::init(const AudioDesc& d) {
  // 仅 packed interleaved(planar 进不来: FFADecoder 输出已归一 packed)
  if (!d.bValid() || (int)d.format >= (int)AudioFormat::AVOX_AUDIO_U8P) {
    return false;
  }
  int32_t sb = sampleBytes(d.format);
  if (sb <= 0) {
    return false;
  }
  desc = d;
  frameBytes = sb * desc.channels;
  sliceBytes = frameBytes * desc.sampleRate * 40 / 1000;
  if (sliceBytes < frameBytes) {
    sliceBytes = frameBytes;
  }
  processor.setSampleRate(desc.sampleRate);
  processor.setChannels(desc.channels);
  processor.setTempo(1.0);
  feedBuf.clear();
  pullBuf.clear();
  outBuf.clear();
  outOffset = 0;
  return true;
}

void SoundTouchTempo::setTempo(double speed) { processor.setTempo(speed); }

int SoundTouchTempo::process(const AvoxData& in) {
  if (!desc.bValid() || in.size <= 0 || frameBytes <= 0) {
    return -1;
  }
  // 尾数不足一样本帧的丢弃(上游 40ms 桶对齐, 正常不出现)
  int32_t frames = in.size / frameBytes;
  if (frames <= 0) {
    return -1;
  }
  int32_t sb = sampleBytes(desc.format);
  feedBuf.resize((size_t)frames * desc.channels);
  for (int32_t i = 0; i < frames * desc.channels; i++) {
    feedBuf[i] = loadSample(desc.format, in.data + (size_t)i * sb);
  }
  processor.putSamples(feedBuf.data(), frames);
  return frames * frameBytes;
}

int SoundTouchTempo::receive(AvoxData& out) {
  out = {};
  if (!desc.bValid()) {
    return 0;
  }
  if (outOffset >= outBuf.size()) {
    int32_t sb = sampleBytes(desc.format);
    pullBuf.resize((size_t)kPullFrames * desc.channels);
    int got = (int)processor.receiveSamples(pullBuf.data(), kPullFrames);
    if (got <= 0) {
      return 0;
    }
    outBuf.resize((size_t)got * desc.channels * sb);
    for (int32_t i = 0; i < got * desc.channels; i++) {
      saveSample(desc.format, outBuf.data() + (size_t)i * sb, pullBuf[i]);
    }
    outOffset = 0;
  }
  // 切片按 ≈40ms 口径给设备(设备按字节入队, 无对齐要求)
  size_t remain = outBuf.size() - outOffset;
  size_t slice = remain < (size_t)sliceBytes ? remain : (size_t)sliceBytes;
  out.data = outBuf.data() + outOffset;
  out.size = (int32_t)slice;
  out.bRef = true;
  outOffset += slice;
  return out.size;
}

void SoundTouchTempo::reset() {
  processor.clear();
  outBuf.clear();
  outOffset = 0;
}

int32_t SoundTouchTempo::latencyMs() {
  // SETTING_INITIAL_LATENCY 是 SoundTouch.h 的 #define(非枚举), 展开为设置 ID;
  // 返回值是样本数(非毫秒), 按 desc.sampleRate 折算
  int lat = processor.getSetting(SETTING_INITIAL_LATENCY);
  if (lat <= 0 || desc.sampleRate <= 0) {
    return 0;
  }
  return (int32_t)((int64_t)lat * 1000 / desc.sampleRate);
}

}
