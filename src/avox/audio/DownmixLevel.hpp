#pragma once

#include <cmath>

#include "../AvoxAudio.h"

namespace avox {

// 下混整体音量下限(防止 0 音量被当成异常值)
constexpr float kDownmixMinVolume = 0.0f;

// dB → 线性增益。注意音频库(swresample)的 -3dB 常量取 M_SQRT1_2=0.70711,
// 本式计算 10^(-3/20)=0.70795, 相差 0.01dB; none 档不干预才能逐位等价历史行为
inline float downmixDbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

// 增益 → 线性; <= -60dB 统一视为不混入(线性 0), 免得留 -60dB 的不可闻残量
inline float downmixGainToLinear(float db) {
  return db <= -60.0f ? 0.0f : downmixDbToLinear(db);
}

// 预设 → 线性混音系数: 中置/环绕/LFE 与整体音量。
// 返回 false = 不干预(none), 调用方保持音频库默认(逐位等价历史行为)
inline bool resolveDownmixLevels(const DownmixParamet& paramet, float& center,
                                 float& surround, float& lfe, float& volume) {
  float centerDb = paramet.centerGainDb;
  float surroundDb = paramet.surroundGainDb;
  float lfeDb = paramet.lfeGainDb;
  switch (paramet.preset) {
    case DownmixPreset::none:
      return false;
    case DownmixPreset::itu:  // BS.775: 中置/环绕 -3dB, LFE 丢弃
      centerDb = -3.0f;
      surroundDb = -3.0f;
      lfeDb = -60.0f;
      break;
    case DownmixPreset::dolby:  // 中置/环绕 -3dB, LFE 保留 -3dB
      centerDb = -3.0f;
      surroundDb = -3.0f;
      lfeDb = -3.0f;
      break;
    case DownmixPreset::custom:
    default:
      break;
  }
  // <= -60dB 统一视为丢弃; 音频库用 0 线性值表示不混入
  center = downmixGainToLinear(centerDb);
  surround = downmixGainToLinear(surroundDb);
  lfe = downmixGainToLinear(lfeDb);
  volume = paramet.volume > kDownmixMinVolume ? paramet.volume : kDownmixMinVolume;
  return true;
}

}
