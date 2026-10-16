#include <atomic>
#include <cstdio>
#include <cstdlib>
#include "VideoStream.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/HighClock.hpp"
#include "RawMuxer.hpp"

namespace avox {

VideoStream::VideoStream() {
// #if WIN32
//   bHardEncoder = false;
// #endif
}

VideoStream::~VideoStream() { stopEncoder(); }

void VideoStream::stopEncoder() {
  if (encoder()) {
    // 排空: 硬编 CompleteFrames 返回前把待出帧全回调完(sink 尚活时写出尾帧)
    encoder()->flush();
  }
  encoderChain.reset(*this);
  // 断链: 之后任何迟到包判空返回, 不再碰已释放的 muxer
  muxer = nullptr;
}

void VideoStream::setHardEncode(bool bHard) { bHardEncoder = bHard; }

bool VideoStream::getHardEncode() { return bHardEncoder; }

void VideoStream::setVideoDesc(const VTrackDesc& desc_) {
  desc = desc_;
  // 候选 = 偏好极默认编码器名 → 注册表全量; 上层"软/硬偏好"只决定谁先试
  if (!encoderChain.build(desc.codecId, bHardEncoder)) {
    LOGFLF(LogLevel::warn, "not find encoder codecId:",
           getVCodecName(desc.codecId));
    return;
  }
  if (!encoderChain.selectFirst(*this)) {
    LOGFLF(LogLevel::warn, "no encoder lane available:",
           getVCodecName(desc.codecId));
    return;
  }
  LOGFLF(LogLevel::info, "init encoder:", encoderChain.implName(),
         " codecId:", getVCodecName(desc.codecId));
}

// 车道接线: 编码器只有 desc 与观察者要接, 开不开得起来要到首帧才知道
bool VideoStream::laneInit(VideoEncoder& impl, const VCodecDesc&) {
  impl.setDesc(desc);
  impl.setObserver(this);
  return true;
}

void VideoStream::laneClose(VideoEncoder& impl) { impl.removeObserver(this); }

void VideoStream::laneSaveConfigs(VideoEncoder& oldImpl) {}

// 换道后: 本车道重新开始记"是否出过包"; 首帧前的选型不动用一个在跑的编码器
void VideoStream::laneChanged(VideoEncoder& newImpl, bool bFirst) {
  bEmittedPacket = false;
  bWarnedEncDead = false;
  if (bFirst) {
    return;
  }
  LOGFLF(LogLevel::warn, "video encoder fallback to:", encoderChain.implName());
}

template <typename TFrame>
void VideoStream::encodeFrameImpl(const TFrame& frame) {
  if (!encoder()) {
    LOGFLF(LogLevel::warn, "not find encoder");
    return;
  }
  // HighClock clock = {};
  const DecodeResult result = encoder()->encode(frame);
  // log(LogLevel::debug, "encoder frame:", clock.recordDelta());
  if (result != DecodeResult::openFailed) {
    return;
  }
  // 首帧前打不开(如硬编建得起但 onPreEncoder 失败): 换同编码的下一条候选重编这一帧
  // —— 此刻一个包都没出过, 封装头与编码描述都还没落, 换道安全。旧实现把 encode() 的
  // 返回值直接丢掉, 硬编开不了 = 整片无视频且零日志
  if (bEmittedPacket) {
    return;
  }
  if (encoderChain.selectNext(*this)) {
    encoder()->encode(frame);
    return;
  }
  if (!bWarnedEncDead) {
    bWarnedEncDead = true;
    LOGFLF(LogLevel::error, "video encoder can not open:",
           encoderChain.implName(), " codec:", getVCodecName(desc.codecId));
  }
}

void VideoStream::encoderFrame(const YUVFrame& frame) { encodeFrameImpl(frame); }

void VideoStream::encoderFrame(const GpuFrame& frame) { encodeFrameImpl(frame); }

void VideoStream::onPacket(AvoxPacket& packet) {
  // [dbg] ENH_VKDBG=1: 编码器包回调
  {
    static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
    static std::atomic<int32_t> cnt{0};
    if (dbg && cnt.fetch_add(1) < 5) {
      fprintf(stderr, "[dbg] VideoStream onPacket, size=%u muxer=%p\n",
              (unsigned)packet.data.size, (void*)muxer);
    }
  }
  // 出了包 = 本车道确实在编码, 之后不再换道(封装头已定)
  bEmittedPacket = true;
  if (!muxer) {
    LOGFLF(LogLevel::warn, "muxer is null");
    return;
  }
  // uint8_t nalUnit = getNalUnit(desc.codecId, packet);
  muxer->pushPacket(packet);
}

}
