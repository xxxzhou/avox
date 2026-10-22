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
  // 排候选表并逐条建实例: 候选 = 注册表全量(优先名只决定谁先试), 兜底是否存在
  // 不再取决于注册顺序(旧写法取的是"首选项之后的注册后继")
  configPkts.clear();
  if (!chain.build(codecId)) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getACodecName(codecId),
                  " not register decoder");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  if (!chain.selectFirst(*this)) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getACodecName(codecId),
                  " select not support");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 开始解码线程
  startTask();
  // 记录track创建成功
  pb.result = ActionResult::success;
  string_format(pb.msg, "codecId ", getACodecName(codecId), " select ",
                chain.implName(), " init");
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
  pbInfo.codecId = codecId;
  pbInfo.channels = srcDesc.channels;
  pbInfo.sampleRate = srcDesc.sampleRate;
  pbInfo.format = srcDesc.format;
  return true;
}

// 车道链回调: 接线(播放器选项/观察者)与 setContext 一起算"这条车道能不能用"
bool ADecoderTask::laneInit(AudioDecoder& impl, const ACodecDesc& desc) {
  // 关联mediaplayer的选项设置
  impl.linkOption(trackContext->getMediaPlayer());
  impl.setObserver(trackContext);
  // 解码的上下文是当前track
  return impl.setContext(desc, srcDesc);
}

void ADecoderTask::laneClose(AudioDecoder& impl) {
  impl.removeObserver(trackContext);
}

// 本腿配置包走自有缓存(configPkts), AudioDecoder 没有 copyConfigs
void ADecoderTask::laneSaveConfigs(AudioDecoder& oldImpl) {}

// 换道后重放配置包; 音频没有关键帧落点与首帧供给看门狗, 超时重起算在调用处做。
// configPkts 不在此清空(保留给下一次换道重放, 与旧行为一致)
void ADecoderTask::laneChanged(AudioDecoder& newImpl, bool bFirst) {
  if (bFirst) {
    return;
  }
  for (auto& cp : configPkts) {
    newImpl.pushConfig(*cp);
  }
  LOGFLF(LogLevel::warn, "audio decoder fallback to:", chain.implName());
}

void ADecoderTask::onRunTask() {
  TickChecker check(delayMs);
  // 解码侧 scratch 走本 track 的包缓冲池(音频常驻最小档)
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>();
  bool bOpenDecode = false;
  DecodeResult result = DecodeResult::dataNoReady;
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    // seek冲刷由音频线程自己执行: 命令线程直接flush会与在解的包/换道重建
    // 窗口并发操作codecCtx(internal空必崩, 9/25 16:15视频道同根crash)
    if (bFlushCtx) {
      bFlushCtx = false;
      decoder()->flush();
    }
    bool bGet = trackContext->getPacketQueue().dequeueAction(
        [&](const PacketBufPtr& packetPtr) {
          tempPtr->form(*packetPtr, trackContext->getPacketPool());
        });
    if (bGet) {
      decoder()->dispatch(&IAudioDecoderOb::onPacket, tempPtr);
      if (tempPtr->configType()) {
        ConfigAddType type = decoder()->pushConfig(*tempPtr);
        addConfigRecord(type);
        // 缓存配置包, 解码器回退时重放(共享拷贝, tempPtr 会被复用)
        configPkts.push_back(std::make_shared<PacketBuf>(*tempPtr));
      }
      // 子类实际解码操作实现,视频解码本身就是一个耗时操作
      result = decoder()->decoder(tempPtr);
    }
    // 首选解码器初始化失败(如fdk-aac遇到不支持的AAC profile):
    // 换下一候选并重放配置继续, 而不是直接杀音频轨
    if (result == DecodeResult::openFailed && chain.selectNext(*this)) {
      bOpenDecode = false;
      check.reset();
      result = DecodeResult::dataNoReady;
      continue;
    }
    if (result == DecodeResult::success && !bOpenDecode) {      
      // 记录编码音频信息
      AudioDesc outDesc = decoder()->getOutDesc();
      pbInfo.xchannels = outDesc.channels;
      pbInfo.xsampleRate = outDesc.sampleRate;
      pbInfo.xformat = outDesc.format;
      pushPB<MPPBType::AudioInfo>(mpPingback, pbInfo);
      bOpenDecode = true;
    }
// 特定时间没有解码成功
// >4x只I帧生效中: 音频包被MediaPlayer::onPacket丢弃, 无包是预期, 持续reset
    if (mediaPlayer && mediaPlayer->iframeOnlyActive()) {
      check.reset();
    }
    if (!bOpenDecode && check.timeout()) {
      // 打不开的音轨要能死: 关闭+排干包队列, 否则满队列 enqueueWait 永久反压
      // demux, 视频包断供开流挂死(高码率视频+参数未解TrueHD实证)
      auto& pktQueue = trackContext->getPacketQueue();
      pktQueue.setClose(true);
      pktQueue.clear();
      trackContext->onDecodeError(DecodeResult::timeout);
      return;
    }
    if ((int32_t)result < 0) {
      // 解码器配置失败, 同样放开包队列再退(残包会反压demux)
      auto& pktQueue = trackContext->getPacketQueue();
      pktQueue.setClose(true);
      pktQueue.clear();
      trackContext->onDecodeError(result);
      return;
    }
    // 解码器遇到完成标志
    if (result == DecodeResult::complete) {
      decoder()->dispatch(&IAudioDecoderOb::onAudioComplete);
      return;
    }
// 空转(队列无包)才休眠让出CPU; 处理了包不垫睡——碎片化音轨实测~3000包/
// 内容秒, 逐包5ms会把消费上限钉在200包/s, 包队列(200)被灌满后enqueueWait
    if (!bGet) {
      sleepTask(false, 5);
    }
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
  if (decoder()) {
    bFlushCtx = true;
  }
}

void ADecoderTask::close() {
  stopTask();
  // 链持有解码器实例, reset 内回调 laneClose 解绑
  chain.reset(*this);
}

}
