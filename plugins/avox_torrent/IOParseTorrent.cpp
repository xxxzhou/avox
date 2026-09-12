#include "IOParseTorrent.hpp"

#include <algorithm>
#include <cstring>

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// avio内部缓冲大小(单次read回调的请求上限): 256KB减少回调与piece等待边界
// 次数, 容器moov解析的小读+seek也基本落在缓冲内不走引擎
constexpr int kAvioBufferSize = 256 * 1024;

IOParseTorrent::IOParseTorrent() { taskName = "torrent io parse"; }

IOParseTorrent::~IOParseTorrent() { close(); }

void IOParseTorrent::applyTorrentOptions() {
  if (!option) {
    return;
  }
  // torrent.* 私有键: 命名风格与player选项一致, 缺省值取Config默认成员
  auto has = [&](const char* k) {
    return option->getType(k) != ArgType::Null;
  };
  if (has("torrent.fileIndex")) {
    engineCfg.fileIndex = (int32_t)option->getInt("torrent.fileIndex");
    LOGFLF(LogLevel::info, "[torrent io] opt fileIndex:", engineCfg.fileIndex);
  }
  if (has("torrent.cacheDir")) {
    engineCfg.cacheDir = option->getString("torrent.cacheDir");
  }
  if (has("torrent.extraTrackers")) {
    engineCfg.extraTrackers = option->getString("torrent.extraTrackers");
  }
  if (has("torrent.metaTimeoutMs")) {
    engineCfg.metaTimeoutMs = (int32_t)option->getInt("torrent.metaTimeoutMs");
  }
  if (has("torrent.lookaheadMB")) {
    engineCfg.lookaheadMB = (int32_t)option->getInt("torrent.lookaheadMB");
  }
  if (has("torrent.pieceTimeoutMs")) {
    engineCfg.pieceTimeoutMs =
        (int32_t)option->getInt("torrent.pieceTimeoutMs");
  }
  if (has("torrent.maxDownloadSpeedKB")) {
    engineCfg.maxDownloadSpeedKB = option->getInt("torrent.maxDownloadSpeedKB");
  }
  if (has("torrent.cacheMaxGB")) {
    engineCfg.cacheMaxGB = option->getInt("torrent.cacheMaxGB");
  }
  if (has("torrent.deleteOnClose")) {
    engineCfg.deleteOnClose = option->getBool("torrent.deleteOnClose");
  }
}

// ---- avio回调: FFmpeg读请求直通TorrentEngine ----
int IOParseTorrent::ioReadPacket(void* opaque, uint8_t* buf, int bufSize) {
  IOParseTorrent* self = static_cast<IOParseTorrent*>(opaque);
  if (!self || !self->engine || !self->engine->running()) {
    return AVERROR_EXIT;
  }
  const TorrentEngine::FileInfo& info = self->engine->getFileInfo();
  if ((uint64_t)self->ioPosition >= info.size) {
    LOGFLF(LogLevel::debug, "[torrent io] read at EOF pos:",
           (double)self->ioPosition);
    return AVERROR_EOF;
  }
  int32_t got = self->engine->readAt((uint64_t)self->ioPosition, buf, bufSize);
  static thread_local int32_t logCount = 0;
  if (logCount < 12) {
    ++logCount;
    LOGFLF(LogLevel::debug, "[torrent io] read off:", (double)self->ioPosition,
           " want:", bufSize, " got:", got);
  }
  if (got < 0) {
    // 引擎超时(数据长期不可用): 映射成IO错误走播放器netTimeout路径
    return AVERROR(EIO);
  }
  if (got == 0) {
    // 被seek/close打断
    return AVERROR_EXIT;
  }
  self->ioPosition += got;
  return got;
}

int64_t IOParseTorrent::ioSeek(void* opaque, int64_t offset, int whence) {
  IOParseTorrent* self = static_cast<IOParseTorrent*>(opaque);
  if (!self || !self->engine) {
    return -1;
  }
  const uint64_t size = self->engine->getFileInfo().size;
  // 尺寸查询: 不移动游标
  if (whence & AVSEEK_SIZE) {
    return (int64_t)size;
  }
  int64_t target = 0;
  switch (whence) {
    case SEEK_SET:
      target = offset;
      break;
    case SEEK_CUR:
      target = self->ioPosition + offset;
      break;
    case SEEK_END:
      target = (int64_t)size + offset;
      break;
    default:
      return -1;
  }
  if (target < 0) {
    target = 0;
  }
  if ((uint64_t)target > size) {
    target = (int64_t)size;
  }
  LOGFLF(LogLevel::debug, "[torrent io] seek ->", (double)(target / 1024),
         "KB");
  self->ioPosition = target;
  return target;
}

