#include "VDecoderTask.hpp"

#include "../module/AvoxManager.hpp"
#include "../player/MediaPlayer.hpp"
#include "../player/VideoTrack.hpp"

namespace avox {

VDecoderTask::VDecoderTask() { taskName = "video decode task"; }

VDecoderTask::~VDecoderTask() { close(); }

bool VDecoderTask::start(class VideoTrack* context) {
  // 先停止之前的解码线程
  close();
  trackContext = context;
  codecId = trackContext->getCodecId();
  srcDesc = trackContext->getDesc();
  // 播放器设置传递给当前对象
  attachPlayContext(trackContext);
  // 记录track里的解码器创建
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = TrackType::video;
  pb.action = MediaAction::create;
  // 先判断是否有解码器
  bool bFind = AvoxManager::Get().vDecoders.hasObjectId(codecId);
  if (!bFind) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "not find codecId ", getVCodecName(codecId));
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 查找解码器列表
  const auto& decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.size() < 0) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId),
                  " not register decoder");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 根据播放器设置选择解码器
  size_t sIndex = 0;
  bool bHard = mediaPlayer->getHardDecode();
  const char* sName = getDefaultDecoderName(codecId, bHard);
  // 查找解码器
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto& vDecode = decodes[sIndex];
  // 初始化解码器
  decode = std::unique_ptr<VideoDecoder>(vDecode.initFunc());
  if (!decode) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId), " select ",
                  vDecode.desc.name, " not init");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 关联mediaplayer的选项设计设置
  decode->linkOption(trackContext->getMediaPlayer());
  decode->setObserver(trackContext);
  // 查看系统是否支持
  bool bInit = decode->setContext(vDecode.desc, srcDesc);
  if (!bInit) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId), " select ",
                  vDecode.desc.name, " no support");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 开始解码线程
  startTask();
  // 记录最终是否启用硬解
  bHardDecode = false;
  codecTh = decode->getCodecTh();
  if (codecTh == VCodecTh::dx11 || codecTh == VCodecTh::androidMC ||
      codecTh == VCodecTh::iosVT) {
    bHardDecode = true;
  }
  // 记录track创建成功
  pb.result = ActionResult::success;
  string_format(pb.msg, "codecId ", getVCodecName(codecId), " select ",
                vDecode.desc.name, " init");
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
  // 记录视频信息
  pbInfo.codecId = codecId;
  pbInfo.slectCodec = vDecode.desc.name;
  return true;
}

