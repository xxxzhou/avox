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
  // 查找解码器列表
  const auto& decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.size() < 0) {
    pb.result = ActionResult::fail;
    string_format(pb.msg, "codecId ", getVCodecName(codecId),
                  " not register decoder");
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
    return false;
  }
  // 根据播放器设置选择解码器; 硬解选型不可用时逐级回退: vulkan 备选 → 软解
  // (如 VP9 老核显无 D3D11VA profile, a03/a12 的运行期回退框架先行手动兜底)
  bool bHard = mediaPlayer->getHardDecode();
  // 解码器名覆盖(测试/排障强制车道): 覆盖首选名, 不再自动插vulkan备选, 失败直接回软解
  const std::string& sOverrideName = mediaPlayer->getVideoDecoderName();
  const char* sOverride =
      (bHard && !sOverrideName.empty()) ? sOverrideName.c_str() : nullptr;
  const char* sName = sOverride ? sOverride : getDefaultDecoderName(codecId, bHard);
  const char* sFallback = bHard ? getDefaultDecoderName(codecId, false) : nullptr;
  if (sFallback && strcmp(sFallback, sName) == 0) {
    sFallback = nullptr;
  }
  // 硬解备选: 主路失败先试vulkan(未注册自动跳过); 覆盖点名时跳过, 保证机器无关回软解
  const char* sVulkan = nullptr;
  if (bHard && !sOverride) {
    if (codecId == VCodecId::h264) {
      sVulkan = AVOX_FFVULKAN_H264_DECODER;
    } else if (codecId == VCodecId::h265) {
      sVulkan = AVOX_FFVULKAN_H265_DECODER;
    }
    if (sVulkan && (strcmp(sVulkan, sName) == 0 ||
                    (sFallback && strcmp(sVulkan, sFallback) == 0))) {
      sVulkan = nullptr;
    }
  }
  size_t sIndex = 0;
  // 按名字选定并初始化: 找不到/建不出/不支持都算未命中, 交给下一候选
  auto trySelect = [&](const char* wantName) -> bool {
    size_t idx = 0;
    bool bHit = false;
    for (size_t i = 0; i < decodes.size(); ++i) {
      if (decodes[i].desc.name == wantName) {
        idx = i;
        bHit = true;
        break;
      }
    }
    if (!bHit) {
      return false;
    }
    auto cand = std::unique_ptr<VideoDecoder>(decodes[idx].initFunc());
    if (!cand) {
      return false;
    }
    cand->linkOption(trackContext->getMediaPlayer());
    cand->setObserver(trackContext);
    if (!cand->setContext(decodes[idx].desc, srcDesc)) {
      LOGFLF(LogLevel::warn, "decoder ", decodes[idx].desc.name,
             " not support, try fallback");
      return false;
    }
    sIndex = idx;
    decode = std::move(cand);
    return true;
  };
  bool bHitName = false;
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      bHitName = true;
      break;
    }
  }
  // 候选次序: 首选名 → vulkan 备选 → 硬解失败回退软解名 → 原行为兜底(找不到名字用首项)
  if (!trySelect(sName)) {
    if (!(sVulkan && trySelect(sVulkan))) {
      if (!(sFallback && trySelect(sFallback))) {
        if (bHitName || decodes.empty() ||
            !trySelect(decodes[0].desc.name.c_str())) {
          pb.result = ActionResult::fail;
          string_format(pb.msg, "codecId ", getVCodecName(codecId),
                        " select ", sName, " not init");
          pushPB<MPPBType::MediaAction>(mpPingback, pb);
          return false;
        }
      }
    }
  }
  auto& vDecode = decodes[sIndex];
  // 缓存懒open失败的降级候选: 默认链按 vulkan→软解 排; 名字被覆盖时仅软解(sVulkan已置空)
  openFallbacks.clear();
  for (const char* sWant : {sVulkan, sFallback}) {
    if (!sWant) {
      continue;
    }
    for (auto& d : decodes) {
      if (d.desc.name == sWant) {
        openFallbacks.push_back({d.desc, d.initFunc});
        break;
      }
    }
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
  // seek 会连同包队列一起清掉, 扣着的簇属于旧位置, 一起作废
  flattener.reset();
  // 解码器状态必须整体重建: hw车道(实测d3d11va) avcodec_flush_buffers 清不掉
  // h264 POC/frame_num, open-GOP 恢复点落点(non-IDR I帧带KEY标志)解码会
  // Frame num gap 螺旋到持续 0 帧(霍小玉.mkv seek冻结实证); 由解码线程在
  // 下一轮循环消费本标志, 避免与在解的包并发操作 codecCtx
  bResetCtx = true;
}

