#include "AMediaSource.hpp"

#include "../AvoxCodec.h"
#include "../audio/AudioDecoder.hpp"
#include "../module/AvoxManager.hpp"
#include "../player/AVTrack.hpp"
#include "../video/VideoDecoder.hpp"

namespace avox {

AMediaSource::AMediaSource() {}

AMediaSource::~AMediaSource() { close(); }

void AMediaSource::setUri(const char* url) { uri = url ? url : ""; }

void AMediaSource::setHardDecode(bool bHard) { bHardDecode = bHard; }

void AMediaSource::setIoPlan(IoPlan plan) { ioPlan = plan; }

void AMediaSource::onOptionChange(const char* key, ArgType option) {
  // io.*键由内部ioSource(同样link到上层JsonOption)自行消费,此处留AMediaSource级扩展点
  (void)key;
  (void)option;
}

void AMediaSource::preSeek() {
  bFlushPending.store(true);
  bDiscardPacket.store(true);
}

void AMediaSource::seek(int64_t posMs) {
  if (ioSource) {
    ioSource->seekTo(posMs);
  }
  bDiscardPacket.store(false);
}

bool AMediaSource::open() {
  if (uri.empty()) {
    return false;
  }
  RawSource::open();
  bool bFind = AvoxManager::Get().ioSources.hasObjectId(ioPlan);
  if (!bFind) {
    return false;
  }
  const auto& ioclass = AvoxManager::Get().ioSources.initFunc(ioPlan);
  ioSource = std::unique_ptr<AVSource>(ioclass.initFunc());
  if (!ioSource) {
    return false;
  }
  // 链到上层JsonOption: ioSource作为观察者直接感知选项(含linkOption时的重放)
  if (getLink()) {
    ioSource->linkOption(getLink());
  }
  bOpenVDecoder = false;
  bIoEnd = false;
  ioSource->setObserver(this);
  // 是否禁用音频与视频(直接复用 BaseSource 的 bDisableVideo/bDisableAudio)
  ioSource->disableVideo(bDisableVideo);
  ioSource->disableAudio(bDisableAudio);
  audioFirstPts = AVOX_NOVALID_PTS;
  videoFirstPts = AVOX_NOVALID_PTS;
  bTrackWaitStarted = false;
  bTrackWaitResolved = false;
  bOpen = ioSource->open(uri.c_str());
  return bOpen;
}

void AMediaSource::close() {
  if (ioSource) {
    ioSource->close();
    ioSource.reset();
  }
  if (videoDecoder) {
    videoDecoder->removeObserver(this);
    videoDecoder.reset();
  }
  if (audioDecoder) {
    audioDecoder->removeObserver(this);
    audioDecoder.reset();
  }
  RawSource::close();
}

bool AMediaSource::ioComplete() { return bIoEnd; }

void AMediaSource::onReady() {
  if (!ioSource) {
    return;
  }
  const auto& vTracks = ioSource->getVideoTracks();
  const auto& aTracks = ioSource->getAudioTracks();
  if (!bDisableVideo) {
    bDisableVideo = vTracks.empty();
  }
  if (!bDisableAudio) {
    bDisableAudio = aTracks.empty();
  }
  // 打开音频与视频解码器
  if (!vTracks.empty()) {
    const auto& vTrack = vTracks[0];
    LOGFLF(LogLevel::info, "video desc:", vTrack);
    initVideoDecoder(vTrack.codecId, vTrack.desc, bHardDecode);
  }
  if (!aTracks.empty()) {
    const auto& aTrack = aTracks[0];
    LOGFLF(LogLevel::info, "audio desc:", aTrack);
    initAudioDecoder(aTrack.codecId, aTrack.desc);
  }
}

void AMediaSource::onSyncPts() {}

void AMediaSource::onClose() {
  // 编码可能还没结束
  bIoEnd = true;
  LOGFLF(LogLevel::info, "io close");
}

void AMediaSource::onComplete() {
  // 编码可能还没结束
  bIoEnd = true;
  LOGFLF(LogLevel::info, "io complete");
}

void AMediaSource::onError(AVError error, const char* msg) {
  dispatch(&IRawSourceOb::onError, error, msg);
}

void AMediaSource::onPacket(const AvoxPacket& packet) {
  // seek 前置:IO 线程执行 flush,保证与 decode 同线程不并发
  if (bFlushPending.load()) {
    bFlushPending.store(false);
    if (videoDecoder) {
      videoDecoder->flush();
    }
    if (audioDecoder) {
      audioDecoder->flush();
    }
  }
  // seek 期间丢弃包,避免旧帧污染输出
  if (bDiscardPacket.load()) {
    return;
  }
  // 记录音视频首包到达时间
  if (videoFirstPts == AVOX_NOVALID_PTS &&
      (packet.packtype == (int32_t)PackType::vconfig ||
       packet.packtype == (int32_t)PackType::video)) {
    videoFirstPts = timeStampMS();
  }
  if (audioFirstPts == AVOX_NOVALID_PTS &&
      (packet.packtype == (int32_t)PackType::aconfig ||
       packet.packtype == (int32_t)PackType::audio)) {
    audioFirstPts = timeStampMS();
  }
  // 声明了双轨、且只有一路先到时,检查另一路是否超时
  if (!bTrackWaitResolved && !bDisableVideo && !bDisableAudio) {
    bool bVideoOk = videoFirstPts != AVOX_NOVALID_PTS;
    bool bAudioOk = audioFirstPts != AVOX_NOVALID_PTS;
    if (bVideoOk && bAudioOk) {
      // 都到了,结束等待
      bTrackWaitResolved = true;
    } else if (bVideoOk != bAudioOk) {
      // 仅一路到达,启动/检查等待另一路的超时
      if (!bTrackWaitStarted) {
        trackWaitChecker.reset();
        bTrackWaitStarted = true;
      } else if (trackWaitChecker.timeout()) {
        resolveTrackWaitTimeout();
      }
    }
  }
  // LOGFLF(LogLevel::info, "packet type:", (int32_t)packet.packtype,
  //        " pts:", packet.pts);
  switch (packet.packtype) {
    case (int32_t)PackType::vconfig:
    case (int32_t)PackType::video:
      if (videoDecoder) {
        // PacketBuf buf(packet);
        PacketBufPtr pkt = std::make_shared<PacketBuf>(packet);
        // uint8_t nalu = getNalUnit(VCodecId::h264, packet);
        // log(LogLevel::info, "nalu:", (int32_t)nalu, " size:", packet.data.size);
        if (packet.packtype == (int32_t)PackType::vconfig) {
          videoDecoder->pushConfig(*pkt);
        }
        DecodeResult result = videoDecoder->decoder(pkt);
        if (result == DecodeResult::success && !bOpenVDecoder) {
          LOGFLF(LogLevel::info, "open video decoder success");
          videoDecoder->dispatch(&IVideoDecoderOb::onVideoDesc);
          bOpenVDecoder = true;
        }
      }
      break;
    case (int32_t)PackType::aconfig:
    case (int32_t)PackType::audio:
      if (audioDecoder) {
        // PacketBuf buf(packet);
        // LOGFLF(LogLevel::info, "audio packet:", packet.pts);
        PacketBufPtr pkt = std::make_shared<PacketBuf>(packet);
        if (packet.packtype == (int32_t)PackType::aconfig) {
          audioDecoder->pushConfig(*pkt);
        }
        DecodeResult result = audioDecoder->decoder(pkt);
        if (result == DecodeResult::success && !bOpenADecoder) {
          LOGFLF(LogLevel::info, "open audio decoder success");
          bOpenADecoder = true;
        }
      }
      break;
  }
}

bool AMediaSource::initVideoDecoder(VCodecId codecId, const VideoDesc& srcDesc,
                                    bool bHard) {
  if (!AvoxManager::Get().vDecoders.hasObjectId(codecId)) {
    LOGFLF(LogLevel::warn, "no find video codec:", codecId);
    return false;
  }
  const auto& decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.empty()) {
    LOGFLF(LogLevel::warn, "no video codec:", codecId, " init");
    return false;
  }
  const char* sName = getDefaultDecoderName(codecId, bHard);
  size_t sIndex = 0;
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto& vDecode = decodes[sIndex];
  videoDecoder = std::unique_ptr<VideoDecoder>(vDecode.initFunc());
  if (!videoDecoder) {
    LOGFLF(LogLevel::warn, "no create video codec:", codecId);
    return false;
  }
  videoDecoder->setObserver(this);
  if (!videoDecoder->setContext(vDecode.desc, srcDesc)) {
    LOGFLF(LogLevel::warn, "video codec:", codecId, " set desc:", srcDesc,
           " failed");
    videoDecoder.reset();
    if (bHard) {
      return initVideoDecoder(codecId, srcDesc, false);
    }
    return false;
  }
  LOGFLF(LogLevel::info, "create video decode success");
  return true;
}

