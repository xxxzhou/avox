#include "VDecoderTask.hpp"

#include <cstring>

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
  // 排候选表并逐条建实例: 候选 = 注册表全量, 上层"软/硬偏好"只决定谁先试,
  // 另一极与注册表其余一律排在其后 —— 不假定上层已设定软或硬, 所以"设了软解"
  // 同样有另一极兜底(旧写法把另一极写死成软解名, 软解偏好时根本没有回退)
  const bool bPrefHard = mediaPlayer->getHardDecode();
  if (!chain.build(codecId, bPrefHard, mediaPlayer->getVideoDecoderName())) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId),
                  " not register decoder");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  if (!chain.selectFirst(*this)) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId), " select ",
                  getDefaultDecoderName(codecId, bPrefHard), " not init");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 开始解码线程
  startTask();
  // 记录track创建成功(codecTh/bHardDecode 已随选型在 laneChanged 里记好)
  pb.result = ActionResult::success;
  string_format(pb.msg, "codecId ", getVCodecName(codecId), " select ",
                chain.implName(), " init");
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
  // 记录视频信息
  pbInfo.codecId = codecId;
  pbInfo.slectCodec = chain.implName();
  return true;
}

// 车道链回调: 接线(播放器选项/观察者/DOVI profile)与 setContext 一起算
// "这条车道能不能用"; 建不出或 setContext 拒绝都返回 false 换下一条
bool VDecoderTask::laneInit(VideoDecoder &impl, const VCodecDesc &desc) {
  impl.linkOption(mediaPlayer);
  impl.setObserver(trackContext);
  impl.setDvProfile(trackContext->getDvProfile());
  if (!impl.setContext(desc, srcDesc)) {
    LOGFLF(LogLevel::warn, "decoder ", desc.name,
           " not support, try next lane");
    return false;
  }
  return true;
}

void VDecoderTask::laneClose(VideoDecoder &impl) {
  impl.removeObserver(trackContext);
}

void VDecoderTask::laneSaveConfigs(VideoDecoder &oldImpl) {
  configPackets.clear();
  oldImpl.copyConfigs(configPackets);
}

// 换道后的处置: 重喂配置帧(首次选型没有旧车道的配置可继承), 记车道类型,
// 看门狗重新计时; 首帧前换道还要吸到下一个关键帧起解
void VDecoderTask::laneChanged(VideoDecoder &newImpl, bool bFirst) {
  const bool bHadFrame = bGotFrame;
  if (!bFirst) {
    for (auto &cp : configPackets) {
      newImpl.pushConfig(cp);
    }
    configPackets.clear();
    LOGFLF(LogLevel::warn, "video decoder fallback to:", chain.implName());
    // 埋点透出换道事实(判定/分诊面): 首选名没交付, 实际交付的是候选名
    PBMediaAction fb = {};
    fb.mediaObject = MediaObject::decode;
    fb.trackType = TrackType::video;
    fb.action = MediaAction::config;
    string_format(fb.msg, "video decoder fallback from ", pbInfo.slectCodec,
                  " to ", chain.implName());
    pushPB<MPPBType::MediaAction>(mpPingback, fb);
    pbInfo.slectCodec = chain.implName();
  }
  codecTh = newImpl.getCodecTh();
  bHardDecode = (codecTh == VCodecTh::dx11 || codecTh == VCodecTh::androidMC ||
                 codecTh == VCodecTh::iosVT);
  // 新解码器需要重新证明自己交付帧, 看门狗重新计时
  bGotFrame = false;
  bOpenDecode = false;
  supplyMarkMs = 0;
  // 起播阶段换道: 视频队列吸到下一个关键帧起解。换道时IO已灌了数秒包,
  // 新车道若从GOP中段起解只会产出缺参考坏帧(实测POC 0/6/12连环报错);
  // 播放中换道画面已在出, 动落点会造成跳帧, 故只在首帧前吸
  if (bFirst || bHadFrame) {
    return;
  }
  int32_t dropped = trackContext->getPacketQueue().dropUntil(
      [](const PacketBufPtr &pkt) {
        return !pkt->configType() && pkt->frameType == 1;
      });
  if (dropped >= 0) {
    flattener.reset();
    LOGFLF(LogLevel::info, "fallback landing snap to idr, dropped:", dropped);
  } else {
    LOGFLF(LogLevel::info, "fallback landing keep position, no idr");
  }
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
  // 不直接decoder()->flush(): 本函数在命令线程跑, 同步flush会打进解码线程
  // onPreDecoder的alloc→open2窗口(internal空, FFmpeg8 flush无守卫必崩,
  // 9/25 16:15真机crash), 也可能与在解的包并发操作codecCtx
  // seek 会连同包队列一起清掉, 扣着的簇属于旧位置, 一起作废
  flattener.reset();
// 解码器状态必须整体重建: hw车道(实测d3d11va) avcodec_flush_buffers 清不掉
// h264 POC/frame_num, open-GOP 恢复点落点(non-IDR I帧带KEY标志)解码会
  if (decoder() && decoder()->getCodecTh() == VCodecTh::androidMC) {
    bFlushCtx = true;
    return;
  }
  bResetCtx = true;
}