int IOParseTorrent::decodeInterruptCb(void* ctx) {
  IOParseTorrent* self = static_cast<IOParseTorrent*>(ctx);
  if (!self) {
    return 1;
  }
  // seek窗口内打断in-flight读(av_read_frame阻塞中)
  return self->interruptIo() ? 1 : 0;
}

bool IOParseTorrent::parseStream(int32_t streamId,
                                 AVCodecParameters* codecpar) {
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
      codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    return true;
  }
  if (!bDisableVideo && codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (codecpar->codec_id == AV_CODEC_ID_H264 ||
        codecpar->codec_id == AV_CODEC_ID_H265) {
      parseH26xConfig(streamId, codecpar->extradata, codecpar->extradata_size,
                      codecpar->codec_id);
    } else if (codecpar->extradata_size > 0) {
      // VC-1/WMV3/RV30/RV40等: 无nalu结构, 容器extradata原样作为vconfig
      // 透传(解码器初始化必需, FFVDecoder 侧按原样组装)
      AvoxPacket vpack = {};
      vpack.data.bRef = true;
      vpack.data.data = codecpar->extradata;
      vpack.data.size = codecpar->extradata_size;
      vpack.prefixSize = 0;
      vpack.packtype = (int32_t)PackType::vconfig;
      vpack.index = vIndexMaps[streamId];
      vpack.pts = 0;
      vpack.dts = 0;
      dispatch(&IAVSourceOb::onPacket, vpack);
      updateConfig(vpack);
    }
  }
  if (!bDisableAudio && codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
      codecpar->extradata_size > 0) {
    if (codecpar->codec_id == AV_CODEC_ID_AAC) {
      parseAACConfig(streamId, codecpar->extradata, codecpar->extradata_size);
      bAACExtradata = true;
    } else {
      AvoxPacket apack = {};
      apack.data.bRef = true;
      apack.data.data = codecpar->extradata;
      apack.data.size = codecpar->extradata_size;
      apack.prefixSize = 0;
      apack.packtype = (int32_t)PackType::aconfig;
      apack.index = aIndexMaps[streamId];
      apack.pts = 0;
      apack.dts = 0;
      dispatch(&IAVSourceOb::onPacket, apack);
    }
  }
  return true;
}

void IOParseTorrent::parseH26xConfig(int32_t streamId,
                                     const uint8_t* extradata, int32_t size,
                                     AVCodecID codecId) {
  // 与IOParseFF同构: annexb直播格式 / avcc-hvcc本地格式
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t*)extradata, std::min(50, size), true};
  log(LogLevel::info, "[torrent io] extradata size:", extradataBuf);
  if (checkAnnexbHeader(extradata, size) > 0) {
    if (codecId == AV_CODEC_ID_H264) {
      h264SplitNalu(extradata, size, packets);
    } else {
      h265SplitNalu(extradata, size, packets);
    }
    bvcc = false;
  } else {
    if (codecId == AV_CODEC_ID_H264) {
      h264SplitAvcc(extradata, size, packets);
    } else {
      h265SplitHvcc(extradata, size, packets);
    }
    bvcc = true;
  }
  for (auto& packet : packets) {
    AvoxPacket spsPps = {};
    spsPps.data.bRef = true;
    spsPps.data.data = packet.buff.data();
    spsPps.data.size = packet.size;
    spsPps.prefixSize = packet.prefixSize;
    spsPps.pts = 0;
    spsPps.dts = 0;
    spsPps.packtype = (int32_t)PackType::vconfig;
    spsPps.index = vIndexMaps[streamId];
    dispatch(&IAVSourceOb::onPacket, spsPps);
    updateConfig(spsPps);
  }
}

void IOParseTorrent::parseAACConfig(int32_t streamId,
                                    const uint8_t* extradata, int32_t size) {
  if (size < 2) {
    return;
  }
  unsigned int cfg = (extradata[0] << 8) | extradata[1];
  int profile = (cfg >> 11) & 0x1F;
  int sampleIdx = (cfg >> 7) & 0x0F;
  static const int freqs[] = {96000, 88200, 64000, 48000, 44100, 32000,
                              24000, 22050, 16000, 11025, 8000, 7350};
  int channels = (cfg >> 3) & 0x0F;
  LOGFLF(LogLevel::info, "[torrent io] aac rate:", freqs[sampleIdx],
         " profile:", profile, " channel:", channels);
  AvoxPacket asc = {};
  asc.data.bRef = true;
  asc.data.data = (uint8_t*)extradata;
  asc.data.size = size;
  asc.prefixSize = 0;
  asc.packtype = (int32_t)PackType::aconfig;
  asc.index = aIndexMaps[streamId];
  asc.pts = 0;
  asc.dts = 0;
  dispatch(&IAVSourceOb::onPacket, asc);
  bAACExtradata = true;
}

