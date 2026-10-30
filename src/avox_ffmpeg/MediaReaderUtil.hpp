#pragma once

// IMediaReader 纯逻辑件: 状态迁移表/URI 判定/旋转归一/峰值扫描/桶映射。
// 不依赖 FFmpeg, 供 MediaReaderFF 与 avox_tests 共用(峰值算法与 ak_index 同源)

#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "avox/AvoxMedia.h"
#include "avox/AvoxAudio.h"

namespace avox {

// 状态迁移表(与 AvoxMedia.h 状态机契约一致); 非法迁移一律 false
inline bool mediaReaderCanTransit(MediaReaderState from, MediaReaderState to) {
  switch (from) {
    case MediaReaderState::idle:
      return to == MediaReaderState::opening;
    case MediaReaderState::opening:
      return to == MediaReaderState::ready || to == MediaReaderState::idle;
    case MediaReaderState::ready:
      return to == MediaReaderState::indexing ||
             to == MediaReaderState::grabbing ||
             to == MediaReaderState::closing;
    case MediaReaderState::indexing:
    case MediaReaderState::grabbing:
      return to == MediaReaderState::ready || to == MediaReaderState::closing;
    case MediaReaderState::closing:
      return to == MediaReaderState::idle;
    default:
      return false;
  }
}

// v1 只收本地: 有合法 scheme 即网络源(file:// 除外); UNC/映射盘路径放行。
// 不用 checkLocalPath——它把 \\server\share 也当网络拒掉, 与 v1 承诺冲突
inline bool isNetworkUri(const char* uri) {
  if (!uri) {
    return false;
  }
  const std::string url = uri;
  const size_t pos = url.find("://");
  if (pos == std::string::npos || pos == 0) {
    return false;
  }
  if (!std::isalpha(static_cast<unsigned char>(url[0]))) {
    return false;
  }
  for (size_t i = 1; i < pos; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') {
      return false;
    }
  }
  std::string scheme = url.substr(0, pos);
  for (char& ch : scheme) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return scheme != "file";
}

// display matrix(9 个 16.16 定点数) -> 顺时针显示角(0/90/180/270),
// 与 ffmpeg get_rotation 同口径: theta=atan2(m1,m0), 等比缩放不影响幅角。
// 镜像矩阵(det<0)与非 90 度整数倍 v1 不转, 记 0
inline int32_t displayMatrixRotation(const int32_t* matrix) {
  constexpr double kRad2Deg = 57.295779513082320876;
  if (!matrix) {
    return 0;
  }
  // 镜像矩阵(det<0)不做: 纯翻转常配 180 角, 按角度转会转错方向
  const double det =
      (double)matrix[0] * matrix[4] - (double)matrix[1] * matrix[3];
  if (det < 0) {
    return 0;
  }
  double theta = std::atan2((double)matrix[1], (double)matrix[0]) * kRad2Deg;
  theta = std::fmod(theta, 360.0);
  if (theta < 0) {
    theta += 360.0;
  }
  const double angle = std::round(theta / 90.0) * 90.0;
  if (std::fabs(theta - angle) > 2.0) {
    return 0;
  }
  const int32_t ret = (int32_t)angle;
  return ret >= 360 ? 0 : ret;
}

template <typename T>
inline double mediaScanSamples(const uint8_t* data, int32_t count,
                               double scale) {
  const T* samples = reinterpret_cast<const T*>(data);
  double peak = 0.0;
  for (int32_t i = 0; i < count; ++i) {
    const double value = std::fabs(static_cast<double>(samples[i]) * scale);
    if (value > peak) {
      peak = value;
    }
  }
  return peak;
}

// 逐样本绝对值最大, 返回线性幅度 [0,1](>1 钳到 1)。
// planar: data 按面排, 每面 samples 个样本; interleaved: 单面 samples*channels。
// s64 与 ak_index 同口径不扫描(计 0)
inline double mediaFramePeak(const uint8_t* const* data, int32_t channels,
                             int32_t samples, AudioFormat fmt) {
  int32_t bytes = 0;
  bool planar = false;
  switch (fmt) {
#define XX(name, value, size, plane, str) \
  case AudioFormat::name: bytes = size; planar = plane != 0; break;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return 0.0;
  }
  if (bytes <= 0 || samples <= 0 || channels <= 0 || !data || !data[0]) {
    return 0.0;
  }
  const int32_t planes = planar ? channels : 1;
  const int32_t count = planar ? samples : samples * channels;
  double peak = 0.0;
  for (int32_t plane = 0; plane < planes; ++plane) {
    const uint8_t* cur = data[plane];
    if (!cur) {
      continue;
    }
    double value = 0.0;
    switch (fmt) {
      case AudioFormat::AVOX_AUDIO_U8:
      case AudioFormat::AVOX_AUDIO_U8P:
        for (int32_t i = 0; i < count; ++i) {
          const double v = std::fabs((static_cast<int>(cur[i]) - 128) / 128.0);
          if (v > value) {
            value = v;
          }
        }
        break;
      case AudioFormat::AVOX_AUDIO_S16:
      case AudioFormat::AVOX_AUDIO_S16P:
        value = mediaScanSamples<int16_t>(cur, count, 1.0 / 32768.0);
        break;
      case AudioFormat::AVOX_AUDIO_S32:
      case AudioFormat::AVOX_AUDIO_S32P:
        value = mediaScanSamples<int32_t>(cur, count, 1.0 / 2147483648.0);
        break;
      case AudioFormat::AVOX_AUDIO_FLT:
      case AudioFormat::AVOX_AUDIO_FLTP:
        value = mediaScanSamples<float>(cur, count, 1.0);
        break;
      case AudioFormat::AVOX_AUDIO_DBL:
      case AudioFormat::AVOX_AUDIO_DBLP:
        value = mediaScanSamples<double>(cur, count, 1.0);
        break;
      default:
        break;
    }
    if (value > peak) {
      peak = value;
    }
  }
  return peak > 1.0 ? 1.0 : peak;
}

// (ms, 线性峰值) 逐帧列表映射到桶: 桶内取线性最大(不开方; shim 统一
// sqrt(p)*100, 与逐帧 sqrt 后取桶最大等价)。duration<=0 时全 0(与 ak_index
// 一致: peaks 数组仍非空)
inline void mapPeaksToBuckets(
    const std::vector<std::pair<int64_t, double>>& frames, int32_t buckets,
    int64_t durationMs, std::vector<float>& out) {
  out.assign(buckets > 0 ? static_cast<size_t>(buckets) : 0u, 0.0f);
  if (buckets <= 0 || durationMs <= 0) {
    return;
  }
  for (const auto& frame : frames) {
    if (frame.first < 0) {
      continue;
    }
    int64_t index = frame.first * buckets / durationMs;
    if (index < 0) {
      index = 0;
    }
    if (index >= buckets) {
      index = buckets - 1;
    }
    const float v = static_cast<float>(frame.second);
    if (v > out[static_cast<size_t>(index)]) {
      out[static_cast<size_t>(index)] = v;
    }
  }
}

}  // namespace avox