bool AMediaSource::initAudioDecoder(ACodecId codecId,
                                    const AudioDesc& srcDesc) {
  if (!AvoxManager::Get().aDecoders.hasObjectId(codecId)) {
    LOGFLF(LogLevel::warn, "no find audio codec:", codecId);
    return false;
  }
  const auto& decodes = AvoxManager::Get().aDecoders.initFuncs(codecId);
  if (decodes.empty()) {
    LOGFLF(LogLevel::warn, "no audio codec:", codecId, " init");
    return false;
  }
  size_t sIndex = 0;
  if (codecId == ACodecId::aac) {
    const char* sName = "fdk-aac decoder";
    for (size_t i = 0; i < decodes.size(); ++i) {
      if (decodes[i].desc.name == sName) {
        sIndex = i;
        break;
      }
    }
  }
  auto& aDecode = decodes[sIndex];
  audioDecoder = std::unique_ptr<AudioDecoder>(aDecode.initFunc());
  if (!audioDecoder) {
    LOGFLF(LogLevel::warn, "no create audio codec:", codecId);
    return false;
  }
  audioDecoder->setObserver(this);
  if (!audioDecoder->setContext(aDecode.desc, srcDesc)) {
    LOGFLF(LogLevel::warn, "audio codec:", codecId, " set desc:", srcDesc,
           " failed");
    audioDecoder.reset();
    return false;
  }
  LOGFLF(LogLevel::info, "create audio decode success");
  return true;
}