bool IOParseTorrent::onOpen() {
  applyTorrentOptions();
  engine = std::make_unique<TorrentEngine>();
  engine->setAbortFlag(&bInterruptRead);
  std::string err;
  if (!engine->start(url, engineCfg, &err)) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport, err.c_str());
    engine.reset();
    return false;
  }
  // 自定义avio上下文: read/seek回调对接引擎, seekable=NORMAL使
  // mov/mkv解容器按可seek文件处理(duration索引可用)
  ioBuffer = (unsigned char*)av_malloc(kAvioBufferSize);
  ioCtx = avio_alloc_context(ioBuffer, kAvioBufferSize, 0, this, ioReadPacket,
                             nullptr, ioSeek);
  ioCtx->seekable = AVIO_SEEKABLE_NORMAL;
  ioPosition = 0;
  return startTask();
}

void IOParseTorrent::onRunTask() {
  // 打开流: CUSTOM_IO由本类供pb, avformat只做探测+解封装
  AVFormatContext* temp = avformat_alloc_context();
  temp->pb = ioCtx;
  temp->flags |= AVFMT_FLAG_CUSTOM_IO;
  temp->interrupt_callback.callback = decodeInterruptCb;
  temp->interrupt_callback.opaque = this;
  LOGFLF(LogLevel::info, "[torrent io] opening",
         engine ? engine->getFileInfo().path.c_str() : url.c_str(), " size:",
         (double)(engine ? (double)(engine->getFileInfo().size / 1024 / 1024)
                         : 0.0),
         "MB");
  int ret = avformat_open_input(&temp, "", nullptr, nullptr);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_open_input failed")
    fmtCtx.reset();
    releaseIoContext();
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open input failed");
    return;
  }
  fmtCtx = getUniquePtr(temp);
  if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_find_stream_info failed")
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open stream failed");
    return;
  }
  // 流描述(对齐IOParseFF)
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 跳过封面图等附加静态图流
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      if (vdesc.codecId == VCodecId::none) {
        LOGFLF(LogLevel::warn, "[torrent io] unsupported video codec:",
               st->codecpar->codec_id);
        continue;
      }
      vdesc.trackId = st->index;
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = ffFps(st);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      addVideoDesc(vdesc);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               !bDisableAudio) {
      ATrackDesc adesc = {};
      adesc.codecId = ffACodec(st->codecpar->codec_id);
      if (adesc.codecId == ACodecId::none) {
        LOGFLF(LogLevel::warn, "[torrent io] unsupported audio codec:",
               st->codecpar->codec_id);
        bDisableAudio = true;
        continue;
      }
      adesc.trackId = st->index;
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat((int32_t)st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      adesc.desc.blockAlign = st->codecpar->block_align;
      addAudioDesc(adesc);
      audioDesc = adesc.desc;
    }
  }
  // 无任何可播轨道(如AV1/Opus等SDK未覆盖编码): 快速报错而非让上层无限等
  if (getVideoTracks().empty() && getAudioTracks().empty()) {
    LOGFLF(LogLevel::error, "[torrent io] no playable codec track, url:", url);
    releaseIoContext();
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "no playable codec track(编码不支持)");
    return;
  }
  SeekType stype = seekType();
  bSeek = false;
  if (stype != SeekType::none) {
    bSeek = true;
  }
  // 种子视频本质是渐进下载(downLive): 有总时长、进度可视、可seek
  sourceMode = fmtCtx->duration > 0 ? AVSourceMode::downLive : AVSourceMode::live;
  trackReady();
  LOGFLF(LogLevel::info, "[torrent io] duration(ms):",
         (double)(fmtCtx->duration == AV_NOPTS_VALUE
                      ? 0
                      : fmtCtx->duration / (AV_TIME_BASE / 1000)));
  // 派发各流配置包(SPS/PPS/VPS/ASC)
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    parseStream(i, st->codecpar);
  }
  // 读循环: 与IOParseFF逐行同构(暂停确认/interrupt打断/EOF/ADTS提升)
  while (running()) {
    if (pauseing()) {
      // 已退出av_read_frame, 通知seekTo可以安全操作fmtCtx
      bIoPausedAck.store(true);
      sleepTask(false, 10);
      continue;
    }
    AVPacketPtr pkt = getUniquePtr(av_packet_alloc());
    ret = av_read_frame(fmtCtx.get(), pkt.get());
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        dispatch(&IAVSourceOb::onComplete);
        break;
      }
      // interrupt打断或非阻塞暂无数据: 回循环顶等暂停处理
      if (ret == AVERROR_EXIT || ret == AVERROR(EAGAIN)) {
        sleepTask(false, 1);
        continue;
      }
      // seek打断窗口内的INVALIDDATA是截停副产物(mov报partial file), 不算故障
      if (bInterruptRead.load()) {
        continue;
      }
      // 真实IO错误(引擎读片超时等)
      AVOX_FFMEPG_LOG(ret, "read frame failed")
      dispatch(&IAVSourceOb::onError, ffIoError(ret), "read frame failed");
      break;
    }
    int32_t streamId = pkt->stream_index;
    auto& st = fmtCtx->streams[streamId];
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    PackType packType = PackType::other;
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      packType = PackType::video;
      if (bDisableVideo) {
        continue;
      }
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      packType = PackType::audio;
      if (bDisableAudio) {
        continue;
      }
    }
    if (packType == PackType::other) {
      continue;
    }
    int32_t prefixSize = 0;
    if (packType == PackType::video) {
      prefixSize = 4;
    } else if (st->codecpar->codec_id == AV_CODEC_ID_AAC &&
               bAdtsHeader(pkt->data, pkt->size) && !bAACExtradata) {
      // AAC裸流且无extradata时把首个ADTS头提升为配置包
      bAACExtradata = true;
      AvoxPacket adts = {};
      adts.data.bRef = true;
      adts.data.data = pkt->data;
      adts.data.size = 7;
      adts.prefixSize = 7;
      adts.packtype = (int32_t)PackType::aconfig;
      adts.index = aIndexMaps[streamId];
      adts.pts = 0;
      adts.dts = 0;
      dispatch(&IAVSourceOb::onPacket, adts);
    }
    // B帧可能没有pts, 用dts补齐后统一转毫秒
    if (pkt->pts == AV_NOPTS_VALUE) {
      pkt->pts = pkt->dts;
    }
    pkt->pts = av_rescale_q(pkt->pts, st->time_base, {1, 1000});
    pkt->dts = av_rescale_q(pkt->dts, st->time_base, {1, 1000});
    pkt->duration = av_rescale_q(pkt->duration, st->time_base, {1, 1000});
    AvoxPacket packet = ffAvoxPacket(pkt.get());
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    processPacket(packet);
    sleepTask(true, 1);
  }
}

