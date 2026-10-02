#pragma once

#include <cstdint>
#include <vector>

#include "../AvoxAudio.h"
#include "LoudnessMeter.hpp"

namespace avox {

// 播放响度均衡: BS.1770 计量 + 慢收敛增益 + 逐样本峰值护栏
// 目标=统一响度(默认-18 LUFS); 增益在用户音量之前乘, 不改 setVolume/getVolume 语义
class AudioLeveler {
 public:
  AudioLeveler() = default;
  ~AudioLeveler() = default;

 private:
  // 目标响度与增益护栏(超出不追, 防病态素材大幅拉偏; 老片/现代片落差 ~13LU 在界内)
  static constexpr double kTargetLufs = -18.0;
  static constexpr double kMinGainDb = -24.0;
  static constexpr double kMaxGainDb = 18.0;
  // 上升 ≤3dB/s(听感无抽气), 下降 ≤20dB/s(防削顶的保护性回落)
  static constexpr double kRiseDbPerSec = 3.0;
  static constexpr double kFallDbPerSec = 20.0;
  // 峰值护栏: 输出采样峰值不超 -1dBFS(采样峰值口径, 真峰升级见后续)
  static constexpr double kPeakCeil = 0.891;
  AudioDesc desc = {};
  double targetLufs = kTargetLufs;
  bool bValid = false;
  double gainDb = 0.0;
  LoudnessMeter meter;
  // 解包的交错 float 与输出缓冲(输出不写回调用方缓冲: 相机录制复用同一原始帧)
  std::vector<float> scratch;
  std::vector<uint8_t> outBuffer;

 private:
  // 解包成交错 float; 格式不支持返回 false
  bool unpack(const uint8_t* src, int32_t frames);
  // 按 from→to 帧内线性过渡应用增益, 逐帧峰值护栏; 格式不支持返回 false
  bool applyTo(uint8_t* dst, int32_t frames, double fromDb, double toDb);

 public:
  // desc 采样率/声道/格式不支持返回 false(调用方旁路, 宁可不处理)
  bool init(const AudioDesc& desc, double targetLufs = kTargetLufs);
  void reset();
  // 处理一帧 PCM: 有改动返回 true, out 指向内部缓冲(生命周期到下次调用)
  bool process(const uint8_t* data, int32_t size, AvoxData& out);
  // 当前生效增益(测试/排障)
  double getGainDb() const { return gainDb; }
};

}
