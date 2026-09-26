#include "AVSource.hpp"

#include "../module/OptionKey.hpp"
#include "../codec/H26XHelper.hpp"
#include "../module/LogHelper.hpp"
#include "../video/PocRestamper.hpp"

namespace avox {

// 异常PTS阈值: -1000000000(约11天), 合法的流不会出现这么大的负值;
// ffmpeg拉流可能出现特别小的值如-102481911520608620,会打穿下游
constexpr int64_t kInvalidPtsThreshold = -1000000000LL;

AVSource::AVSource() {
  videoInfo.type = TrackType::video;
  audioInfo.type = TrackType::audio;
  vIndexMaps.resize(4, 0);
  aIndexMaps.resize(4, 0);
  // MKV 动漫源常见 10+ 字幕轨
  sIndexMaps.resize(32, 0);
}

AVSource::~AVSource() {}

void AVSource::setSpeed(double speed_) {
  if (speed == speed_) {
    return;
  }
  speed = speed_;
  // 先让 IO 侧应用倍速并标定 bSpeedAble
  onSpeed();
  // 倍速时预判进入I帧模式，收到P/B帧会自动退出。
  // 仅对 IO 有真倍速语义的源(zlmediakit): 服务端切流有 I 帧窗口期。
  // ffmpeg 本地/VOD 包流不随倍速改变, 预判只会静音音频+把时钟交给
  // 静音车道硬灌(2x 下 pos 锯齿、tap 断流, tempo.pipe.* 实证), 且退出
  // 依赖 P/B 包抵达 singleVideo, 视频消费链一旦背压就可能长期滞留。
  if (speed > 1 && bSpeedAble && !bIFrameMode) {
    bIFrameMode = true;
    iFrameCount = 0;
    lastIFramePts = AVOX_NOVALID_PTS;
    dispatch(&IAVSourceOb::onIFrameMode, true);
  } else if (speed <= 1 && bIFrameMode) {
    bIFrameMode = false;
    iFrameCount = 0;
    lastIFramePts = AVOX_NOVALID_PTS;
    dispatch(&IAVSourceOb::onIFrameMode, false);
  }
}

bool AVSource::open(const char* url_) {
  url = url_;
  bOpen = true;
  bAVAlign = false;
  bCheckAcc = false;
  bIFrameMode = false;
  iFrameCount = 0;
  lastIFramePts = AVOX_NOVALID_PTS;
  bDiscardDupGop = false;
  bSeeking = false;
  iFrameHistory.clear();
  videoTracks.clear();
  audioTracks.clear();
  // bDisableVideo = true;
  // bDisableAudio = true;
  return onOpen();
}

void AVSource::close() {
  onClose();
  bOpen = false;
  url.clear();
}

ISourceInfo* AVSource::getSourceInfo() { return this; }

void AVSource::onTrackOpen() {
  baseTimeMS = AVOX_NOVALID_PTS;
  bSyncPts = false;
  bSendConfig = false;
  bDiscardDupGop = false;
  bSeeking = false;
  iFrameHistory.clear();
  videoInfo.trackSize = videoTracks.size();
  videoInfo.tracks.resize(videoTracks.size());
  for (int32_t i = 0; i < videoTracks.size(); i++) {
    vIndexMaps[videoTracks[i].trackId] = i;
  }
  videoInfo.reset();
  audioInfo.trackSize = audioTracks.size();
  audioInfo.tracks.resize(audioTracks.size());
  for (int32_t i = 0; i < audioTracks.size(); i++) {
    aIndexMaps[audioTracks[i].trackId] = i;
  }
  audioInfo.reset();
  // 字幕轨映射重置(容量不足时扩到 trackId+1, 防越界)
  for (int32_t i = 0; i < (int32_t)subtitleTracks.size(); i++) {
    const int32_t trackId = subtitleTracks[i].trackId();
    if (trackId >= (int32_t)sIndexMaps.size()) {
      sIndexMaps.resize(trackId + 1, 0);
    }
    sIndexMaps[trackId] = i;
  }
  //
  vconfigPackets.clear();
  aconfigPacket = nullptr;
  bAVAlign = videoInfo.trackSize > 0 && audioInfo.trackSize > 0;
  dispatch(&IAVSourceOb::onReady);
}

void AVSource::onOptionChange(const char* key, ArgType type) {
  if (!option) {
    return;
  }
  if (equalsIgnoreCase(key, AVOX_MP_IO_TIMEOUT_MS_INT)) {
    timeoutMs = option->getInt(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", timeoutMs);
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_RTSP_TRANSPORT_STR)) {
    rtspTransport = option->getString(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", rtspTransport);
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_RTSP_SPEED_DOUBLE)) {
    setSpeed(option->getDouble(key));
    LOGFLF(LogLevel::info, "option:", key, " change:", speed);
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_TRACK_READY_MS_INT)) {
    trackReadyMs = option->getInt(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", trackReadyMs);
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_HTTP_PERSISTENT_INT)) {
    httpPersistent = option->getInt(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", httpPersistent);
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_HTTP_HEADERS_STR)) {
    httpHeaders = option->getString(key);
    // 头里可能带鉴权信息(Access-Token 等), 不落日志值
    LOGFLF(LogLevel::info, "option:", key, " change: len=",
           (int32_t)httpHeaders.size());
  } else if (equalsIgnoreCase(key, AVOX_MP_IO_HTTP_USERAGENT_STR)) {
    httpUserAgent = option->getString(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", httpUserAgent);
  } else if (equalsIgnoreCase(key, AVOX_LOG_SOURCE_INPACKET_BOOL)) {
    bLogPacket = option->getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bLogPacket);
  }
}

