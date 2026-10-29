#include "AMediaSource.hpp"

#include <cstring>

#include "../AvoxCodec.h"
#include "../audio/AudioDecoder.hpp"
#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"
#include "../module/OptionKey.hpp"
#include "../player/AVTrack.hpp"
#include "../video/VideoDecoder.hpp"

namespace avox {

AMediaSource::AMediaSource() {}

AMediaSource::~AMediaSource() { close(); }

void AMediaSource::setUri(const char* url) { uri = url ? url : ""; }

void AMediaSource::setHardDecode(bool bHard) { bHardDecode = bHard; }

void AMediaSource::setIoPlan(IoPlan plan) { ioPlan = plan; }

void AMediaSource::onOptionChange(const char* key, ArgType option) {
  // io.*键由内部ioSource(同样link到上层JsonOption)自行消费
  if (equalsIgnoreCase(key, AVOX_MP_VIDEO_DECODER_NAME_STR)) {
    videoDecoderName = getLink()->getString(key);
  }
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
// smb://自动路由(口径同 MediaPlayer): 插件注册即用, 不要求业务显式
// setIoPlan——录制/抽帧(IRecorder)腿原来只在 ffmpeg 里找协议, smb://
  if (ioPlan != IoPlan::smb && uri.rfind("smb://", 0) == 0) {
    ModuleMgr::Get().ensureStarted();
    if (AvoxManager::Get().ioSources.hasObjectId(IoPlan::smb)) {
      ioPlan = IoPlan::smb;
    }
  }
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
  bVDropUntilIdr = false;
  vConfigPackets.clear();
  aConfigPackets.clear();
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
  videoChain.reset(*this);
  audioChain.reset(*this);
  RawSource::close();
}

bool AMediaSource::ioComplete() { return bIoEnd; }

void AMediaSource::setTransMode(TransMode mode) {
  transMode = mode;
  bVideoCopy = (mode == TransMode::VideoCopy);
  bAudioCopy = (mode == TransMode::AudioCopy);
}

void AMediaSource::setAudioCopySupported(
    std::function<bool(ACodecId)> supported) {
  audioCopySupported = std::move(supported);
}

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
  // 打开音频与视频解码器(copy轨不解码: 原始包在onPacket经onRawPacket转出)
  if (!vTracks.empty()) {
    const auto& vTrack = vTracks[0];
    LOGFLF(LogLevel::info, "video desc:", vTrack);
    if (bVideoCopy) {
      LOGFLF(LogLevel::info, "video copy mode, decoder off");
      // copy轨无解码器则无onVideoDesc回调, 手动补RawSource轨描述,
      // 否则checkTrackReady永远等不到视频轨, onReady不派发(录制器卡死)
      setVideoDesc(vTrack.desc);
    } else {
      initVideoDecoder(vTrack.codecId, vTrack.desc, bHardDecode);
    }
  }
  if (!aTracks.empty()) {
    const auto& aTrack = aTracks[0];
    LOGFLF(LogLevel::info, "audio desc:", aTrack);
// 直拷前置判定(容器容不下该编码就改走转码): 判定必须落在本函数 —— 源音频
// 编码只有 ioSource 就绪后才可知, 而直拷/转码的岔路(建不建解码器)在这里就
    if (bAudioCopy && audioCopySupported &&
        !audioCopySupported(aTrack.codecId)) {
      if (AvoxManager::Get().aDecoders.hasObjectId(aTrack.codecId)) {
        LOGFLF(LogLevel::warn,
               "audio copy unsupported by container, fallback transcode, codec:",
               aTrack.codecId);
        bAudioCopy = false;
      } else {
        // 容器容不下、本端又没有该编码解码器: 无从转码。保持直拷让封装层报错,
        // 不静默丢轨(丢轨是产品决策, 不在源层做)
        LOGFLF(LogLevel::warn,
               "audio copy unsupported by container and no decoder, keep copy, codec:",
               aTrack.codecId);
      }
    }
    if (bAudioCopy) {
      LOGFLF(LogLevel::info, "audio copy mode, decoder off");
      // 同视频copy: 手动补轨描述凑齐checkTrackReady握手
      setAudioDesc(aTrack.desc);
    } else {
      initAudioDecoder(aTrack.codecId, aTrack.desc);
    }
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
  // 流尾排空解码器: 重排与帧级线程扣住的尾帧不吐出来, 片尾会固定少一截
  if (bOpenVDecoder && videoDecoder()) {
    videoDecoder()->onInputEnd();
  }
  if (bOpenADecoder && audioDecoder()) {
    audioDecoder()->onInputEnd();
  }
  LOGFLF(LogLevel::info, "io complete");
}

void AMediaSource::onError(AVError error, const char* msg) {
  // 出错后IO不会再出帧, 置结束标志让消费线程的ioComplete()退出条件成立
  bIoEnd = true;
  dispatch(&IRawSourceOb::onError, error, msg);
}

void AMediaSource::onPacket(const AvoxPacket& packet) {
  // seek 前置:IO 线程执行 flush,保证与 decode 同线程不并发
  if (bFlushPending.load()) {
    bFlushPending.store(false);
    if (videoDecoder()) {
      videoDecoder()->flush();
    }
    if (audioDecoder()) {
      audioDecoder()->flush();
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
      if (bVideoCopy) {
        // copy轨原始包直出(不解码); 上面bDiscardPacket已含seek丢弃语义
        dispatch(&IRawSourceOb::onRawPacket, packet);
        break;
      }
      if (videoDecoder()) {
        // 换道落点: config 照喂, 其余非关键帧一律丢, 到 IDR 才恢复喂(见兜底函数)
        if (bVDropUntilIdr && packet.packtype != (int32_t)PackType::vconfig &&
            packet.frameType != 1) {
          break;
        }
        // PacketBuf buf(packet);
        PacketBufPtr pkt = std::make_shared<PacketBuf>(packet);
        // uint8_t nalu = getNalUnit(VCodecId::h264, packet);
        // log(LogLevel::info, "nalu:", (int32_t)nalu, " size:", packet.data.size);
        if (packet.packtype == (int32_t)PackType::vconfig) {
          videoDecoder()->pushConfig(*pkt);
          if (vConfigPackets.size() < 8) {
            vConfigPackets.push_back(pkt);  // 首帧前换道要重喂给新车道
          }
        } else if (bVDropUntilIdr) {
          bVDropUntilIdr = false;
          LOGFLF(LogLevel::info, "fallback landing snap to idr, pts:", packet.pts);
        }
        DecodeResult result = videoDecoder()->decoder(pkt);
        if (result == DecodeResult::success && !bOpenVDecoder) {
          LOGFLF(LogLevel::info, "open video decoder success");
          videoDecoder()->dispatch(&IVideoDecoderOb::onVideoDesc);
          bOpenVDecoder = true;
        } else if (result == DecodeResult::openFailed) {
          // 首帧前车道被拒(如 Apple VT 无 h264 10bit 输出): 按候选表降级重试
          // (与播放腿 VDecoderTask 同构), 候选走完才判车道死
          if (!bOpenVDecoder && videoChain.selectNext(*this)) {
            break;
          }
// 车道从没出过帧且连续喂包失败(FFDecoder 判据)= 解码车道死(如无软解
// 构建下的 AV1: 每包 ENOSYS)。停喂并按源错误上报 —— 否则整片喂完,
          LOGFLF(LogLevel::error, "video decode lane dead, stop feeding:", uri);
          videoChain.reset(*this);  // 候选走完, 解绑并销毁
          onError(AVError::decodeLaneDead, "video decode lane dead");
        }
      }
      break;
    case (int32_t)PackType::aconfig:
    case (int32_t)PackType::audio:
      if (bAudioCopy) {
        // copy轨原始包直出(不解码, fdk等解码兼容性问题整段绕开)
        dispatch(&IRawSourceOb::onRawPacket, packet);
        break;
      }
      if (audioDecoder()) {
        // PacketBuf buf(packet);
        // LOGFLF(LogLevel::info, "audio packet:", packet.pts);
        PacketBufPtr pkt = std::make_shared<PacketBuf>(packet);
        if (packet.packtype == (int32_t)PackType::aconfig) {
          audioDecoder()->pushConfig(*pkt);
          if (aConfigPackets.size() < 8) {
            aConfigPackets.push_back(pkt);  // 首帧前换道要重喂给新车道
          }
        }
        DecodeResult result = audioDecoder()->decoder(pkt);
        if (result == DecodeResult::success && !bOpenADecoder) {
          LOGFLF(LogLevel::info, "open audio decoder success");
          bOpenADecoder = true;
        } else if (result == DecodeResult::openFailed) {
          // 首帧前车道被拒(如 fdk-aac 不支持该 AAC profile): 按候选表降级重试,
          // 候选走完才判这条轨没得解 —— 旧实现只认 success, openFailed 被静默
          // 丢弃, 首选解码器打不开就是整片无声且零日志
          if (!bOpenADecoder && audioChain.selectNext(*this)) {
            break;
          }
          LOGFLF(LogLevel::error, "audio decode lane dead:", uri);
        }
      }
      break;
  }
}

bool AMediaSource::initVideoDecoder(VCodecId codecId, const VideoDesc& srcDesc_,
                                    bool bHard) {
  srcDesc = srcDesc_;
  // 排候选表并逐条建实例: 候选 = 注册表全量, 上层"软/硬偏好"只决定谁先试,
  // 另一极与注册表其余一律排在其后 —— 不假定上层已设定软或硬(旧实现把另一极
  // 写死成软解名, 且软解车道首帧前就死时不补硬解)
  if (!videoChain.build(codecId, bHard, videoDecoderName)) {
    LOGFLF(LogLevel::warn, "no video codec register:", codecId);
    return false;
  }
  if (!videoChain.selectFirst(*this)) {
    LOGFLF(LogLevel::warn, "no video decode lane available:", codecId);
    return false;
  }
  LOGFLF(LogLevel::info, "video lane selected:", videoChain.implName());
  return true;
}

// 车道链回调: 抽帧/录制腿不解码到渲染面, 接线只有观察者 + setContext
bool AMediaSource::laneInit(VideoDecoder& impl, const VCodecDesc& desc) {
  impl.setObserver(this);
  return impl.setContext(desc, srcDesc);
}

void AMediaSource::laneClose(VideoDecoder& impl) { impl.removeObserver(this); }

// 本腿的配置包另有 vConfigPackets 自有缓存(onPacket 里按到达顺序收), 不沿用
// 播放腿从旧解码器 copyConfigs 的路子
void AMediaSource::laneSaveConfigs(VideoDecoder& oldImpl) {}

// 换道后: 重喂本腿缓存的配置包, 并在喂包口吸到下一个关键帧(半 GOP 起解会产
// 缺参考坏帧); 首帧前才吸, 解码已出图时(videoChain 换道只发生在首帧前)不动落点
void AMediaSource::laneChanged(VideoDecoder& newImpl, bool bFirst) {
  if (bFirst) {
    return;
  }
  for (auto& cp : vConfigPackets) {
    newImpl.pushConfig(*cp);
  }
  vConfigPackets.clear();
  bOpenVDecoder = false;
  bVDropUntilIdr = true;
  LOGFLF(LogLevel::warn, "video lane downgrade to:", videoChain.implName());
}

bool AMediaSource::initAudioDecoder(ACodecId codecId,
                                    const AudioDesc& srcDesc_) {
  if (!AvoxManager::Get().aDecoders.hasObjectId(codecId)) {
    LOGFLF(LogLevel::warn, "no find audio codec:", codecId);
    return false;
  }
  // 排候选表并逐条建实例: 候选 = 注册表全量(优先名只决定谁先试)
  audioSrcDesc = srcDesc_;
  if (!audioChain.build(codecId)) {
    LOGFLF(LogLevel::warn, "no audio codec register:", codecId);
    return false;
  }
  if (!audioChain.selectFirst(*this)) {
    LOGFLF(LogLevel::warn, "no audio decode lane available:", codecId);
    return false;
  }
  LOGFLF(LogLevel::info, "audio lane selected:", audioChain.implName());
  return true;
}

// 车道链回调(音频): 抽帧/录制腿不解码到渲染面, 接线只有观察者 + setContext
bool AMediaSource::laneInit(AudioDecoder& impl, const ACodecDesc& desc) {
  impl.setObserver(this);
  return impl.setContext(desc, audioSrcDesc);
}

void AMediaSource::laneClose(AudioDecoder& impl) { impl.removeObserver(this); }

// 音频配置包走 aConfigPackets 自有缓存(AudioDecoder 没有 copyConfigs)
void AMediaSource::laneSaveConfigs(AudioDecoder& oldImpl) {}

// 换道后重喂配置包; 音频没有关键帧落点语义
void AMediaSource::laneChanged(AudioDecoder& newImpl, bool bFirst) {
  if (bFirst) {
    return;
  }
  for (auto& cp : aConfigPackets) {
    newImpl.pushConfig(*cp);
  }
  aConfigPackets.clear();
  bOpenADecoder = false;
  LOGFLF(LogLevel::warn, "audio lane downgrade to:", audioChain.implName());
}

void AMediaSource::onVideoDesc() {
  const auto& vTracks = ioSource->getVideoTracks();
  if (!vTracks.empty()) {
    auto vTrack = vTracks[0];
    const DecoderParams& dparams = videoDecoder()->getDecoderParams();
    vTrack.desc.fps = dparams.fps;
    setVideoRotation(vTrack.rotation);
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
  AudioDesc decodeDesc = audioDecoder()->getOutDesc();
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
    audioChain.reset(*this);
    bDisableAudio = true;
  }
  // 音频已到、视频没到 -> 关闭视频解码器,标记无视频
  else if (audioFirstPts != AVOX_NOVALID_PTS &&
           videoFirstPts == AVOX_NOVALID_PTS) {
    LOGFLF(LogLevel::warn,
           "video track declared but no packet arrived, drop video track");
    videoChain.reset(*this);
    bDisableVideo = true;
  }
  // 降级后重新检查轨道就绪,触发 onReady(否则录制器仍卡在等待)
  checkTrackReady();
}

}