void VDecoderTask::addConfigRecord(ConfigAddType type) {
  // 忽略重复数据
  if (type == ConfigAddType::duplicate) {
    return;
  }
  // 记录配置数据
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = TrackType::video;
  pb.action = MediaAction::config;
  // 指明是否重复数据
  string_format(pb.msg, "add config packet, data ", getConfigAddTypeStr(type));
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void VDecoderTask::flush() {
  if (decode) {
    decode->flush();
  }
}

void VDecoderTask::onRunTask() {
  TickChecker check(delayMs);
  // 如果有配置数据，可能是切换解码器保留的
  if (!configPackets.empty()) {
    for (int32_t i = 0; i < configPackets.size(); ++i) {
      decode->pushConfig(configPackets[i]);
    }
    configPackets.clear();
  }
  // 预先申请合适大小，减少重新申请的机率
  std::vector<uint8_t> temp(10000);
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>(temp);
  bool bOpenDecode = false;
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    double speed = trackContext->getSpeed();
    // 是否要求重置解码器
    if (bResetFlag || bDecodeUpdate) {
      PacketBufPtr topItem = nullptr;
      if (trackContext->getPacketQueue().peek(topItem)) {
        // 切换解码器，需要保存当前配置帧，退出当前线程
        if (bResetFlag && topItem->frameType == 1) {
          decode->copyConfigs(configPackets);
          break;
        }
        // 配置帧变了,不需要退出当前线程
        if (bDecodeUpdate && topItem->frameType == 1) {
          // 如果是硬解，重置后相关GPU的上下文可能失效
          // 原保存的frame也失效
          if (bHardDecode) {
            trackContext->flush();
          }
          DecodeResult result = decode->onPreDecoder();
          if (result == DecodeResult::success) {
            LOGFLF(LogLevel::info, "reset decoder success");
          } else {
            LOGFLF(LogLevel::warn, "reset decoder fail ");
          }
          bDecodeUpdate = false;
        }
      }
    }
    // 多个I帧分片是否需要合并在一起?
    bool bGet = trackContext->getPacketQueue().dequeueAction(
        [&](const PacketBufPtr& packetPtr) { tempPtr->form(*packetPtr); });
    DecodeResult result = DecodeResult::dataNoReady;
    if (bGet) {
      // 给track记录当前解码包的PTS
      decode->dispatch(&IVideoDecoderOb::onPacket, tempPtr);
      if (tempPtr->configType()) {
        // log(LogLevel::info, "xxxx ", tempPtr->size);
        ConfigAddType type = decode->pushConfig(*tempPtr);
        // 配置帧变了，重置不?
        // 直播有时频繁更新，但是老的也能继续解码
        // 直接重置不确定是否是一个好的策略
        // 或是检查大小是否变化，只有引起大小变化才重置？
        if (type == ConfigAddType::updateSize) {
          bDecodeUpdate = true;
        }
        if (type == ConfigAddType::duplicate) {
          continue;
        }
        addConfigRecord(type);
      }
      // 如果超过4倍,数据包只解I帧
      // if (speed > 4 && !tempPtr->configType() && !tempPtr->frameType) {
      //   continue;
      // }
      // 子类实际解码操作实现,视频解码本身就是一个耗时操作
      result = decode->decoder(tempPtr);
    }
    // 第一次返回成功表示解码器打开
    if (result == DecodeResult::success && !bOpenDecode) {
      decode->dispatch(&IVideoDecoderOb::onVideoDesc);
      RenderType renderType = selectRenderType();
      const DecoderParams& dparams = decode->getDecoderParams();
      // 记录视频信息
      pbInfo.yuvType = dparams.yuvType;
      pbInfo.width = dparams.width;
      pbInfo.height = dparams.height;
      pbInfo.fps = dparams.fps;
      pbInfo.renderType = renderType;
      pushPB<MPPBType::VideoInfo>(mpPingback, pbInfo);
      bOpenDecode = true;
    }
    if ((int32_t)result < 0) {
      // 解码器配置失败
      trackContext->onDecodeError(result);
      return;
    }
    // 解码器遇到完成标志
    if (result == DecodeResult::complete) {
      decode->dispatch(&IVideoDecoderOb::onVideoComplete);
      return;
    }
    if (!bOpenDecode && check.timeout()) {
      trackContext->onDecodeError(DecodeResult::timeout);
      return;
    }
    // 超过4倍后只解码I帧,相反可以慢下来了
    if (speed > 2 && speed <= 4) {
      // 超过2倍,快速解码
      sleepTask(true);
    } else {
      sleepTask(result == DecodeResult::noConfig, 10);
    }
  }
  // 如果是因为重置Flag关闭的，需要让Track再重置打开编码器
  if (bResetFlag) {
    decode->flush();
    if (trackContext) {
      trackContext->onResetDecoder();
    }
    bResetFlag = false;
  }
}

void VDecoderTask::close() {
  stopTask();
  if (decode) {
    // 队列里数据清空
    decode->removeObserver(trackContext);
    decode.reset();
  }
}

RenderType VDecoderTask::selectRenderType() {
  RenderType renderType = RenderType::Vulkan;
  switch (codecTh) {
    case VCodecTh::dx11:
      renderType = RenderType::D3D11;
      break;
    case VCodecTh::androidMC:
      renderType = RenderType::OpenGLES;
      break;
    case VCodecTh::iosVT:
      renderType = RenderType::Metal;
      break;
    default:
      break;
  }
  return renderType;
}

}