void AVSource::processPacket(AvoxPacket& packet) {
  PackType type = (PackType)packet.packtype;
  // 字幕旁路: 只做 index 映射后直接下发, 不进音视频同步/基准链
  // (字幕由播放时钟旁路消费, 见计划 doc/plan/player/ASS字幕渲染计划.md §3.2)
  if (type == PackType::subtitles || type == PackType::sconfig) {
    if (packet.index >= 0 && packet.index < (int32_t)sIndexMaps.size()) {
      packet.index = sIndexMaps[packet.index];
      dispatch(&IAVSourceOb::onPacket, packet);
    }
    return;
  }
  // 全局packet.index映射到A/V对应的index
  if (type == PackType::video || type == PackType::vconfig) {
    packet.index = vIndexMaps[packet.index];
    // 视频包检查一次annexb/avcc
    if (type == PackType::video) {
      if (!bCheckAcc) {
        bvcc = checkAvccPacket(packet.data.data, packet.data.size);
        LOGFLF(LogLevel::info, "bvcc:", bvcc);
        bCheckAcc = true;
      }
    }
    // 视频包可能需要拆包
    processVideo(packet);
  } else if (type == PackType::audio || type == PackType::aconfig) {
    // 精确seek: 丢目标位前的音频包, 首个>=目标位的包恢复通行并自解除
    // (优先于保护期持有; NOPTS透传由音频轨nextPts自推进, 不判不卡闸)
    if (type == PackType::audio &&
        preciseSeekPts.load() != AVOX_NOVALID_PTS) {
      if (packet.pts != AVOX_NOVALID_PTS) {
        if (packet.pts < preciseSeekPts.load()) {
          return;
        }
        preciseSeekPts = AVOX_NOVALID_PTS;
      }
    } else if (type == PackType::audio && bSeeking && !videoTracks.empty()) {
      // seek保护期(等首个视频I帧)内音频数据包一并丢弃: 避免"音频先跑、画面
      // 冻结"的错轴体感(2026-09-24 拍板: 人首先关注画面); IDR到达 bSeeking
      // 清除后音视频一起恢复。
      // 无视频轨的源不持有(bSeeking没有视频I帧来清除, 会永久静音)
      return;
    }
    packet.index = aIndexMaps[packet.index];
    alignPacketPts(packet);
    if (bLogPacket) {
      int64_t baseTime = audioInfo.basePts;
      if (baseTime != AVOX_NOVALID_PTS) {
        baseTime = packet.pts - baseTime;
      } else {
        baseTime = 0;
      }
      log(LogLevel::info, "---audio packet pts:", packet.pts,
          " base pts:", baseTime, " dts:", packet.dts,
          " size:", packet.data.size);
    }
  }
}

