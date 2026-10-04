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
  // 直接 Protocol not found, 与播放腿行为分裂(缩图恒空即此症)
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
    // 要定下, 再往后 muxer 已经在等着 desc 了。容器能力由录制器注入的判定问
    // muxer 实现层(rmvb 的 cook 进 mp4/mov 无 tag: 直拷必在写头失败, 且失败后
    // 录制器拿不到终态 → 任务永久 0%, 2026-09-29 画质增强案)。
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
      if (bVideoCopy) {
        // copy轨原始包直出(不解码); 上面bDiscardPacket已含seek丢弃语义
        dispatch(&IRawSourceOb::onRawPacket, packet);
        break;
      }
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
        } else if (result == DecodeResult::openFailed) {
          // 车道从没出过帧且连续喂包失败(FFDecoder 判据)= 解码车道死(如无软解
          // 构建下的 AV1: 每包 ENOSYS)。停喂并按源错误上报 —— 否则整片喂完,
          // 每包 4 行报错(1003 实测 4 个缩略图 job 刷 22 万行/约 2 小时)。
          LOGFLF(LogLevel::error, "video decode lane dead, stop feeding:", uri);
          videoDecoder->removeObserver(this);
          videoDecoder.reset();
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
  return initVideoDecoderTried(codecId, srcDesc, bHard, /*bTriedHard=*/false,
                               /*bTriedSoft=*/false);
}

// 解码器选型的候选链(与播放器腿 VDecoderTask 同构): 先主路, 失败逐级下沉/上浮。
// 硬软互备 + 已试过的不重试(防硬→软→硬 无限递归)。抽帧腿(IRecorder)默认软解,
// 此前软解名缺注册时直接失败 —— Windows AV1(只有硬解可用, libdav1d 缺席)即此症。
bool AMediaSource::initVideoDecoderTried(VCodecId codecId,
                                         const VideoDesc& srcDesc, bool bHard,
                                         bool bTriedHard, bool bTriedSoft) {
  if (bHard) {
    bTriedHard = true;
  } else {
    bTriedSoft = true;
  }
  if (!AvoxManager::Get().vDecoders.hasObjectId(codecId)) {
    LOGFLF(LogLevel::warn, "no find video codec:", codecId);
    return false;
  }
  const auto& decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.empty()) {
    LOGFLF(LogLevel::warn, "no video codec:", codecId, " init");
    return false;
  }
  // 解码器名覆盖(测试/排障强制车道): 覆盖首选名, 不再自动插vulkan备选, 失败直接回软解
  const char* sOverride =
      (bHard && !videoDecoderName.empty()) ? videoDecoderName.c_str() : nullptr;
  const char* sName = sOverride ? sOverride : getDefaultDecoderName(codecId, bHard);
  // AV1 软解车道可用性判定(1003): FFmpeg 原生 "av1" 是 hwaccel-only 包装, 无
  // hwaccel 上下文即每包 AVERROR(ENOSYS)。构建带 dav1d 时注册表才有真正能解帧的
  // 软解器 "libdav1d"(FFHelper 归一注册名), 此时软解名改指它; 注册表没有 dav1d
  // (如 Windows 无该库)则 AV1 软解车道不存在, 直接上浮硬解 —— 旧行为会落回原生
  // "av1" 空壳, 每包 ENOSYS 解不出帧(缩略图/转码链实证)。
  bool bSoftAv1Unusable = false;
  if (!sOverride && !bHard && codecId == VCodecId::av1) {
    bool bHasDav1d = false;
    for (size_t i = 0; i < decodes.size(); ++i) {
      if (decodes[i].desc.name == AVOX_FF_LIBDAV1D_DECODER) {
        sName = AVOX_FF_LIBDAV1D_DECODER;
        bHasDav1d = true;
        break;
      }
    }
    bSoftAv1Unusable = !bHasDav1d;
  }
  // 硬解备选: 主路失败先试vulkan(未注册自动跳过); 覆盖点名时跳过, 保证机器无关回软解
  const char* sVulkan = nullptr;
  if (bHard && !sOverride) {
    if (codecId == VCodecId::h264) {
      sVulkan = AVOX_FFVULKAN_H264_DECODER;
    } else if (codecId == VCodecId::h265) {
      sVulkan = AVOX_FFVULKAN_H265_DECODER;
    }
    if (sVulkan && strcmp(sVulkan, sName) == 0) {
      sVulkan = nullptr;
    }
  }
  size_t sIndex = 0;
  bool bHitName = false;
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      bHitName = true;
      break;
    }
  }
  if (!bHitName && sOverride) {
    // 覆盖名未注册: 不落首项兜底, 直接回软解重选
    LOGFLF(LogLevel::warn, "override decoder ", sName, " not register, try soft");
    return initVideoDecoderTried(codecId, srcDesc, false, bTriedHard, bTriedSoft);
  }
  // 软解车道不可用: 名查不到, 或该编码的软解器实际解不出帧(AV1 空壳)。上浮硬解重选
  // —— 该编码本端只有硬解可用时(Windows AV1 即此), 软解设置也能出图。硬解已试过
  // 则放弃(防硬→软→硬 无限递归)。
  if ((!bHitName || bSoftAv1Unusable) && !bHard) {
    if (bTriedHard) {
      LOGFLF(LogLevel::warn, "soft decoder ", sName,
             " unusable and hard already tried, give up");
      return false;
    }
    LOGFLF(LogLevel::warn, "soft decoder ", sName,
           " not register or unusable, try hard decode");
    return initVideoDecoderTried(codecId, srcDesc, true, bTriedHard, bTriedSoft);
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
      // 主路失败先试 vulkan 备选(未注册/环境不支持自动跳过), 再落软解
      if (sVulkan) {
        for (size_t i = 0; i < decodes.size(); ++i) {
          if (decodes[i].desc.name != sVulkan) {
            continue;
          }
          videoDecoder = std::unique_ptr<VideoDecoder>(decodes[i].initFunc());
          if (videoDecoder) {
            videoDecoder->setObserver(this);
            if (videoDecoder->setContext(decodes[i].desc, srcDesc)) {
              LOGFLF(LogLevel::info, "create video decode success (vulkan)");
              return true;
            }
            LOGFLF(LogLevel::warn, "vulkan backup failed, fallback to soft");
            videoDecoder.reset();
          }
          break;
        }
      }
      if (bTriedSoft) return false;
      return initVideoDecoderTried(codecId, srcDesc, false, bTriedHard,
                                   bTriedSoft);
    }
    // 软解建不出/不支持(setContext 失败): 上浮硬解兜底(硬解已试过则放弃)
    if (bTriedHard) return false;
    return initVideoDecoderTried(codecId, srcDesc, true, bTriedHard, bTriedSoft);
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