void VDecoderTask::onRunTask() {
  TickChecker check(delayMs);
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
      decode->pushConfig(configPackets[i]);
    }
    configPackets.clear();
  }
  // 预先申请合适大小，减少重新申请的机率
  std::vector<uint8_t> temp(10000);
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>(temp);
  bool bOpenDecode = false;
  // 视频轨是否真收到过帧: success会被重复参数集包空转返回污染, 看门狗只认实际产出
  bool bGotFrame = false;
  // 降级下一候选: 重建解码器并重放配置帧, 建不出/不支持跳下一个; open失败与首帧超时共用
  auto tryNextFallback = [&]() -> bool {
    while (!openFallbacks.empty()) {
      auto next = std::move(openFallbacks.front());
      openFallbacks.erase(openFallbacks.begin());
      configPackets.clear();
      decode->copyConfigs(configPackets);
      auto cand = std::unique_ptr<VideoDecoder>(next.second());
      bool bOk = false;
      if (cand) {
        cand->linkOption(trackContext->getMediaPlayer());
        cand->setObserver(trackContext);
        bOk = cand->setContext(next.first, srcDesc);
      }
      if (!bOk) {
        continue;
      }
      decode = std::move(cand);
      for (auto& cp : configPackets) {
        decode->pushConfig(cp);
      }
      configPackets.clear();
      codecTh = decode->getCodecTh();
      bHardDecode = (codecTh == VCodecTh::dx11 || codecTh == VCodecTh::androidMC ||
                     codecTh == VCodecTh::iosVT);
      // 新解码器需要重新证明自己交付帧, 看门狗重新计时
      bGotFrame = false;
      bOpenDecode = false;
      check.reset();
      LOGFLF(LogLevel::warn, "video decoder fallback to:", next.first.name);
      // 埋点透出换道事实(判定/分诊面): 首选名没交付, 实际交付的是候选名
      PBMediaAction fb = {};
      fb.mediaObject = MediaObject::decode;
      fb.trackType = TrackType::video;
      fb.action = MediaAction::config;
      string_format(fb.msg, "video decoder fallback from ", pbInfo.slectCodec,
                    " to ", next.first.name);
      pushPB<MPPBType::MediaAction>(mpPingback, fb);
      pbInfo.slectCodec = next.first.name;
      return true;
    }
    return false;
  };
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      sleepTask(false, 10);
      continue;
    }
    double speed = trackContext->getSpeed();
    // seek重置(见flush注释): 不等包、不等参数集, 立即在解码线程重建codecCtx,
    // 从落点包起以全新状态解码
    if (bResetCtx) {
      bResetCtx = false;
      DecodeResult result = decode->onPreDecoder();
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
          decode->copyConfigs(configPackets);
          break;
        }
        // 配置帧变了,不需要退出当前线程
        if (bDecodeUpdate && topItem->frameType == 1) {
          // 如果是硬解，重置后相关GPU的上下文可能失效
          // 原保存的frame也失效。只丢帧不丢包: 本地文件IO常在此时已读完全部
          // 包(EOF), 清包队列会让切换分辨率后的包无处补充, 解码线程静默饿死
          if (bHardDecode) {
            trackContext->flushFrames();
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
          [&](const PacketBufPtr& packetPtr) { tempPtr->form(*packetPtr); });
    }
    // 尾簇: 输入排空(本地文件常已读完全部包)后必须放出来, 否则最后一簇扣死.
    // 用排空轮数而不是队列 close 来判, 直播长静默也会走到这里, 但一次只错一簇。
    // IO 已确认读完(ioExhausted)时用短阈值: 此时不可能是直播静默, 再等 300 轮
    // 只会把流尾拖后数秒, 连带把重排缓冲的尾帧也顶到后面
    if (!bGet && flattener.pending() && packetQueue.empty() &&
        ++idleRounds > (mediaPlayer->ioExhausted() ? kEndIdleRounds
                                                   : kTailIdleRounds)) {
      flattener.finish();
      idleRounds = 0;
    }
    if (!bGet) {
      bGet = flattener.pop([&](const PacketBufPtr& pkt) {
        tempPtr->form(*pkt);
      });
    }
    // 重排尾部: 尾簇放完且连续若干轮无包可解, 认定输入结束, 让解码器放空内部
    // 扣住的重排帧——不放空则流尾 reorderFrames 帧永不显示
    if (!bGet && packetQueue.empty() && !flattener.pending() &&
        mediaPlayer->ioExhausted()) {
      if (++endRounds > kEndIdleRounds) {
        endRounds = 0;
        decode->onInputEnd();
      }
    } else {
      endRounds = 0;
    }
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
    // 懒open失败(如vulkan会话协商失败): 换下一候选重放配置帧继续, 而不是杀视频轨
    if (result == DecodeResult::openFailed && tryNextFallback()) {
      continue;
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
    if (!bGotFrame && trackContext->getFrameQueue().size() > 0) {
      bGotFrame = true;
    }
    if (!bGotFrame && check.timeout()) {
      // 首帧看门狗超时(选中但不交付帧, 如vulkan会话建出后驱动不产帧): 降级下一候选
      if (tryNextFallback()) {
        // 死车道已把本地流吞到EOF: 回0重读让新车道从关键帧起解, 否则永远等不到数据
        if (mediaPlayer->ioExhausted() && mediaPlayer->seekable()) {
          mediaPlayer->seek(0);
        }
        continue;
      }
      trackContext->onDecodeError(DecodeResult::timeout);
      return;
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
      // 单包耗时被抬到 10ms+, 50fps 源实测只能出 ~33fps(帧间隔 28ms)。
      sleepTask(true);
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