void AVSource::processVideo(AvoxPacket& packet) {
  // log(LogLevel::info, "pts:", packet.pts, " dts:", packet.dts);
  // 视频配置帧,有些IO可能会把多个配置帧与一个I帧合并发送
  VCodecId vcodecId = videoTracks[packet.index].codecId;
  // 非NALU编码(rv40/wmv3/vc1/mpeg2等)整包直发: 码流里的00 00 01只是巧合
  // 字节, annexb拆片从起始码起头, 首个伪起始码之前的前导字节不落入任何
  // 分片被丢弃, 回拼后整帧截断 → 参考链污染全程花屏 (RM/RV40 实证)
  if (vcodecId != VCodecId::h264 && vcodecId != VCodecId::h265) {
    singleVideo(packet);
    return;
  }
  if (packet.data.size <= 4) {
    return;
  }
  // 检查nalu是三还是4
  uint8_t* pdata = packet.data.data;
  // 有可能是annexb3,也有可能是avcc/hvcc4
  if (!bvcc && pdata[0] == 0 && pdata[1] == 0 && pdata[2] == 1) {
    packet.prefixSize = 3;
  } else {
    // annexb 4字节以及avcc/hvcc都是4字节
    packet.prefixSize = 4;
  }
  // 丢ctts的B帧流修复: pts按POC平移到显示格(正常流零改写)。
  // 在拆包前改, 拆出的配置/SEI/帧子包共享同一父pts, SEI与帧不脱钩
  if (!pocRestamper) {
    pocRestamper = std::make_unique<PocRestamper>();
    double fps = 0;
    if (packet.index >= 0 && (size_t)packet.index < videoTracks.size()) {
      fps = videoTracks[packet.index].desc.fps;
    }
    pocRestamper->setup(vcodecId, fps);
  }
  pocRestamper->feed(packet);
  // 几个点.
  // 组合类型(配置帧组+I帧)(配置帧组+SEI+I帧)
  // 拆分类型(多个I帧)(多个P帧)
  // IOS,非avcc,解码时要拆包并把annexb转成avcc/hvcc
  // android,非annexb,解码时要拆包并把avcc/hvcc转成annexb
  // 推流,非annexb,拆包并把avcc/hvcc转成annexb
  if (bvcc) {
    uint32_t naluLength = (packet.data.data[0] << 24) |
                          (packet.data.data[1] << 16) |
                          (packet.data.data[2] << 8) | packet.data.data[3];
    if (packet.data.size > naluLength) {
      splitAvccNalu(packet, spiltBufs);
    }
  } else {
    // 需不需要只是配置帧开头才拆?
    splitAnnexbNalu(packet, spiltBufs);
  }
  // 如果能拆分多个包,每个包单独处理
  if (spiltBufs.size() > 1) {
    combineBufs.clear();
    // 分开的I帧与P帧合并
    for (auto& buf : spiltBufs) {
      bool bNewFrame = naluNewFrame(vcodecId, buf.data.data + buf.prefixSize);
      if (bNewFrame) {
        combineBufs.push_back(buf);
      } else {
        if (combineBufs.size() > 0) {
          AvoxPacket& lastBuf = combineBufs.back();
          // 前一个是配置帧(VPS/SPS/PPS)时,不把后续SEI等非配置NALU并进配置包,
          // 否则addConfigPacket整段比较会把变化的SEI误判成update,触发假更新;
          // SEI 单独成组随流下发(载 HDR 元数据,解码侧靠它兜底)
          if (naluConfigFrame(vcodecId, getNalUnit(vcodecId, lastBuf))) {
            combineBufs.push_back(buf);
          } else {
            // 上面的spilt只是记录包指针偏移,这个buff是连续的
            lastBuf.data.size += buf.data.size;
          }
        } else {
          // 帧前置的非新帧 NAL(典型为前缀 SEI): 作为合并组起点下发。
          // 此前直接丢弃——ffmpeg>=9 已不从带内 SEI 导出 MDCV/CLL,
          // HDR 元数据必须随流到达解码器
          combineBufs.push_back(buf);
        }
      }
    }
    // log(LogLevel::info, "spilt bufs:", spiltBufs.size(),
    //     " combin bufs:", combineBufs.size());
    for (auto& buf : combineBufs) {
      singleVideo(buf);
    }
  } else {
    singleVideo(packet);
  }
}

// 整组扫描随机访问NAL(h264:IDR / h265:IRAP), 返回其长度前缀在组内的字节
// 偏移(可直接作截断起点), 无则-1. 分组启发式(h264NaluNewFrame的first_mb
// 高位检查)会把IDR切片误判成"未起新帧"并进前帧组(霍小玉.mkv实证), 组首
// NAL判不出关键帧, seek落点闸需要全组扫描
static int32_t groupRandomAccessOffset(const AvoxPacket& packet, VCodecId vc,
                                       bool bvcc) {
  const uint8_t* p = packet.data.data;
  const int32_t size = packet.data.size;
  const int32_t prefix = packet.prefixSize;
  int32_t i = prefix;
  int32_t guard = 0;
  while (i > 0 && i < size && guard++ < 1024) {
    bool bIdr = false;
    if (vc == VCodecId::h264) {
      bIdr = naluKeyFrame(VCodecId::h264, p[i] & 0x1F);
    } else {
      bIdr = naluKeyFrame(VCodecId::h265, (p[i] >> 1) & 0x3F);
    }
    if (bIdr) {
      return i - prefix;
    }
    if (bvcc) {
      int32_t len = 0;
      for (int32_t k = 0; k < prefix; ++k) {
        len = (len << 8) | p[i - prefix + k];
      }
      if (len <= 0 || i + len > size) {
        break;
      }
      i = i + len + prefix;
    } else {
      // annexb: 找下一个起始码
      while (i + 2 < size && !(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)) {
        ++i;
      }
      i += 3;
    }
  }
  return -1;
}

