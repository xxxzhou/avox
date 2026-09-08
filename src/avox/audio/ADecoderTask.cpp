#include "ADecoderTask.hpp"

#include "../module/AvoxManager.hpp"
#include "../player/AudioTrack.hpp"
#include "../player/MediaPlayer.hpp"

namespace avox {

ADecoderTask::ADecoderTask() { taskName = "audio decode task"; }

ADecoderTask::~ADecoderTask() { close(); }

bool ADecoderTask::start(AudioTrack* context) {
  close();
  trackContext = context;
  codecId = trackContext->getCodecId();
  srcDesc = trackContext->getInDesc();
  // 播放器设置传递给当前对象
  attachPlayContext(trackContext);
  // 记录track里的解码器创建
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = TrackType::audio;
  pb.action = MediaAction::create;
  // 先判断是否有解码器
  bool bFind = AvoxManager::Get().aDecoders.hasObjectId(codecId);
  if (!bFind) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "not find codecId ", getACodecName(codecId));
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  const auto& decodes = AvoxManager::Get().aDecoders.initFuncs(codecId);
  if (decodes.size() < 0) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getACodecName(codecId),
                  " not register decoder");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  size_t sIndex = 0;
  if (codecId == ACodecId::aac) {
    // 优先使用fdk-aac解码器
    // fdk-aac decoder/ffmpeg_aac
    const char* sName = "fdk-aac decoder";
    for (size_t i = 0; i < decodes.size(); ++i) {
      if (decodes[i].desc.name == sName) {
        sIndex = i;
        break;
      }
    }
  }
  auto& aDecode = decodes[sIndex];
  decode = std::unique_ptr<AudioDecoder>(aDecode.initFunc());
  if (!decode) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getACodecName(codecId), " select ",
                  aDecode.desc.name, " not init");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 关联mediaplayer的选项设计设置
  decode->linkOption(trackContext->getMediaPlayer());
  decode->setObserver(trackContext);
  // 解码的上下文是当前track
  bool bInit = decode->setContext(aDecode.desc, srcDesc);
  if (!bInit) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getACodecName(codecId), " select ",
                  aDecode.desc.name, " not support");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 开始解码线程
  startTask();
  // 记录track创建成功
  pb.result = ActionResult::success;
  string_format(pb.msg, "codecId ", getACodecName(codecId), " select ",
                aDecode.desc.name, " init");
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
  pbInfo.codecId = codecId;
  pbInfo.channels = srcDesc.channels;
  pbInfo.sampleRate = srcDesc.sampleRate;
  pbInfo.format = srcDesc.format;
  return true;
}

void ADecoderTask::onRunTask() {
  TickChecker check(delayMs);
  std::vector<uint8_t> temp(1000);
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>(temp);
  bool bOpenDecode = false;
  DecodeResult result = DecodeResult::dataNoReady;
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    bool bGet = trackContext->getPacketQueue().dequeueAction(
        [&](const PacketBufPtr& packetPtr) { tempPtr->form(*packetPtr); });
    if (bGet) {
      decode->dispatch(&IAudioDecoderOb::onPacket, tempPtr);
      if (tempPtr->configType()) {
        ConfigAddType type = decode->pushConfig(*tempPtr);
        addConfigRecord(type);
      }
      // 子类实际解码操作实现,视频解码本身就是一个耗时操作
      result = decode->decoder(tempPtr);
    }
    if (result == DecodeResult::success && !bOpenDecode) {      
      // 记录编码音频信息
      AudioDesc outDesc = decode->getOutDesc();
      pbInfo.xchannels = outDesc.channels;
      pbInfo.xsampleRate = outDesc.sampleRate;
      pbInfo.xformat = outDesc.format;
      pushPB<MPPBType::AudioInfo>(mpPingback, pbInfo);
      bOpenDecode = true;
    }
    // 特定时间没有解码成功
    // >4x只I帧生效中: 音频包被MediaPlayer::onPacket丢弃, 无包是预期, 持续reset
    // 期限, 超时从降速那一刻重新起算(否则降回4x后首次迭代就误判timeout杀掉音频轨)。
    // 判据用 iframeOnlyActive() 而非 speed>4: 选项关掉时音频包正常流, 不能豁免,
    // 否则真实解码故障在高倍速下被永久吞掉
    if (mediaPlayer && mediaPlayer->iframeOnlyActive()) {
      check.reset();
    }
    if (!bOpenDecode && check.timeout()) {
      trackContext->onDecodeError(DecodeResult::timeout);
      return;
    }
    if ((int32_t)result < 0) {
      // 解码器配置失败
      trackContext->onDecodeError(result);
      return;
    }
    // 解码器遇到完成标志
    if (result == DecodeResult::complete) {
      decode->dispatch(&IAudioDecoderOb::onAudioComplete);
      return;
    }
    // 根据队列状态动态选择策略
    sleepTask(result == DecodeResult::noConfig, 5);
  }
}

void ADecoderTask::addConfigRecord(ConfigAddType type) {
  // 忽略重复数据
  if (type == ConfigAddType::duplicate) {
    return;
  }
  // 记录配置数据
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = TrackType::audio;
  pb.action = MediaAction::config;
  // 指明是否重复数据
  string_format(pb.msg, "add config packet, data ", getConfigAddTypeStr(type));
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void ADecoderTask::flush() {
  if (decode) {
    decode->flush();
  }
}

void ADecoderTask::close() {
  stopTask();
  if (decode) {
    decode->removeObserver(trackContext);
    decode.reset();
  }
}

}
