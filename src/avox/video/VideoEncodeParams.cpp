#include "VideoEncodeParams.hpp"

#include <cstdio>
#include <cstdlib>

namespace avox {

int32_t getAutoVideoBitrate(const VideoDesc& desc, VCodecId codecId) {
  // 每像素每帧比特: 按分辨率档位给, 高档位小尺寸下信噪比压力更大
  const int64_t pixels = (int64_t)desc.width * desc.height;
  double bpp = 0.12;
  if (pixels <= 640 * 480) {
    bpp = 0.05;
  } else if (pixels <= 1280 * 720) {
    bpp = 0.08;
  } else if (pixels <= 1920 * 1080) {
    bpp = 0.12;
  } else if (pixels <= 3840 * 2160) {
    bpp = 0.25;
  } else {
    bpp = 0.4;
  }
  // H.265 同画质约省 30% 码率
  const double codecFactor = (codecId == VCodecId::h265) ? 0.7 : 1.0;
  double fpsFactor = 1.0;
  if (desc.fps <= 15) {
    fpsFactor = 0.8;
  } else if (desc.fps <= 30) {
    fpsFactor = 1.0;
  } else if (desc.fps <= 60) {
    fpsFactor = 1.2;
  } else {
    fpsFactor = 1.5;
  }
  if (desc.width <= 0 || desc.height <= 0 || desc.fps <= 0) {
    return 2000000;
  }
  int64_t bps =
      (int64_t)(pixels * desc.fps * bpp * codecFactor * fpsFactor * 0.5);
  const int32_t kMinBitrate = 200000;
  const int32_t kMaxBitrate = 100000000;
  if (bps < kMinBitrate) {
    bps = kMinBitrate;
  } else if (bps > kMaxBitrate) {
    bps = kMaxBitrate;
  }
  return (int32_t)bps;
}

bool recordFpsGate(int64_t ptsMs, FpsGateState& state, double maxFps) {
  if (maxFps <= 0) {
    return true;
  }
  // 源无有效时间戳时无法按时间丢帧: 旁路且不动基准(INT64_MIN = AVOX_NOVALID_PTS)
  if (ptsMs == INT64_MIN) {
    return true;
  }
  const double periodMs = 1000.0 / maxFps;
  // 首帧 / pts 回退(seek、换段): 以当前帧重设基准, 否则旧基准会把整段扣光
  if (!state.bKept || ptsMs < state.lastPtsMs) {
    state.bKept = true;
    state.lastPtsMs = ptsMs;
    state.nextPtsMs = (double)ptsMs + periodMs;
    return true;
  }
  state.lastPtsMs = ptsMs;
  // 容差 1ms: 整数毫秒 pts 网格与浮点周期累积存在 <1ms 的拍频差(0.5ms 会
  // 撞上 x.67 相位周期性丢帧, CFR 源每秒丢数帧); 真超频源(pts 差 <周期-1ms)
  // 不受影响
  if ((double)ptsMs + 1.0 < state.nextPtsMs) {
    return false;
  }
  state.nextPtsMs += periodMs;
  // 源慢于目标/长时间无帧: 目标直接追到当前帧之后(避免大间隔时循环累积)
  if (state.nextPtsMs <= (double)ptsMs) {
    state.nextPtsMs = (double)ptsMs + periodMs;
  }
  return true;
}

}