void AVSource::singleVideo(AvoxPacket& packet) {
  VCodecId vcodecId = videoTracks[packet.index].codecId;
  uint8_t nalu = getNalUnit(vcodecId, packet);
  // AUD单独成包,每帧喂一个无slice的空包,解码器会持续刷"no frame!"。
  // 但[AUD][SEI..]合并组不能整组丢——SEI载带内HDR元数据(mdcv/clli),
  // 跳过头AUD按下一NAL重新起头; 丢弃点必须在这里而不是合并循环:
  // 合并循环跳AUD会破坏组内连续性(size扩展跨过跳过的字节,包内容错位)
  if (naluDropAble(vcodecId, nalu)) {
    std::vector<AvoxPacket> nalus;
    if (bvcc) {
      splitAvccNalu(packet, nalus);
    } else {
      splitAnnexbNalu(packet, nalus);
    }
    if (nalus.size() <= 1) {
      return;
    }
    const AvoxPacket& next = nalus[1];
    packet.data.size -= (int32_t)(next.data.data - packet.data.data);
    packet.data.data = next.data.data;
    packet.prefixSize = next.prefixSize;
    nalu = getNalUnit(vcodecId, packet);
  }
  // log(LogLevel::info, "nalu:", (int32_t)nalu, "-", getNalName(vcodecId,
  // nalu));
  bool bConfig = naluConfigFrame(vcodecId, nalu);
  if (bConfig) {
    packet.packtype = (int32_t)PackType::vconfig;
    // 每个I帧前确认下,有就不再发了,否则就发保存下来的
    bSendConfig = true;
    updateConfig(packet);
  } else {
    packet.packtype = (int32_t)PackType::video;
  }
  // 关键帧: h264/h265按NALU类型判定; 其余编码(wmv3/vc1/rv/mpeg4等)无nalu
  // 结构, naluKeyFrame恒false会覆盖成0导致基准pts永不设定, 保持容器
  // AV_PKT_FLAG_KEY标记(ffAvoxPacket已带入)。
  // 注意容器标志只用于基准pts对齐: 下游I帧模式检测与muxer配置重发仍按
  // nalu语义(非h26x恒false) — mpegts类容器可能全包标KEY, 直接采用会误入
  // I帧模式且无法退出(P/B帧也是"key")
  bool bKeyFrame = false;
  // 容器KEY标记(ffAvoxPacket带入)先留存: h26x随即被nalu语义覆盖。mkv索引的
  // 关键帧=封装器认定的可seek点, 含open-GOP恢复点(修复版重封装可能全流无
  // 真IDR, 玉蒲团实测扫3.7min无nal:5), 闸靠它识别恢复点入口
  int32_t containerKey = packet.frameType;
  if (vcodecId == VCodecId::h264 || vcodecId == VCodecId::h265) {
    bKeyFrame = naluKeyFrame(vcodecId, nalu);
    packet.frameType = bKeyFrame ? 1 : 0;
  }
  // seek落点IDR闸(仅h264/h265): 容器KEY标志不可信(全包标KEY的容器误入I帧
  // 模式), 但mkv索引关键帧可信(见containerKey注释). 保护期内找入口: 真IDR
  // 截断起步 > 容器关键帧组恢复点起步 > 都没有则丢弃直到出现(解码器从P/B
  // 中间起步POC/frame_num断裂, 实测霍小玉.mkv Frame num gap螺旋到0帧).
  // bSeeking由checkDupGop在首个video I帧(frameType==1)解除, 丢的包不经
  // alignPacketPts, 不会被提前解除
  //
  // 注意组首NAL不可信: h264NaluNewFrame的first_mb_in_slice高位启发式会把
  // IDR切片误判成"未起新帧"而并进前帧组(实证: 霍小玉.mkv的IDR包组首是
  // B帧切片), 所以这里全组扫描随机访问NAL, 找到就把组截断到它起步——
  // 截掉的前导B/P切片本就缺参考, 喂给解码器只会产出灰帧
  int32_t idrOff = -1;
  if (bSeeking && !bKeyFrame && !bConfig &&
      (vcodecId == VCodecId::h264 || vcodecId == VCodecId::h265)) {
    idrOff = groupRandomAccessOffset(packet, vcodecId, bvcc);
    // 入口选择(优先级从上到下):
    // ① 组内真IDR(nal:5): 截断到IDR起解, 最干净入口
    // ② 容器关键帧组(open-GOP恢复点): 直接作入口按I帧放行——解码器seek时
    //    已整体重建(干净POC)+OUTPUT_CORRUPT, 恢复期短花屏可容忍(VLC同款);
    //    全流无真IDR的修复版重封装若硬等IDR, 只能防呆放行解缺参考垃圾
    //    (panvox 62min处seek实测: 黑屏数秒+Missing reference刷屏)
    // ③ 都不是(索引坏落GOP中间的P/B): 一直丢到下一个IDR/关键帧组, 丢弃是
    //    IO速度远快于解垃圾; 仅kSeekIdrDropMax防呆防真无关键帧流永久黑屏
    if (idrOff < 0 && containerKey != 1) {
      ++seekIdrDrops;
      if (seekIdrDrops <= kSeekIdrDropMax) {
        // 首几包+每500包留痕, 长丢弃期不逐包刷屏
        if (seekIdrDrops <= 5 || seekIdrDrops % 500 == 0) {
          LOGFLF(LogLevel::info, "seek idr gate drop ", seekIdrDrops,
                 " nal:", (int32_t)nalu, " pts:", packet.pts,
                 " size:", packet.data.size);
        }
        return;
      }
      if (seekIdrDrops == kSeekIdrDropMax + 1) {
        LOGFLF(LogLevel::warn, "seek idr gate: no idr/keyframe in ",
               seekIdrDrops - 1, " packets, passing through");
      }
      // 超防呆: 垃圾放行走正常处理, 不再逐包留痕
    } else if (idrOff < 0) {
      LOGFLF(LogLevel::info, "seek idr gate: recovery keyframe as entry after ",
             seekIdrDrops, " drops, pts:", packet.pts);
      bKeyFrame = true;
      packet.frameType = 1;
    } else {
      LOGFLF(LogLevel::info, "seek idr gate: idr at ", idrOff, " after ",
             seekIdrDrops, " drops, pts:", packet.pts);
      // 截断到随机访问NAL的长度前缀起头, 组内前导切片丢弃
      packet.data.data += idrOff;
      packet.data.size -= idrOff;
      packet.prefixSize = bvcc ? 4 : 3;
      bKeyFrame = true;
      packet.frameType = 1;
      nalu = getNalUnit(vcodecId, packet);
    }
  }
  if (bSeeking && bKeyFrame && seekIdrDrops > 0) {
    LOGFLF(LogLevel::info, "seek idr gate passed after ", seekIdrDrops,
           " drops, nal:", (int32_t)nalu, " pts:", packet.pts);
  }
  // I帧模式检测：连续不同PTS的I帧数据包，说明只有I帧没有P/B帧
  if (!bConfig) {
    if (bKeyFrame) {
      if (packet.pts != lastIFramePts) {
        iFrameCount++;
        lastIFramePts = packet.pts;
        if (iFrameCount >= 2 && !bIFrameMode) {
          bIFrameMode = true;
          dispatch(&IAVSourceOb::onIFrameMode, true);
        }
      }
    } else {
      // P/B帧 → 退出I帧模式
      if (bIFrameMode) {
        bIFrameMode = false;
        dispatch(&IAVSourceOb::onIFrameMode, false);
      }
      iFrameCount = 0;
      lastIFramePts = AVOX_NOVALID_PTS;
    }
  }
  // 挂接的Muxer,要求I帧前发送配置帧
  if (bKeyFrame) {
    // ffmpeg本地源,原来只在最开始有配置帧,改为I帧前发一次
    if (preIFramePts != packet.pts && !bSendConfig &&
        vconfigPackets.size() > 0) {
      int32_t cindex = vconfigPackets.size();
      for (auto& buf : vconfigPackets) {
        AvoxPacket vsp = {};
        vsp.prefixSize = buf.prefixSize;
        vsp.packtype = (int32_t)PackType::vconfig;
        vsp.frameType = 0;
        vsp.pts = packet.pts - cindex;
        vsp.dts = packet.dts - cindex;
        vsp.data.data = buf.buff.data();
        vsp.data.size = buf.size;
        vsp.data.bRef = true;
        alignPacketPts(vsp);
        cindex--;
      }
      // log(LogLevel::info, "send config packets:", vconfigPackets.size());
    }
    preIFramePts = packet.pts;
    // 配置帧在I帧之前,如果自带配置帧,则不需要发送
    bSendConfig = false;
  }
  alignPacketPts(packet);
  if (bLogPacket) {
    int64_t baseTime = videoInfo.basePts;
    if (baseTime != AVOX_NOVALID_PTS) {
      baseTime = packet.pts - baseTime;
    } else {
      baseTime = 0;
    }
    log(LogLevel::info, "---video packet pts:", packet.pts,
        " base pts:", baseTime, " dts:", packet.dts, " size:", packet.data.size,
        " nul:", getNalName(vcodecId, nalu));
  }
  // AvoxData tempData = {packet.data.data, std::min(packet.data.size, 10),
  // true}; log(LogLevel::info, "in packet nalu:", (int32_t)nalu, " key:",
  // bKeyFrame,
  //     " config:", bConfig, " pts:", packet.pts, " size:", packet.data.size,
  //     " data:", tempData);
}

