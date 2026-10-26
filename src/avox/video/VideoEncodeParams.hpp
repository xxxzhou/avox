#pragma once

#include "../AvoxCodec.h"
#include "../AvoxDef.h"
#include "../AvoxVideo.h"
#include "../AvoxMuxer.h"

namespace avox {

// EncodeUsage 定义在 AvoxMuxer.h(与 EncodeRateMode/EncodePreset/EncodeProfile
// 同址, 随 VideoEncodeParam::usage 走公共录制接口); 本头保留量纲常量与码率/帧率算式。

// 用途档缺省的画质档位(0..100, 越大越清晰、体积越大)。各编码器量纲不同
// (量化参数 0~51、质量 0~100、0.0~1.0 都有), 由各平台实现换算。
// 60 = 本机实测与 libx264 crf23 对齐的那一档(1080p30: MF quality60 得
// 2602kbps@VMAF95.5, x264 crf23 得 820kbps@95.8; 各硬编换算后落同档)
constexpr int32_t kRecordQuality = 60;
constexpr int32_t kStreamQuality = 50;

// 目标码率(bps): 分辨率 × 帧率 × 每像素比特, 再按编码效率折算。
// 码率定档的编码器当目标, 质量定档的只当日志基准。取代原先两处各写一版的
// 算术(FFVEncoder 的 ×3 得 186 Mbps、IOMuxerZM 的 ×8/1000 得 497 Mbps)
AVOX_EXPORT int32_t getAutoVideoBitrate(const VideoDesc& desc, VCodecId codecId);

// 帧率门状态(调用方持有; 新会话/新段须 reset())
struct FpsGateState {
  // 本段是否已有保留帧
  bool bKept = false;
  // 上一输入帧 pts(回退检测)
  int64_t lastPtsMs = 0;
  // 下一保留帧的目标时刻(毫秒, 浮点; 逐帧累积, 不随实际保留帧重置)
  double nextPtsMs = 0;
  void reset() {
    bKept = false;
    lastPtsMs = 0;
    nextPtsMs = 0;
  }
};

// 帧率门(转码腿输出帧率上限): 源帧率不高于上限时一帧不丢, 高于上限按目标
// 时刻累积丢帧(逐帧重置相位会把 60→24 压成 20fps)。maxFps<=0 = 不限。
// pts 非法(INT64_MIN)时旁路; pts 回退(seek/换段)时以当前帧重设基准
AVOX_EXPORT bool recordFpsGate(int64_t ptsMs, FpsGateState& state, double maxFps);

}
