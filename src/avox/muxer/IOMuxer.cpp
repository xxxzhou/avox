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
// (HDR 带内元数据随帧)不命中, 不会误杀。首 NAL 是 SEI 才整包扫描, 常规包零开销
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

void IOMuxer::pushPacket(PacketBufPtr packet) {
  // 无running()守卫: 本方法仅worker线程(onRunTask)调用, close后仍需排干
  // 队列残留包(离线快速转封装时尾部几十帧全在此), 丢弃会截短成片;
  // 关闭后写的安全性由onPushPacket的fmtCtx空保护兜底
  PackType packType = (PackType)packet->packtype;
  // 第一个包的时间做为基准时间
  if (basePts == AVOX_NOVALID_PTS) {
    // 用首帧 dts 做基准(非 pts): B帧流首帧 dts 可能为负(dts<=pts),
    // 用 dts 才能让所有包 dts-=basePts >= 0; 否则首帧(IDR)会被下方 dts<0 丢弃
    // -> 文件开头无关键帧 -> 播放器前几秒绿屏花屏(到下一个 IDR 才恢复)
    basePts = packet->dts;
    LOGFLF(LogLevel::info, "base pts:", basePts);
  }
  packet->pts -= basePts;
  packet->dts -= basePts;
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
      LOGFLF(LogLevel::info, "drop pure sei access unit, dts:", packet->dts);
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
    if (preBuffer && packet->dts == preBuffer->dts) {
      preBuffer->append(*packet);
      // LOGFLF(LogLevel::info, "au merge dts:", packet->dts,
      //        " nalu:", getNalName(vcodecId, nalu),
      //        " size:", preBuffer->size);
    } else {
      flushPendingAu();
      preBuffer = std::make_shared<PacketBuf>();
      preBuffer->form(*packet);
    }
  } else if (packType == PackType::audio || packType == PackType::aconfig) {
    // 音频与视频是不同stream, dts各自单调, 音频不需要冲视频AU
    AvoxPacket audioData = getPacket(packet);
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
  // (0xC0000409)。preBuffer 生命周期完全归于写线程(pushPacket 写 / onRunTask 退出 flush)。
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
            int32_t configSize = videoConfigs.size();
            for (auto& config : videoConfigs) {
              PacketBufPtr configPtr = std::make_shared<PacketBuf>(config);
              configPtr->pts = tempPtr->pts - configSize;
              configPtr->dts = tempPtr->dts - configSize;
              pushPacket(configPtr);
              configSize--;
            }
            bConfigChange = false;
          }
          pushPacket(tempPtr);
        } else if (packtype == PackType::audio) {
          pushPacket(tempPtr);
        }
      } else {
        // 初始化失败,直接退出
        if (bInitFailed) {
          break;
        }
      }
    }
    // 如果是视频包PTS相同需要合并的情况,快速处理
    // sleepTask(packetQueue.size() > 10, 5);
    sleepTask(true);
  }
  // worker线程退出, 冲掉最后一个挂起的视频AU
  flushPendingAu();
}

}
