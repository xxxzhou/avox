#pragma once

#include "../AvoxCodec.h"
#include "../AvoxDef.h"
#include "../AvoxVideo.h"

namespace avox {

// 编码用途档(内部概念, 不是用户旋钮): 只决定"用户没显式指定时"各旋钮的缺省取向。
// record = 转码/录制落盘(体积与画质优先)
// stream = 实时推流(WebRTC/直播: 码率是硬约束, 低延迟优先)
enum class EncodeUsage : int32_t {
  record = 0,
  stream = 1,
};

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

}