bool AVSource::reviseInvalidPts(AvoxPacket& packet, TrackInfos& info) {
  // 仅视频进入: 垃圾/NOPTS按fps递推合成; 音频在alignPacketPts归一为NOPTS透传
  if (packet.pts < kInvalidPtsThreshold) {
    // FFmpeg解封装HLS时, 部分I帧组(VPS/SPS/PPS/IDR)的PTS/DTS是垃圾值
    // (如-102481911520608620), zlmediakit拉流无此问题
    // 修正为prePts+1, 同一帧的多个NAL(连续invalid pts)共享同一个修正值,
    // 保证同帧NAL的PTS一致, 下游可以按dts合并
    int64_t invalidPts = packet.pts;
    int64_t invalidDts = packet.dts;
    if (!info.tracks.empty()) {
      auto& track = info.tracks[packet.index];
        // 合成帧时长: 视频按轨道fps, 无fps信息给40ms保底
      int64_t frameDur = 40;
      // 同帧共享包数上限: h264/h265一帧可拆出参数集组(SPS/PPS/IDR=3包);
      // 无nalu结构编码(mpeg1/2/4,wmv,rv)每包即整帧, 共享会造成时间戳翻倍
      int32_t shareWindow = 1;
      if (&info == &videoInfo && packet.index >= 0 &&
          videoTracks.size() > (size_t)packet.index) {
        const auto& vt = videoTracks[packet.index];
        if (vt.desc.fps > 1.0) {
          frameDur = (int64_t)(1000.0 / vt.desc.fps);
        }
        if (vt.codecId == VCodecId::h264 || vt.codecId == VCodecId::h265) {
          // h264组SPS/PPS/IDR=3包; h265组VPS/SPS/PPS/SEI/IDR可达5包
          shareWindow = vt.codecId == VCodecId::h264 ? 3 : 5;
        }
      }
      if (track.lastRevisedPts != AVOX_NOVALID_PTS &&
          track.revisedCount < shareWindow) {
        // 同帧的后续包, 复用上一次修正的PTS
        packet.pts = track.lastRevisedPts;
        ++track.revisedCount;
      } else {
        // 新帧: prePts+帧时长递推; 首帧就垃圾兜底reset 0
        packet.pts = (track.prePts != AVOX_NOVALID_PTS && track.prePts >= 0)
                         ? track.prePts + frameDur
                         : 0;
        track.lastRevisedPts = packet.pts;
        track.revisedCount = 1;
      }
    } else {
      packet.pts = 0;
    }
    if (packet.dts < kInvalidPtsThreshold) {
      packet.dts = packet.pts;
    }
    LOGFLF(LogLevel::info, "invalid pts:", invalidPts,
           " invalid dts:", invalidDts, " revise pts:", packet.pts,
           " revise dts:", packet.dts);
    return true;
  }
  // 正常帧: 清除lastRevisedPts, 标记不在同一invalid帧内
  if (!info.tracks.empty()) {
    info.tracks[packet.index].lastRevisedPts = AVOX_NOVALID_PTS;
    info.tracks[packet.index].revisedCount = 0;
  }
  return false;
}

