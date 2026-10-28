#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "IOMuxer.hpp"

#include "../codec/H26XHelper.hpp"
#include "MediaMuxer.hpp"

namespace avox {

namespace {
// G14: 纯非VCL访问单元判定。产物开头出现过 28B/8B 的纯 SEI 访问单元(无任何
// VCL NAL), 写进 MP4 是无用 sample 且部分播放器起播异常; SEI+slice 混合包
bool bSeiNal(VCodecId vcodecId, uint8_t nal) {
  if (vcodecId == VCodecId::h264) {
    return static_cast<H264NAL>(nal) == H264NAL::NAL_SEI;
  }
  if (vcodecId == VCodecId::h265) {
    return static_cast<H265NAL>(nal) == H265NAL::NAL_SEI_PREFIX ||
           static_cast<H265NAL>(nal) == H265NAL::NAL_SEI_SUFFIX;
  }
  return false;
}

bool bVclNal(VCodecId vcodecId, uint8_t nal) {
  if (vcodecId == VCodecId::h264) {
    return naluDataFrame(static_cast<H264NAL>(nal));
  }
  if (vcodecId == VCodecId::h265) {
    return naluDataFrame(static_cast<H265NAL>(nal));
  }
  return false;
}

bool pureNonVclAu(VCodecId vcodecId, const AvoxPacket& data) {
  if (!bSeiNal(vcodecId, getNalUnit(vcodecId, data))) {
    return false;
  }
  // 整包扫 NAL: annexb(00 00 01 起始)与 avcc(长度前缀)两种布局都认
  std::vector<AvoxPacket> nalus;
  const int32_t annexb =
      checkAnnexbHeader(data.data.data, data.data.size);
  if (annexb > 0) {
    splitAnnexbNalu(data, nalus);
  } else {
    splitAvccNalu(data, nalus);
  }
  for (const auto& nalu : nalus) {
    if (bVclNal(vcodecId, getNalUnit(vcodecId, nalu))) {
      return false;  // 含 VCL: 正常访问单元
    }
  }
  return true;
}
}  // namespace

IOMuxer::IOMuxer() {}

IOMuxer::~IOMuxer() { stopTask(); }

void IOMuxer::open(const char* url_) {
  url = url_;
  vDesc = {};
  aDesc = {};
  bInitStreams = false;
  bInitFailed = false;
  basePts = AVOX_NOVALID_PTS;
  audioSynthPts = AVOX_NOVALID_PTS;
  audioSynthStepMs = 0;
  audioAnchorPts = AVOX_NOVALID_PTS;
  audioAnchorPackets = 0;
  pendingAudio.clear();
  packetQueue.setMaxSize(100);
  //
  LOGFLF(LogLevel::info, "url:", url);
  // 检查是否onvif
  onOpen();
  startTask();
}

void IOMuxer::setVideoDesc(const VTrackDesc& desc) {
  vDesc = desc;
  LOGFLF(LogLevel::info, "set video desc: ", vDesc);
  bHaveVideo = true;
}

void IOMuxer::setAudioDesc(const ATrackDesc& desc) {
  aDesc = desc;
  LOGFLF(LogLevel::info, "set audio desc: ", aDesc);
  bHaveAudio = true;
}

void IOMuxer::noteMuxDts(int64_t dts, int32_t packtype) {
  const bool bVideo = (packtype == (int32_t)PackType::video);
  int64_t& lastDts = bVideo ? lastMuxVideoDts : lastMuxAudioDts;
  if (lastDts != AVOX_NOVALID_PTS && dts != AVOX_NOVALID_PTS && dts < lastDts) {
    ++dtsBackwardCount;
    // 只在首次回退打完整信息, 之后仅累计 —— 一次 seek 就可能连续上百包, 全打会刷爆日志
    if (dtsBackwardCount == 1) {
      LOGFLF(LogLevel::warn, "mux dts backward, now:", dts, " pre:", lastDts,
             " track:", bVideo ? "video" : "audio");
    }
  }
  lastDts = dts;
}

void IOMuxer::pushPacket(PacketBufPtr packet) {
  // 无running()守卫: 本方法仅worker线程(onRunTask)调用, close后仍需排干
  // 队列残留包(离线快速转封装时尾部几十帧全在此), 丢弃会截短成片;
  // 关闭后写的安全性由onPushPacket的fmtCtx空保护兜底
  PackType packType = (PackType)packet->packtype;
// 音频NOPTS合成(b6fb8af 后源层音频垃圾pts归一NOPTS透传): 播放侧靠解码采样
// 轴推进, 录制链没有解码轴, NOPTS 直进会把 basePts 毒化成 INT64_MIN 或写出
  if (packType == PackType::audio) {
    if (packet->pts != AVOX_NOVALID_PTS) {
      if (packet->dts == AVOX_NOVALID_PTS) {
        packet->dts = packet->pts;
      }
      if (audioAnchorPts != AVOX_NOVALID_PTS && audioAnchorPackets > 0 &&
          packet->pts > audioAnchorPts) {
        const int64_t learned =
            (packet->pts - audioAnchorPts) / audioAnchorPackets;
        if (learned > 0 && learned <= 500) {
          audioSynthStepMs = learned;
        }
      }
      if (audioSynthPts == AVOX_NOVALID_PTS || packet->pts >= audioSynthPts) {
        audioSynthPts = packet->pts;
      }
      audioAnchorPts = packet->pts;
      audioAnchorPackets = 0;
    } else {
      if (audioSynthPts == AVOX_NOVALID_PTS) {
        // 尚无有效锚: 借视频基准起量(源时间域同轴), 首包则从0
        audioSynthPts = (basePts != AVOX_NOVALID_PTS) ? basePts : 0;
      }
      const int64_t advance = packet->duration > 0 ? packet->duration
                              : (audioSynthStepMs > 0 ? audioSynthStepMs : 40);
      packet->pts = audioSynthPts;
      packet->dts = audioSynthPts;
      audioSynthPts += advance;
      ++audioAnchorPackets;
    }
  }
  // 配置包可能带 NOPTS; 源层给配置帧减 cindex 时还可能溢出成正的巨大值。
  // 这类时间戳既不能定基准也不能参与减法, 统一归为 NOPTS。
  constexpr int64_t kInvalidPtsThreshold = -1000000000LL;
  constexpr int64_t kInvalidPtsCeiling = 1000000000000LL;
  const auto validTs = [](int64_t value) {
    return value >= kInvalidPtsThreshold && value <= kInvalidPtsCeiling;
  };
  if (!validTs(packet->dts)) {
    packet->dts = validTs(packet->pts) ? packet->pts : AVOX_NOVALID_PTS;
  }
  if (!validTs(packet->pts)) {
    packet->pts = packet->dts;
  }
  const bool bTimedData =
      packType == PackType::video || packType == PackType::audio;
  // 第一个数据包的时间做为基准时间; 配置文件不参与定基准
  if (basePts == AVOX_NOVALID_PTS && bTimedData && validTs(packet->dts)) {
    // 用首帧 dts 做基准(非 pts): B帧流首帧 dts 可能为负(dts<=pts),
    // 用 dts 才能让所有包 dts-=basePts >= 0; 否则首帧(IDR)会被下方 dts<0 丢弃
    // -> 文件开头无关键帧 -> 播放器前几秒绿屏花屏(到下一个 IDR 才恢复)
    basePts = packet->dts;
    LOGFLF(LogLevel::info, "base pts:", basePts);
  }
  if (basePts == AVOX_NOVALID_PTS) {
    if (bTimedData) {
      // 时间轴还没建立且本包时间无效: 丢了比写出 INT64_MIN 安全
      return;
    }
  } else {
    if (validTs(packet->pts)) {
      packet->pts -= basePts;
    }
    if (validTs(packet->dts)) {
      packet->dts -= basePts;
    }
  }
  // 派发当前pts给MediaMuxer,用于onProgress
  // 有视频轨只用视频PTS,无视频才用音频(避免音视频交叉跳变,音频易出2x/9x变速)
  bool bVideoPkt = (packType == PackType::video);
  bool bAudioPkt = (packType == PackType::audio);
  if (muxer && packet->pts >= 0 && (bHaveVideo ? bVideoPkt : bAudioPkt)) {
    muxer->onProgress(packet->pts);
  }
  if (packType == PackType::video || packType == PackType::vconfig) {
    VCodecId vcodecId = vDesc.codecId;
    AvoxPacket videoData = getPacket(packet);
    uint8_t nalu = getNalUnit(vcodecId, videoData);
    // 如果是AUD帧,很可能是与数据帧同PTS,在这直接放弃,以免写入出现问题
    if (naluDropAble(vcodecId, nalu)) {
      return;
    }
    // G14: 无任何 VCL 的纯 SEI 访问单元直接丢, 不落成开头孤立 sample
    if (pureNonVclAu(vcodecId, videoData)) {
      LOGFLF(LogLevel::debug, "drop pure sei access unit, dts:", packet->dts);
      return;
    }
    // config包(vconfig): 不参与合并, 直通
    if (packType == PackType::vconfig) {
      onPushPacket(videoData);
      return;
    }
    // dts<0的包是PTS无效的帧(被alignPacketPts reset成0后减basePts),
    // 这些帧的时间戳是错的, 写入MP4会在错误时间点闪现画面, 直接丢弃
    if (packet->dts < 0) {
      return;
    }
    // 同dts=同一帧的多个slice, 用append合并; dts变=新帧, 先flush再开新的
    // 首包 dts 常缺失(被补成 pts), 与下一帧的 dts 撞车; 同帧必同 pts,
    // 只按 dts 合并会把下一帧的 slice 并进 IDR, 开头整段解不出来
    if (preBuffer && packet->dts == preBuffer->dts &&
        packet->pts == preBuffer->pts) {
      preBuffer->append(*packet);
      // LOGFLF(LogLevel::info, "au merge dts:", packet->dts,
      //        " nalu:", getNalName(vcodecId, nalu),
      //        " size:", preBuffer->size);
    } else {
      flushPendingAu();
      preBuffer = std::make_shared<PacketBuf>();
      preBuffer->form(*packet);
      // 新 AU 起点才计入体检: 上面 append 分支是同帧多 slice(同 dts), 算不得新帧;
      // 而此前被丢弃的包(dts<0 / 纯SEI / AUD)根本没交给 muxer, 也不该计入
      noteMuxDts(packet->dts, (int32_t)PackType::video);
    }
  } else if (packType == PackType::audio || packType == PackType::aconfig) {
    // 音频与视频是不同stream, dts各自单调, 音频不需要冲视频AU
    AvoxPacket audioData = getPacket(packet);
    if (packType == PackType::audio) {
      // 配置包(aconfig)不是样本, 不参与 dts 单调性体检
      noteMuxDts(packet->dts, (int32_t)PackType::audio);
    }
    onPushPacket(audioData);
  }
}

void IOMuxer::onError(AVError err, const char* msg) {
  LOGFLF(LogLevel::error, "error:", (int32_t)err, " msg:", msg);
  muxer->dispatch(&IRecorderOb::onIoError, err, msg);
}

void IOMuxer::flushPendingAu() {
  if (!preBuffer) {
    return;
  }
  AvoxPacket pkt = getPacket(*preBuffer);
  onPushPacket(pkt);
  preBuffer = nullptr;
}

void IOMuxer::close() {
// 只 stopTask(join) 让写线程退出即可: onRunTask 退出时会 flushPendingAu 冲掉最后挂起的 AU。
// 不能在 stopTask 前 flush: pushPacket(写线程) 与 flushPendingAu 并发读写 preBuffer -> 堆破坏
  stopTask();
  onClose();
}

void IOMuxer::onRunTask() {
  std::vector<uint8_t> tdata(10000);
  PacketBufPtr tempPtr = std::make_shared<PacketBuf>(tdata);
  bool bIFrmae = false;
  bool bConfigChange = false;
  // G13 兜底: 停止后的排空阶段有界(10s), 防 close 的 join 被滞留的写线程
  // 永久挂住(根因未定的偶发卡死)。超时弃排直出, onClose 仍写 trailer 保
  // moov, 产物可播; 运行期(running=true)不受影响
  bool bStopSeen = false;
  std::chrono::steady_clock::time_point stopAt;
  const auto kDrainTimeout = std::chrono::seconds(10);
  while (running() || !packetQueue.empty()) {
    if (!running()) {
      const auto now = std::chrono::steady_clock::now();
      if (!bStopSeen) {
        bStopSeen = true;
        stopAt = now;
      } else if (now - stopAt > kDrainTimeout) {
        LOGFLF(LogLevel::warn,
               "muxer drain timeout on close, abandon rest, trailer still "
               "written");
        break;
      }
    }
    PackType packtype = PackType::other;
    bool bGet = packetQueue.dequeueAction([&](const PacketBufPtr& packetPtr) {
      packtype = (PackType)(packetPtr->packtype);
      // 配置帧
      if (packtype == PackType::vconfig || packtype == PackType::video) {
        if (packtype == PackType::vconfig) {
          ConfigAddType configAddType =
              addConfigPacket(videoConfigs, *packetPtr, vDesc.codecId);
          bConfigChange = configAddType != ConfigAddType::duplicate;
        } else {
          // 有I帧来了,可以初始化了
          if (packetPtr->frameType == 1) {
            bIFrmae = true;
          }
          tempPtr->form(*packetPtr);
        }
      } else if (packtype == PackType::aconfig || packtype == PackType::audio) {
        if (packtype == PackType::aconfig) {
          // 配置帧记录下来
          audioConfig = std::make_shared<PacketBuf>(*packetPtr);
        } else {
          tempPtr->form(*packetPtr);
        }
      }
    });
    if (bGet) {
      // 如果有视频，等I帧来,I帧前需要保证有配置帧,有配置帧才能初始化流
      if (bHaveVideo) {
        if (bIFrmae && packtype == PackType::video) {
          if (!bInitStreams) {
            // [dbg] ENH_VKDBG=1: onInit 触发点与配置帧数量
            {
              static const bool dbg = std::getenv("ENH_VKDBG") != nullptr;
              static std::atomic<int32_t> cnt{0};
              if (dbg && cnt.fetch_add(1) < 5) {
                fprintf(stderr, "[dbg] IOMuxer onInit attempt, configs=%u\n",
                        (unsigned)videoConfigs.size());
              }
            }
            bInitStreams = onInit();
          }
        }
      } else if (bHaveAudio && packtype == PackType::audio) {
        if (!bInitStreams) {
          bInitStreams = onInit();
        }
      }
      if (bInitStreams) {
        if (packtype == PackType::video) {
          // 如果是I帧,并且配置帧发生变化
          if (bIFrmae && bConfigChange) {
            // 首帧配置已在 extradata 里; 此刻 basePts 未定, 补写的配置包会带
            // 源时间戳抢跑, 把封装器 cur_dts 顶到 I 帧之前, IDR 反被当成回退丢弃
            if (basePts != AVOX_NOVALID_PTS) {
              int32_t configSize = videoConfigs.size();
              for (auto& config : videoConfigs) {
                PacketBufPtr configPtr = std::make_shared<PacketBuf>(config);
                configPtr->pts = tempPtr->pts - configSize;
                configPtr->dts = tempPtr->dts - configSize;
                pushPacket(configPtr);
                configSize--;
              }
            }
            bConfigChange = false;
          }
          pushPacket(tempPtr);
        } else if (packtype == PackType::audio) {
          pushPacket(tempPtr);
        }
        // 流头已写: 补写初始化前先到的音频(此时 basePts 已由首个视频包定下)
        while (!pendingAudio.empty()) {
          pushPacket(pendingAudio.front());
          pendingAudio.pop_front();
        }
      } else {
        // 初始化失败,直接退出
        if (bInitFailed) {
          break;
        }
        // 首个视频I帧到达前音频常已解出: 直丢会让产物开头少几秒声音,
        // 有界缓存, 未等到I帧时丢最旧的保时间上连续
        if (packtype == PackType::audio) {
          constexpr size_t kMaxPendingAudio = 2048;
          if (pendingAudio.size() >= kMaxPendingAudio) {
            pendingAudio.pop_front();
          }
          pendingAudio.push_back(std::make_shared<PacketBuf>(*tempPtr));
        }
      }
    }
    // 本轮到包 → 让出时间片立即继续; 队列空(等上游) → 阻塞睡, 不空转。
    // (旧写法无条件 yield, 参数被丢弃 —— 队列空时就是纯自旋)
    sleepTask(bGet);
  }
  // worker线程退出, 冲掉最后一个挂起的视频AU
  flushPendingAu();
}

}