void VDecoderTask::onRunTask() {
  // 输入连续排空的轮数阈值: 到它才认定 EOF 并放出尾簇
  const int32_t kTailIdleRounds = 300;
  int32_t idleRounds = 0;
  // 输入排空轮数阈值(重排尾部放空用): 比尾簇阈值短, 避免尾帧被拖后数秒
  const int32_t kEndIdleRounds = 20;
  int32_t endRounds = 0;
  // 摊平的标称帧长取容器声明值; 无 fps 信息(0)时摊平器整体旁路
  flattener.setup(srcDesc.fps > 1.0 ? (int64_t)(1000.0 / srcDesc.fps) : 0);
  // 如果有配置数据，可能是切换解码器保留的
  if (!configPackets.empty()) {
    for (int32_t i = 0; i < configPackets.size(); ++i) {
      decoder()->pushConfig(configPackets[i]);
    }
    configPackets.clear();
  }
  // 解码侧 scratch 也走同一个包缓冲池: 大 I 帧与小 P 帧各取各的档
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>();
  // 打开态与首帧看门狗基准归零(成员, laneChanged 换道时要重置)
  bOpenDecode = false;
  bGotFrame = false;
  supplyMarkMs = 0;
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    double speed = trackContext->getSpeed();
    // MediaCodec 原地 flush(见flush注释): 清 codec 内部 buffer/状态, 实例与
    // SurfaceTexture/EGL 链全保留 → 渲染层 GLESContext 不变不拆图, 旧帧持续在屏
    if (bFlushCtx) {
      bFlushCtx = false;
      if (bHardDecode) {
        trackContext->flushFrames();
      }
      decoder()->flush();
      LOGFLF(LogLevel::info, "seek flush decoder ok");
    }
    // seek重置(见flush注释): 不等包、不等参数集, 立即在解码线程重建codecCtx,
    // 从落点包起以全新状态解码
    if (bResetCtx) {
      bResetCtx = false;
      // 硬解码体重建后旧帧的GPU句柄(MediaCodec buffer idx/OES纹理)即悬空,
      // 不清会让渲染消费旧帧时把新codec输出循环搞死(seek后画面冻住只有声)
      if (bHardDecode) {
        trackContext->flushFrames();
      }
      DecodeResult result = decoder()->onPreDecoder();
      if (result == DecodeResult::success) {
        LOGFLF(LogLevel::info, "seek reset decoder ok");
      } else {
        LOGFLF(LogLevel::warn, "seek reset decoder skip: ", (int32_t)result);
      }
    }
    // 是否要求重置解码器
    if (bResetFlag || bDecodeUpdate) {
      PacketBufPtr topItem = nullptr;
      if (trackContext->getPacketQueue().peek(topItem)) {
        // 切换解码器，需要保存当前配置帧，退出当前线程
        if (bResetFlag && topItem->frameType == 1) {
          decoder()->copyConfigs(configPackets);
          break;
        }
        // 配置帧变了,不需要退出当前线程
        if (bDecodeUpdate && topItem->frameType == 1) {
// 如果是硬解，重置后相关GPU的上下文可能失效
// 原保存的frame也失效。只丢帧不丢包: 本地文件IO常在此时已读完全部
          if (bHardDecode) {
            trackContext->flushFrames();
          }
          DecodeResult result = decoder()->onPreDecoder();
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
    // 摊平器只 peek 队头 pts 定簇, 认领的包才出队; 正常流的单包簇位移恒 0,
    // 配置包与无标称帧率时走原 dequeueAction 路径, pts 一律不动
    bool bGet = false;
    PacketBufPtr head = nullptr;
    auto& packetQueue = trackContext->getPacketQueue();
    if (packetQueue.peek(head) && !head->configType() &&
        flattener.feed(head->pts) && packetQueue.dequeue(head)) {
      flattener.take(head);
    } else {
      bGet = packetQueue.dequeueAction(
          [&](const PacketBufPtr& packetPtr) {
            tempPtr->form(*packetPtr, trackContext->getPacketPool());
          });
    }
// 尾簇: 输入排空(本地文件常已读完全部包)后必须放出来, 否则最后一簇扣死.
// 用排空轮数而不是队列 close 来判, 直播长静默也会走到这里, 但一次只错一簇。
    if (!bGet && flattener.pending() && packetQueue.empty() &&
        ++idleRounds > (mediaPlayer->ioExhausted() ? kEndIdleRounds
                                                   : kTailIdleRounds)) {
      flattener.finish();
      idleRounds = 0;
    }
    if (!bGet) {
      bGet = flattener.pop([&](const PacketBufPtr& pkt) {
        tempPtr->form(*pkt, trackContext->getPacketPool());
      });
    }
    // 重排尾部: 尾簇放完且连续若干轮无包可解, 认定输入结束, 让解码器放空内部
    // 扣住的重排帧——不放空则流尾 reorderFrames 帧永不显示
    if (!bGet && packetQueue.empty() && !flattener.pending() &&
        mediaPlayer->ioExhausted()) {
      if (++endRounds > kEndIdleRounds) {
        endRounds = 0;
        decoder()->onInputEnd();
      }
    } else {
      endRounds = 0;
    }
    DecodeResult result = DecodeResult::dataNoReady;
    if (bGet) {
      // 给track记录当前解码包的PTS
      decoder()->dispatch(&IVideoDecoderOb::onPacket, tempPtr);
      if (tempPtr->configType()) {
        // log(LogLevel::info, "xxxx ", tempPtr->size);
        ConfigAddType type = decoder()->pushConfig(*tempPtr);
// 配置帧变了，重置不?
// 直播有时频繁更新，但是老的也能继续解码
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
      result = decoder()->decoder(tempPtr);
    }
    // 第一次返回成功表示解码器打开
    if (result == DecodeResult::success && !bOpenDecode) {
      decoder()->dispatch(&IVideoDecoderOb::onVideoDesc);
      RenderType renderType = selectRenderType();
      const DecoderParams& dparams = decoder()->getDecoderParams();
      // 记录视频信息
      pbInfo.yuvType = dparams.yuvType;
      pbInfo.width = dparams.width;
      pbInfo.height = dparams.height;
      pbInfo.fps = dparams.fps;
      pbInfo.renderType = renderType;
      pushPB<MPPBType::VideoInfo>(mpPingback, pbInfo);
      bOpenDecode = true;
    }
    // 懒open失败(如vulkan会话协商失败): 换下一候选重放配置帧继续, 而不是杀视频轨
    if (result == DecodeResult::openFailed && chain.selectNext(*this)) {
      continue;
    }
    if ((int32_t)result < 0) {
      // 解码器配置失败: 放开包队列再退, 否则满队列 enqueueWait 永久反压 demux,
      // 音频包也断供全管线冻死(高码率+TrueHD实证); 设计意图视频死音频续播
      auto& pktQueue = trackContext->getPacketQueue();
      pktQueue.setClose(true);
      pktQueue.clear();
      trackContext->onDecodeError(result);
      return;
    }
    // 解码器遇到完成标志
    if (result == DecodeResult::complete) {
      decoder()->dispatch(&IVideoDecoderOb::onVideoComplete);
      return;
    }
    if (!bGotFrame && trackContext->getFrameQueue().size() > 0) {
      bGotFrame = true;
    }
    // 首帧看门狗: 只在持续吃得到输入时计时(本轮喂进包/队列有货/摊平器有簇),
    // 饿等清零重记; 连续供给满delayMs仍无帧才认定车道死(如vulkan会话建出后
    // 驱动不产帧)
    {
      int64_t nowMs = timeStampMS();
      if (!bGotFrame &&
          (bGet || !packetQueue.empty() || flattener.pending())) {
        if (supplyMarkMs == 0) {
          supplyMarkMs = nowMs;
        } else if (nowMs - supplyMarkMs > delayMs && chain.selectNext(*this)) {
          // 死车道已把本地流吞到EOF: 回0重读让新车道从关键帧起解, 否则永远等不到数据
          if (mediaPlayer->ioExhausted() && mediaPlayer->seekable()) {
            mediaPlayer->seek(0);
          }
          continue;
        } else if (nowMs - supplyMarkMs > delayMs) {
          trackContext->onDecodeError(DecodeResult::timeout);
          return;
        }
      } else if (!bGotFrame) {
        supplyMarkMs = 0;
      }
    }
    // 超过4倍后只解码I帧,相反可以慢下来了
    if (speed > 2 && speed <= 4) {
      // 超过2倍,快速解码
      sleepTask(true);
    } else if (!bGet || result == DecodeResult::noConfig) {
      // 没取到包 / 参数集还没齐: 阻塞等待下一轮, 避免空转
      sleepTask(false, 10);
    } else {
// 正常处理了数据: 必须全速继续, 不能睡。
// 原写法 sleepTask(result == noConfig, 10) 语义反了 —— 解出数据反而 sleep 10ms,
      sleepTask(true);
    }
  }
  // 如果是因为重置Flag关闭的，需要让Track再重置打开编码器
  if (bResetFlag) {
    decoder()->flush();
    if (trackContext) {
      trackContext->onResetDecoder();
    }
    bResetFlag = false;
  }
}

void VDecoderTask::close() {
  stopTask();
  // 摊平器随任务销毁复位: lastOut是上一轮输出的单调下界, 残留会把下一轮
  // 的组合多包簇(配置帧+I帧)整簇钳到上一轮停片位
  flattener.reset();
  // 车道链持有解码器实例, reset 内回调 laneClose 解绑。配置帧不在此清:
  // 软硬解切换(见 flush/onResetDecoder)靠它跨过一次 start() 传承, 与旧行为一致
  chain.reset(*this);
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