void IOParseTorrent::releaseIoContext() {
  // 手动构建的pb自行释放(avio内部缓冲可能已realloc替换, 取当前指针释放)
  if (ioCtx) {
    av_freep(&ioCtx->buffer);
    av_freep(&ioCtx);
  } else if (ioBuffer) {
    av_freep(&ioBuffer);
  }
  ioBuffer = nullptr;
  ioCtx = nullptr;
}

void IOParseTorrent::onClose() {
  // 先置打断再停线程: IO线程可能阻塞在引擎读片等待里,
  // 靠bInterruptRead快速退出, stopTask的join才能及时返回
  bInterruptRead.store(true);
  stopTask();
  fmtCtx.reset();
  releaseIoContext();
  if (engine) {
    engine->shutdown();
    engine.reset();
  }
  bInterruptRead.store(false);
}

void IOParseTorrent::preSeek() { bInterruptRead.store(true); }

void IOParseTorrent::pause(bool bFlag) {
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseTorrent::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  }
  return SeekType::none;
}

void IOParseTorrent::seekTo(double progress) {}

bool IOParseTorrent::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    // 早退必须清标记, 否则读线程被钉死在EXIT空转(见IOParseFF同名注释)
    bInterruptRead.store(false);
    return false;
  }
  bSeeking = true;
  bool bOk = false;
  // 无条件打断IO线程的av_read_frame并等待确认, 再独占fmtCtx执行seek
  bInterruptRead.store(true);
  pauseTask();
  bIoPausedAck.store(false);
  for (int i = 0; i < 10 && !bIoPausedAck.load(); ++i) {
    sleepTask(false, 20);
  }
  bInterruptRead.store(false);
  const int64_t targetTs = pos * 1000;
  int ret = avformat_seek_file(fmtCtx.get(), -1, INT64_MIN, targetTs,
                               INT64_MAX, AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_seek_file failed")
  }
  bOk = (ret == 0);
  resumeTask();
  return bOk;
}

int64_t IOParseTorrent::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  const int64_t durUs = fmtCtx->duration;
  if (durUs == AV_NOPTS_VALUE) {
    return 0;
  }
  return durUs / (AV_TIME_BASE / 1000);
}

double IOParseTorrent::progress() const {
  const int64_t dur = duration();
  if (dur <= 0) {
    return 0.0;
  }
  const int64_t pos = position();
  if (pos <= 0) {
    return 0.0;
  }
  double ratio = (double)pos / (double)dur;
  return std::max(0.0, std::min(ratio, 1.0));
}

void IOParseTorrent::onOptionChange(const char* key, ArgType type) {
  if (!option) {
    return;
  }
  // torrent.*私有键仅在本源打开前快照生效, 其余键交给基类(timeout等)
  if (std::strncmp(key, "torrent.", 8) == 0) {
    applyTorrentOptions();
    return;
  }
  AVSource::onOptionChange(key, type);
}

}