void AVSource::alignPacketPts(AvoxPacket& packet) {
  PackType type = (PackType)packet.packtype;
  // 修正异常PTS/DTS
  if (type == PackType::video || type == PackType::vconfig) {
    reviseInvalidPts(packet, videoInfo);
  } else if (type == PackType::audio || type == PackType::aconfig) {
    // 音频不合成时间: 源层无法知道包时长, 垃圾值归一为NOPTS透传,
    // 无效pts由AudioTrack按实际解码采样数推进(nextPts)
    if (packet.pts < kInvalidPtsThreshold) {
      packet.pts = AVOX_NOVALID_PTS;
    }
    if (packet.dts < kInvalidPtsThreshold) {
      packet.dts = AVOX_NOVALID_PTS;
    }
  }
  // 检测HLS分片重叠的重复GOP, 命中则丢弃该包(不做后续基准/同步/prePts更新)
  if (checkDupGop(packet)) {
    return;
  }
  // log(LogLevel::info, "pts:", packet.pts, " type:", getPackTypeStr(type));
  // 先确定基准时间
  if (type == PackType::video && videoInfo.basePts == AVOX_NOVALID_PTS) {
    // 视频以第一个关键帧的时间为基准
    // 或者视频有跳变，以跳变后时间为基准
    if (packet.frameType == 1) {
      videoInfo.basePts = packet.pts;
      baseTimeMS = videoInfo.basePts;
      videoInfo.noKeyPackets = 0;
      LOGFLF(LogLevel::info, "first video pts:", videoInfo.basePts,
             " base time:", baseTimeMS);
    } else if (packet.index >= 0 &&
               videoTracks.size() > (size_t)packet.index &&
               videoTracks[packet.index].codecId != VCodecId::h264 &&
               videoTracks[packet.index].codecId != VCodecId::h265 &&
               ++videoInfo.noKeyPackets == 50) {
      // 兜底(仅无nalu结构的编码): MPEG-PS/无索引AVI等容器不标关键帧,
      // 等不到I帧标志基准时间永不设定(进度恒0); 连续50包无标志采信当前包。
      // h264/h265不适用: 位流内可判定关键帧, 长GOP无I帧包是正常现象
      videoInfo.basePts = packet.pts;
      baseTimeMS = videoInfo.basePts;
      LOGFLF(LogLevel::warn,
             "no keyframe flag in 50 packets, use first pts as base:",
             videoInfo.basePts);
    }
  } else if (type == PackType::audio &&
             audioInfo.basePts == AVOX_NOVALID_PTS &&
             packet.pts != AVOX_NOVALID_PTS) {
    // 音频如果在有视频的情况下，以视频关键帧后包为基准
    if (videoInfo.trackSize > 0) {
      if (videoInfo.basePts != AVOX_NOVALID_PTS) {
        audioInfo.basePts = packet.pts;
        LOGFLF(LogLevel::info, "first audio pts:", audioInfo.basePts);
      }
    } else {
      // 如果没有视频，音频其实并不需要设置PTS
      audioInfo.basePts = packet.pts;
      baseTimeMS = packet.pts;
      LOGFLF(LogLevel::info, "first audio pts:", audioInfo.basePts,
             " base time:", baseTimeMS);
    }
  }
  // 当音频与视频的基准时间都确定后
  if (!bSyncPts && baseTimeMS != AVOX_NOVALID_PTS &&
      videoInfo.basePts != AVOX_NOVALID_PTS &&
      audioInfo.basePts != AVOX_NOVALID_PTS) {
    // 用当前进度(prePts)判断音视频是否可同步, 而非用初始基准(basePts)
    // basePts是初始对齐值不会变, prePts是实时进度, 能反映跳变后的实际偏移
    if (!checkAvSynced()) {
      LOGFLF(LogLevel::warn, "audio pre pts:", audioInfo.getPrePts(),
             " video pre pts:", videoInfo.getPrePts(),
             ",diff too large,no sync");
    } else {
      LOGFLF(LogLevel::info, "audio pre pts:", audioInfo.getPrePts(),
             " video pre pts:", videoInfo.getPrePts(),
             " base time:", baseTimeMS);
      // 如果时间差小于5s，都以baseTimeMS为基准
      videoInfo.basePts = baseTimeMS;
      audioInfo.basePts = baseTimeMS;
    }
    // 通知播放器，以自身的时间或视频为基准
    dispatch(&IAVSourceOb::onSyncPts);
    bSyncPts = true;
  }
  checkJump(packet);
  // 记录上一个包的PTS,用于检查是否有跳时间
  if (type == PackType::video || type == PackType::vconfig) {
    if (videoInfo.basePts != AVOX_NOVALID_PTS) {
      videoInfo.tracks[packet.index].prePts = packet.pts;
    }
    // log(LogLevel::info, "video pts:", packet.pts, " dts:", packet.dts);
  } else if (type == PackType::audio || type == PackType::aconfig) {
    // 无效pts不更新prePts, 保持最后一个真实值供跳变检测与position
    if (audioInfo.basePts != AVOX_NOVALID_PTS &&
        packet.pts != AVOX_NOVALID_PTS) {
      audioInfo.tracks[packet.index].prePts = packet.pts;
    }
  }
  dispatch(&IAVSourceOb::onPacket, packet);
}

