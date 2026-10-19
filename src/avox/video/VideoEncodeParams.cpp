#include "VideoEncodeParams.hpp"

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

}
