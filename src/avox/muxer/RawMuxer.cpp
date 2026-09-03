#include "RawMuxer.hpp"

#include "../module/AvoxManager.hpp"
#include "../source/AVSource.hpp"

namespace avox {

RawMuxer::RawMuxer() {
  // 音频流，编码PCM到ACC包
  audioStream = std::make_unique<AudioStream>();
  // 视频流，编码YUV到H264/H265包
  videoStream = std::make_unique<VideoStream>();
  bDisableAudio = false;
}

RawMuxer::~RawMuxer() { close(); }

bool RawMuxer::getHardEncode() { return videoStream->getHardEncode(); }

void RawMuxer::setHardEncode(bool bHard) { videoStream->setHardEncode(bHard); }

void RawMuxer::setVideoCodec(VCodecId codecId) {
  vCodecId = codecId;
  if (codecId == VCodecId::none) {
    bDisableVideo = true;
    LOGFLF(LogLevel::info, "video disabled (codec=none)");
  }
}
void RawMuxer::setAudioCodec(ACodecId codecId) {
  aCodecId = codecId;
  if (codecId == ACodecId::none) {
    bDisableAudio = true;
    LOGFLF(LogLevel::info, "audio disabled (codec=none)");
  }
}

void RawMuxer::setAudioDesc(const AudioDesc& desc) {
  bReAudio = true;
  outADesc = desc;
}

void RawMuxer::setInVideoDesc(const VTrackDesc& desc) {
  if (!ioMuxer) {
    LOGFLF(LogLevel::warn, "ioMuxer is null");
    return;
  }
  // 用户设 none = 丢弃视频轨(不论源 desc 带不带编码, 本层兜底)
  if (vCodecId == VCodecId::none) {
    bDisableVideo = true;
    LOGFLF(LogLevel::info, "video discarded (codec=none)");
    return;
  }
  videoStream->setRawMuxer(this);
  VTrackDesc vdesc = desc;
  // 一般来说，设备是直出，desc没有编码信息,需要设置
  if (vdesc.codecId == VCodecId::none) {
    vdesc.codecId = vCodecId;
  }
  // 如果没有设置，软编默认都使用YUV420P，硬编默认NV12
  if (vdesc.desc.type == YuvType::other) {
    LOGFLF(LogLevel::info, "yuv type is other");
  }
  // 如果有FPS,但是没有设置，默认25，但是其可能导致变快或是变慢
  if (vdesc.desc.fps == 0) {
    vdesc.desc.fps = 25;
  }
  videoStream->setVideoDesc(vdesc);
  ioMuxer->setVideoDesc(vdesc);
  vcodecId = vdesc.codecId;
  bHaveVideo = true;
}

void RawMuxer::setInAudioDesc(const ATrackDesc& desc) {
  // 用户设 none = 丢弃音频轨(本层兜底)
  if (aCodecId == ACodecId::none) {
    bDisableAudio = true;
    LOGFLF(LogLevel::info, "audio discarded (codec=none)");
    return;
  }
  audioStream->setRawMuxer(this);
  ATrackDesc adesc = desc;
  // 一般来说，设备是直出，不会设置编码信息
  if (adesc.codecId == ACodecId::none) {
    adesc.codecId = aCodecId;
  }
  AudioDesc tDesc = adesc.desc;
  if (bReAudio) {
    tDesc = outADesc;
  }
  // 如果启用了编码器，可能会改变输入音频信息
  ATrackDesc odesc = audioStream->setAudioDesc(adesc, outADesc);
  ioMuxer->setAudioDesc(odesc);
  bHaveAudio = true;
}

void RawMuxer::onOpen() {}

void RawMuxer::onClose() { bReAudio = false; }

void RawMuxer::pushFrame(const YUVFrame& frame) {
  if (bDisableVideo) {
    return;
  }
  if (state != RecorderState::recording) {
    LOGFLF(LogLevel::warn, "muxer is not recording");
    return;
  }
  // LOGFLF(LogLevel::info, "video pts:", frame.pts);
  videoStream->encoderFrame(frame);
}

void RawMuxer::pushFrame(const GpuFrame& frame) {
  if (bDisableVideo) {
    return;
  }
  if (state != RecorderState::recording) {
    LOGFLF(LogLevel::warn, "muxer is not recording");
    return;
  }
  videoStream->encoderFrame(frame);
}

void RawMuxer::pushFrame(const AvoxAFrame& frame) {
  if (bDisableAudio) {
    return;
  }
  if (state != RecorderState::recording) {
    LOGFLF(LogLevel::warn, "muxer is not recording");
    return;
  }
  // LOGFLF(LogLevel::info, "audio pts:", frame.pts);
  audioStream->encoderFrame(frame);
}

}