void AVSource::updateConfig(const AvoxPacket& data) {
  if (data.packtype == (int32_t)PackType::vconfig) {
    VCodecId vcodecId = videoTracks[data.index].codecId;
    ConfigAddType type = addConfigPacket(vconfigPackets, data, vcodecId);
    // 配置帧有变化
    if (type == ConfigAddType::update) {
      uint8_t nalu = getNalUnit(vcodecId, data);
      log(LogLevel::info, "update video config, nalu:", (int32_t)nalu,
          " type:", getNalName(vcodecId, nalu), " config data:", data.data);
    } else if (type == ConfigAddType::add) {
      uint8_t nalu = getNalUnit(vcodecId, data);
      log(LogLevel::info, "add video config, nalu:", (int32_t)nalu,
          " type:", getNalName(vcodecId, nalu), " config data:", data.data);
    }
  } else if (data.packtype == (int32_t)PackType::aconfig) {
    aconfigPacket = std::make_shared<PacketBuf>(data);
    AvoxData packet = {data.data.data, std::max(data.data.size, 10), true};
    log(LogLevel::info, "audio config pts:", data.pts, " data:", packet);
  }
}

void AVSource::checkJump(AvoxPacket& packet) {
  // 如果只有一种类型的流,不需要检查
  if (!bAVAlign || packet.pts == AVOX_NOVALID_PTS) {
    return;
  }
  PackType type = (PackType)packet.packtype;
  // 只处理数据包
  if (type != PackType::video && type != PackType::audio) {
    return;
  }
  // 始终读取两个track的prePts, 用于日志输出跳变方向
  int64_t videoPrePts = videoInfo.getPrePts();
  int64_t audioPrePts = audioInfo.getPrePts();
  int64_t spanPaket = 0;
  if (type == PackType::video) {
    if (videoPrePts == AVOX_NOVALID_PTS) {
      return;
    }
    spanPaket = std::abs(packet.pts - videoPrePts);
  } else if (type == PackType::audio) {
    if (audioPrePts == AVOX_NOVALID_PTS) {
      return;
    }
    spanPaket = std::abs(packet.pts - audioPrePts);
  }
  // 如果时间跳了
  if (spanPaket > maxSyncAvTime) {
    // 只标记同步失效, 通知播放器重新同步
    // 不修改basePts: 跳变可能是分片重叠(PTS会追上)或正常的seek/断线恢复,
    // 改basePts反而会破坏已有的时间基准, 导致baseTimeMS和相对时间全部乱掉
    LOGFLF(LogLevel::warn, "pts jump,now pts:", packet.pts,
           " pre pts:", type == PackType::video ? videoPrePts : audioPrePts,
           " track:", type == PackType::video ? "video" : "audio", " backward:",
           packet.pts < (type == PackType::video ? videoPrePts : audioPrePts));
    bSyncPts = false;
  }
}

