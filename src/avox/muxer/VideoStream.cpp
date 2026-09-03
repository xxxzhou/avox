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

VideoStream::~VideoStream() {}

void VideoStream::setHardEncode(bool bHard) { bHardEncoder = bHard; }

bool VideoStream::getHardEncode() { return bHardEncoder; }

void VideoStream::setVideoDesc(const VTrackDesc& desc_) {
  desc = desc_;
  bool bFind = AvoxManager::Get().vEncoders.hasObjectId(desc.codecId);
  if (!bFind) {
    LOGFLF(LogLevel::warn, "not find codecId:", getVCodecName(desc.codecId));
    return;
  }
  const auto& encodes = AvoxManager::Get().vEncoders.initFuncs(desc.codecId);
  if (encodes.empty()) {
    LOGFLF(LogLevel::warn, "not find decoder:", getVCodecName(desc.codecId));
    return;
  }
  // 根据播放器设置选择解码器
  size_t sIndex = 0;
  const char* sName = getDefaultEncoderName(desc.codecId, bHardEncoder);
  // 查找解码器
  for (size_t i = 0; i < encodes.size(); ++i) {
    if (encodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto& vEncode = encodes[sIndex];
  // 初始化解码器
  encoder = std::unique_ptr<VideoEncoder>(vEncode.initFunc());
  if (!encoder) {
    LOGFLF(LogLevel::warn, "init encoder failed:", sName);
    return;
  }
  LOGFLF(LogLevel::info, "init encoder:", vEncode.desc.name,
         " codecId:", getVCodecName(desc.codecId));
  encoder->setDesc(desc);
  encoder->setObserver(this);
}

void VideoStream::encoderFrame(const YUVFrame& frame) {
  if (!encoder) {
    LOGFLF(LogLevel::warn, "not find encoder");
    return;
  }
  // HighClock clock = {};
  encoder->encode(frame);
  // log(LogLevel::debug, "encoder frame:", clock.recordDelta());
}

void VideoStream::encoderFrame(const GpuFrame& frame) {
  if (!encoder) {
    LOGFLF(LogLevel::warn, "not find encoder");
    return;
  }
  encoder->encode(frame);
}

void VideoStream::onPacket(AvoxPacket& packet) {
  if (!muxer) {
    LOGFLF(LogLevel::warn, "muxer is null");
    return;
  }
  // uint8_t nalUnit = getNalUnit(desc.codecId, packet);
  muxer->pushPacket(packet);
}

}