void AMediaSource::onVideoDesc() {
  const auto& vTracks = ioSource->getVideoTracks();
  if (!vTracks.empty()) {
    auto vTrack = vTracks[0];
    const DecoderParams& dparams = videoDecoder->getDecoderParams();
    vTrack.desc.fps = dparams.fps;
    setVideoDesc(vTrack.desc);
    LOGFLF(LogLevel::info, vTrack);
  }
}

void AMediaSource::onPacket(PacketBufPtr packet) {}

void AMediaSource::onDecode(const YUVFrame& frame) { pushFrame(frame); }

void AMediaSource::onDecodeGpu(const GpuFrame& frame) { pushFrame(frame); }

void AMediaSource::onVideoComplete() {}

void AMediaSource::onAudioDesc() {
  // 在这才知道解码后的输出格式
  AudioDesc decodeDesc = audioDecoder->getOutDesc();
  setAudioDesc(decodeDesc);
  LOGFLF(LogLevel::info, decodeDesc);
}

void AMediaSource::onDecode(const AvoxAFrame& frame) { pushFrame(frame); }

void AMediaSource::onAudioComplete() {}

void AMediaSource::resolveTrackWaitTimeout() {
  if (bTrackWaitResolved) {
    return;
  }
  bTrackWaitResolved = true;
  // 视频已到、音频没到 -> 关闭音频解码器,标记无音频,让录制继续
  if (videoFirstPts != AVOX_NOVALID_PTS && audioFirstPts == AVOX_NOVALID_PTS) {
    LOGFLF(LogLevel::warn,
           "audio track declared but no packet arrived, drop audio track");
    if (audioDecoder) {
      audioDecoder->removeObserver(this);
      audioDecoder.reset();
    }
    bDisableAudio = true;
  }
  // 音频已到、视频没到 -> 关闭视频解码器,标记无视频
  else if (audioFirstPts != AVOX_NOVALID_PTS &&
           videoFirstPts == AVOX_NOVALID_PTS) {
    LOGFLF(LogLevel::warn,
           "video track declared but no packet arrived, drop video track");
    if (videoDecoder) {
      videoDecoder->removeObserver(this);
      videoDecoder.reset();
    }
    bDisableVideo = true;
  }
  // 降级后重新检查轨道就绪,触发 onReady(否则录制器仍卡在等待)
  checkTrackReady();
}

}
