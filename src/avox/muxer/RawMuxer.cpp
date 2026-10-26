#include <atomic>
#include <cstdio>
#include <cstdlib>
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

// 析构先停编码器: MediaMuxer::close 已放 sink 时也要保证编码器先死,
// 否则硬编回调(异步)会在成员析构途中打到已释放的队列
RawMuxer::~RawMuxer() {
  stopStreams();
  close();
}

void RawMuxer::stopStreams() {
  if (videoStream) videoStream->stopEncoder();
  if (audioStream) audioStream->stopEncoder();
}

bool RawMuxer::getHardEncode() { return videoStream->getHardEncode(); }

void RawMuxer::setHardEncode(bool bHard) { videoStream->setHardEncode(bHard); }

void RawMuxer::setEncodeParam(const VideoEncodeParam& param) {
  videoStream->setEncodeParam(param);
}

void RawMuxer::setVideoEncodeParam(const VideoEncodeParam& param) {
  setEncodeParam(param);
}

void RawMuxer::setVideoDesc(const VideoDesc& desc) {
  // open 前契约: 开录后改会让流头/编码器描述与门限/守卫不一致, 等于现场切断画面
  if (state == RecorderState::opening || state == RecorderState::recording) {
    LOGFLF(LogLevel::warn, "setVideoDesc ignored while recording");
    return;
  }
  outVideoDesc = desc;
  bSetOutSize = desc.width > 0 && desc.height > 0;
  // 新规格 = 新段: 帧率门与尺寸告警从当前 pts 重起
  fpsGate.reset();
  sizeMismatchFrames = 0;
  bWarnedSizeMismatch = false;
}

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
  // 声明的输出规格(投屏转码腿): 尺寸的实际像素由渲染链 enableSizeChange 缩;
  // fps 必须同步给编码器(自动码率/GOP/VUI 都按它算)
  if (bSetOutSize) {
    vdesc.desc.width = outVideoDesc.width;
    vdesc.desc.height = outVideoDesc.height;
  }
  if (outVideoDesc.fps > 0) {
    vdesc.desc.fps = outVideoDesc.fps;
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

void RawMuxer::onClose() {
  // MediaMuxer::close 在 onClose 之后才释放 sink(ioMuxer): 此处先停编码器,
  // 待出帧趁 sink 尚活排空落盘, 且关流后不再有回调能触到 sink
  stopStreams();
  bReAudio = false;
  // 会话结束清转码腿状态: RawMuxer 随 MediaPlayer 长期复用, 残留的 pts 基准
  // 会把下一段(pts 从头开始)的整段视频扣光, 残留尺寸会污染普通录制
  outVideoDesc = {};
  bSetOutSize = false;
  fpsGate.reset();
  sizeMismatchFrames = 0;
  bWarnedSizeMismatch = false;
}

void RawMuxer::pushFrame(const YUVFrame& frame) {
  // [dbg] ENH_VKDBG=1: 前几个视频帧进入编码链路
  {
    static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
    static std::atomic<int32_t> cnt{0};
    if (dbg && cnt.fetch_add(1) < 3) {
      fprintf(stderr, "[dbg] RawMuxer pushFrame video, state=%d %ux%u\n",
              (int)state, (unsigned)frame.format.width,
              (unsigned)frame.format.height);
    }
  }
  if (bDisableVideo) {
    return;
  }
  if (state != RecorderState::recording) {
    LOGFLF(LogLevel::warn, "muxer is not recording");
    return;
  }
  // 尺寸守卫在帧率门之前: 腿没缩放的帧不该消耗门的保留槽位
  if (!checkFrameSize((int32_t)frame.format.width,
                      (int32_t)frame.format.height)) {
    return;
  }
  // 帧率门(声明了输出 fps 才生效): 超频帧按 pts 丢, 音频不动
  if (!recordFpsGate(frame.pts, fpsGate, outVideoDesc.fps)) {
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
  // 尺寸守卫在帧率门之前: 腿没缩放的帧不该消耗门的保留槽位
  if (!checkFrameSize((int32_t)frame.format.width,
                      (int32_t)frame.format.height)) {
    return;
  }
  if (!recordFpsGate(frame.pts, fpsGate, outVideoDesc.fps)) {
    return;
  }
  videoStream->encoderFrame(frame);
}

bool RawMuxer::checkFrameSize(int32_t width, int32_t height) {
  if (!bSetOutSize) {
    return true;
  }
  if (width == outVideoDesc.width && height == outVideoDesc.height) {
    sizeMismatchFrames = 0;
    bWarnedSizeMismatch = false;
    return true;
  }
  // 不符即丢(防流头/编码器描述与实际帧错位); 连续 3 帧不符才升级告警,
  // 首帧/换图的一两帧瞬态不误报
  ++sizeMismatchFrames;
  if (sizeMismatchFrames >= 3 && !bWarnedSizeMismatch) {
    bWarnedSizeMismatch = true;
    LOGFLF(LogLevel::error, "frame size:", width, "x", height,
           "!= declared:", outVideoDesc.width, "x", outVideoDesc.height,
           "frames dropped; render lane cannot resize (canSizeChange=false)");
  }
  return false;
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
