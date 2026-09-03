#include "VideoEncoder.hpp"

namespace avox {

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {}

void VideoEncoder::setDesc(const VTrackDesc& desc_) {
  desc = desc_;
  if (desc.desc.fps == 0) {
    desc.desc.fps = 30;
  }
  if (bitrate == 0) {
    bitrate = getAutoBitrate();
  }
}

void VideoEncoder::setBitrate(int32_t bitrate_) { bitrate = bitrate_; }

int32_t VideoEncoder::getAutoBitrate() {
  // 如果desc未设置，返回默认比特率
  if (desc.desc.width == 0 || desc.desc.height == 0 || desc.desc.fps == 0) {
    return 2000000;  // 默认2Mbps
  }
  // 计算视频像素总数
  int32_t pixelCount = desc.desc.width * desc.desc.height;
  // 根据分辨率和帧率计算基础比特率
  // 基础公式：比特率 = 像素数 × 帧率 × 复杂度因子
  // 复杂度因子根据分辨率调整
  double complexityFactor = 0.0;
  if (pixelCount <= 640 * 480) {  // 标清
    complexityFactor = 0.05;
  } else if (pixelCount <= 1280 * 720) {  // 720p
    complexityFactor = 0.08;
  } else if (pixelCount <= 1920 * 1080) {  // 1080p
    complexityFactor = 0.12;
  } else if (pixelCount <= 3840 * 2160) {  // 4K
    complexityFactor = 0.25;
  } else {  // 8K及以上
    complexityFactor = 0.4;
  }
  // 根据编码器类型调整复杂度
  double codecFactor = 1.0;
  switch (desc.codecId) {
    case VCodecId::h264:
      codecFactor = 1.0;  // H.264标准复杂度
      break;
    case VCodecId::h265:
      codecFactor = 0.7;  // H.265更高效，需要更低的比特率
      break;
    default:
      codecFactor = 1.0;
      break;
  }
  // 计算基础比特率（单位：bps）
  double baseBitrate =
      pixelCount * desc.desc.fps * complexityFactor * codecFactor;
  // 根据帧率进一步调整
  double fpsFactor = 1.0;
  if (desc.desc.fps <= 15) {
    fpsFactor = 0.8;
  } else if (desc.desc.fps <= 30) {
    fpsFactor = 1.0;
  } else if (desc.desc.fps <= 60) {
    fpsFactor = 1.2;
  } else {
    fpsFactor = 1.5;  // 高帧率需要更高比特率
  }
  // 最终比特率计算
  int32_t finalBitrate = static_cast<int32_t>(baseBitrate * fpsFactor * 0.5);
  // 设置比特率范围限制
  const int32_t MIN_BITRATE = 200000;     // 最小500kbps
  const int32_t MAX_BITRATE = 100000000;  // 最大100Mbps
  if (finalBitrate < MIN_BITRATE) {
    finalBitrate = MIN_BITRATE;
  } else if (finalBitrate > MAX_BITRATE) {
    finalBitrate = MAX_BITRATE;
  }
  return finalBitrate;
}

}