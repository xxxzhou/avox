#pragma once

#include <cstdint>
#include <vector>

namespace avox {

// EBU R128/ITU-R BS.1770-4 响度计: K 加权 + 400ms(75%重叠)块 + 两级门限
// 只计量不出声; 输入交错 float(-1~1), 参考值已对 ffmpeg ebur128 逐例核对
class LoudnessMeter {
 public:
  LoudnessMeter() = default;
  ~LoudnessMeter() = default;

 private:
  // 双二阶(转置直接II型), 系数按采样率双线性变换设计, 状态在结构内
  struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double z1 = 0.0;
    double z2 = 0.0;
    double process(double x) {
      double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
    void clear() {
      z1 = 0.0;
      z2 = 0.0;
    }
  };

  static constexpr int32_t kHopPerBlock = 4;
  static constexpr int32_t kMaxChannels = 16;
  // BS.1770: 响度 = offset + 10log10(加权均方); 绝对门限 -70 LUFS, 相对门限再减 10 LU
  static constexpr double kLoudnessOffset = -0.691;
  static constexpr double kAbsGateLufs = -70.0;
  static constexpr double kRelGateLu = 10.0;
  int32_t sampleRate = 0;
  int32_t channels = 0;
  int32_t hopSamples = 0;
  int32_t hopCount = 0;
  int32_t hopIndex = 0;
  int32_t hopFilled = 0;
  int32_t hopLen[kHopPerBlock] = {};
  double hopSum[kHopPerBlock][kMaxChannels] = {};
  double channelWeight[kMaxChannels] = {};
  Biquad filters[kMaxChannels][2];
  // 每 100ms 一个块能量(时长片 2h 约 7.2 万项)
  std::vector<float> blockEnergy;
  double rawPeak = 0.0;

 private:
  void designFilters();
  void finishHop();
  void pushBlock();

 public:
  // 采样率/声道数决定 K 加权系数与声道权重; 不支持返回 false(调用方旁路)
  bool init(int32_t sampleRate, int32_t channels);
  void reset();
  // 送入交错 float 样本(切分粒度任意, 结果与切分无关)
  void feed(const float* interleaved, int32_t frames);
  // 门限积分响度 LUFS; 无有效块返回 NaN
  double integratedLufs() const;
  bool hasLoudness() const;
  // 输入域采样峰值(含 LFE: 削顶防护按全部声道算)
  double samplePeak() const { return rawPeak; }
  double samplePeakDbfs() const;
};

}