bool AVSource::checkDupGop(AvoxPacket& packet) {
  PackType type = (PackType)packet.packtype;
  bool bVideoIFrame = (type == PackType::video && packet.frameType == 1);
  // seek保护期: seek后数据是新内容, 不做重复检测
  // 第一个video I帧到来时重置重复检测状态并结束保护期
  if (bSeeking) {
    if (bVideoIFrame) {
      iFrameHistory.clear();
      iFrameHistory.add(packet.pts, packet.data.size);
      bDiscardDupGop = false;
      bSeeking = false;
    }
    return false;
  }
  // 重复GOP丢弃中: 到下一个新I帧前的所有包都丢弃
  if (bDiscardDupGop) {
    if (bVideoIFrame && !iFrameHistory.has(packet.pts, packet.data.size)) {
      bDiscardDupGop = false;
      iFrameHistory.add(packet.pts, packet.data.size);
      return false;
    }
    return true;
  }
  // 正常状态: 命中历史I帧(PTS,SIZE一致)则判定为重复GOP, 开始丢弃
  if (bVideoIFrame) {
    if (iFrameHistory.has(packet.pts, packet.data.size)) {
      bDiscardDupGop = true;
      LOGFLF(LogLevel::warn,
             "duplicate gop detected, discard. pts:", packet.pts,
             " size:", packet.data.size);
      return true;
    }
    iFrameHistory.add(packet.pts, packet.data.size);
  }
  return false;
}

int64_t AVSource::position() const {
  // 如何是seek中,包计时可能是乱的
  int64_t nowPts = getNowPts();
  return nowPts;
}

int64_t AVSource::getNowPts() const {
  if (videoInfo.tracks.empty() && audioInfo.tracks.empty()) {
    return 0;
  }
  // 优先返回视频的pts
  int64_t pts = videoInfo.getPrePts();
  if (pts == AVOX_NOVALID_PTS) {
    pts = audioInfo.getPrePts();
  }
  if (pts == AVOX_NOVALID_PTS) {
    return 0;
  }
  return pts;
}

int64_t AVSource::getBaseTime(TrackType type) {
  if (type == TrackType::video) {
    return videoInfo.basePts;
  } else if (type == TrackType::audio) {
    return audioInfo.basePts;
  }
  return baseTimeMS;
}

bool AVSource::checkAvSynced() const {
  int64_t videoCurPts = videoInfo.getPrePts();
  int64_t audioCurPts = audioInfo.getPrePts();
  if (videoCurPts == AVOX_NOVALID_PTS || audioCurPts == AVOX_NOVALID_PTS) {
    return true;
  }
  return std::abs(audioCurPts - videoCurPts) <= maxSyncAvTime;
}

}
