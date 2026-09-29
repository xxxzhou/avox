#include "IOParseDisc.hpp"

#include <algorithm>
#include <cstring>

#include <libbluray/bluray.h>

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// avio 内部缓冲大小: 单次 read 回调请求上限, 256KB 与 bd_read 的 6144 对齐
// 单元倍乘后减少回调次数(mpegts 定位/时长估计的 seek 也落在缓冲内)
constexpr int kAvioBufferSize = 256 * 1024;

IOParseDisc::IOParseDisc() { taskName = "disc io parse"; }

IOParseDisc::~IOParseDisc() { close(); }

void IOParseDisc::applyDiscOptions() {
  if (!option) {
    return;
  }
  // disc.* 私有键: 命名风格与 player 选项一致
  auto has = [&](const char* k) { return option->getType(k) != ArgType::Null; };
  if (has("disc.title")) {
    // 双形态兼容: pvx_player_set_option 走 setString(纯文本), resolve 走 setInt
    const ArgType t = option->getType("disc.title");
    if (t == ArgType::String) {
      discTitle = std::atoi(option->getString("disc.title"));
    } else {
      discTitle = (int32_t)option->getInt("disc.title");
    }
    LOGFLF(LogLevel::info, "[disc io] opt title:", (double)discTitle);
  }
}

void IOParseDisc::closeBd() {
  if (bd) {
    bd_close(bd);
    bd = nullptr;
  }
}

// ---- avio 回调: FFmpeg 读请求直通 libbluray ----
int IOParseDisc::ioReadPacket(void* opaque, uint8_t* buf, int bufSize) {
  IOParseDisc* self = static_cast<IOParseDisc*>(opaque);
  if (!self || !self->bd) {
    return AVERROR_EXIT;
  }
  if (self->interruptIo()) {
    return AVERROR_EXIT;
  }
  int got = bd_read(self->bd, buf, bufSize);
  if (got > 0) {
    self->ioPosition += got;
    return got;
  }
  if (got == 0) {
    LOGFLF(LogLevel::debug, "[disc io] read at EOF pos:",
           (double)self->ioPosition);
    return AVERROR_EOF;
  }
  // 读错误(结构损坏等)
  return AVERROR(EIO);
}

int64_t IOParseDisc::ioSeek(void* opaque, int64_t offset, int whence) {
  IOParseDisc* self = static_cast<IOParseDisc*>(opaque);
  if (!self || !self->bd) {
    return -1;
  }
  const int64_t size = (int64_t)self->titleSize();
  if (whence & AVSEEK_SIZE) {
    return size;
  }
  int64_t target = 0;
  switch (whence) {
    case SEEK_SET:
      target = offset;
      break;
    case SEEK_CUR:
      target = (int64_t)self->ioPosition + offset;
      break;
    case SEEK_END:
      target = size + offset;
      break;
    default:
      return -1;
  }
  if (target < 0) {
    target = 0;
  }
  if (target > size) {
    target = size;
  }
  int64_t got = bd_seek(self->bd, (uint64_t)target);
  self->ioPosition = got >= 0 ? got : target;
  return (int64_t)self->ioPosition;
}

int IOParseDisc::decodeInterruptCb(void* ctx) {
  IOParseDisc* self = static_cast<IOParseDisc*>(ctx);
  if (!self) {
    return 1;
  }
  return self->interruptIo() ? 1 : 0;
}

uint64_t IOParseDisc::titleSize() const {
  return bd ? bd_get_title_size(bd) : 0;
}

bool IOParseDisc::parseStream(int32_t streamId,
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
      // VC-1/WMV3 等: 容器 extradata 原样作为 vconfig 透传
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

void IOParseDisc::parseH26xConfig(int32_t streamId,
                                  const uint8_t* extradata, int32_t size,
                                  AVCodecID codecId) {
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t*)extradata, std::min(50, size), true};
  log(LogLevel::info, "[disc io] extradata size:", extradataBuf);
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

void IOParseDisc::parseAACConfig(int32_t streamId,
                                 const uint8_t* extradata, int32_t size) {
  if (size < 2) {
    return;
  }
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

bool IOParseDisc::onOpen() {
  applyDiscOptions();
  bd = bd_open(url.c_str(), nullptr);
  if (bd == nullptr) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "不是蓝光镜像(BDMV): DVD/数据盘不支持");
    return false;
  }
  const BLURAY_DISC_INFO* di = bd_get_disc_info(bd);
  if (di && ((di->aacs_detected && !di->aacs_handled) ||
             (di->bdplus_detected && !di->bdplus_handled))) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "加密蓝光(AACS/BD+)不支持, 仅支持未加密原盘");
    closeBd();
    return false;
  }
  // 选标题: disc.title<0 = 自动最长; 否则用业务传入的 bd 标题索引
  uint32_t count = bd_get_titles(bd, 0, 0);
  uint32_t sel = 0;
  if (discTitle >= 0 && (uint32_t)discTitle < count) {
    sel = (uint32_t)discTitle;
  } else {
    uint64_t best = 0;
    for (uint32_t i = 0; i < count; ++i) {
      BLURAY_TITLE_INFO* ti = bd_get_title_info(bd, i, 0);
      if (ti) {
        if (ti->duration > best) {
          best = ti->duration;
          sel = i;
        }
        bd_free_title_info(ti);
      }
    }
  }
  if (bd_select_title(bd, sel) <= 0) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "标题选择失败(镜像结构异常)");
    closeBd();
    return false;
  }
  LOGFLF(LogLevel::info, "[disc io] title:", (double)sel, " size(MB):",
         (double)(bd_get_title_size(bd) / 1024 / 1024));
  ioBuffer = (unsigned char*)av_malloc(kAvioBufferSize);
  ioCtx = avio_alloc_context(ioBuffer, kAvioBufferSize, 0, this, ioReadPacket,
                             nullptr, ioSeek);
  ioCtx->seekable = AVIO_SEEKABLE_NORMAL;
  ioPosition = 0;
  return startTask();
}

void IOParseDisc::onRunTask() {
  AVFormatContext* temp = avformat_alloc_context();
  temp->pb = ioCtx;
  temp->flags |= AVFMT_FLAG_CUSTOM_IO;
  temp->interrupt_callback.callback = decodeInterruptCb;
  temp->interrupt_callback.opaque = this;
  LOGFLF(LogLevel::info, "[disc io] opening", url.c_str());
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
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      if (vdesc.codecId == VCodecId::none) {
        LOGFLF(LogLevel::warn, "[disc io] unsupported video codec:",
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
        LOGFLF(LogLevel::warn, "[disc io] unsupported audio codec:",
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
  if (getVideoTracks().empty() && getAudioTracks().empty()) {
    LOGFLF(LogLevel::error, "[disc io] no playable codec track, url:", url);
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
  // 原盘有总时长/进度可视/可seek: downLive
  sourceMode = fmtCtx->duration > 0 ? AVSourceMode::downLive : AVSourceMode::live;
  trackReady();
  LOGFLF(LogLevel::info, "[disc io] duration(ms):",
         (double)(fmtCtx->duration == AV_NOPTS_VALUE
                      ? 0
                      : fmtCtx->duration / (AV_TIME_BASE / 1000)));
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    parseStream(i, st->codecpar);
  }
  while (running()) {
    if (pauseing()) {
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
      if (ret == AVERROR_EXIT || ret == AVERROR(EAGAIN)) {
        sleepTask(false, 1);
        continue;
      }
      if (bInterruptRead.load()) {
        continue;
      }
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
    if (pkt->pts == AV_NOPTS_VALUE) {
      pkt->pts = pkt->dts;
    }
    int64_t ptsMs = av_rescale_q(pkt->pts, st->time_base, {1, 1000});
    int64_t dtsMs = av_rescale_q(pkt->dts, st->time_base, {1, 1000});
    int64_t durMs = av_rescale_q(pkt->duration, st->time_base, {1, 1000});
    // 首包 dts 定基(B帧链 pts≥dts, 用 dts 免负值), 全链转相对时间
    if (ptsBaseMs < 0) {
      ptsBaseMs = dtsMs;
      LOGFLF(LogLevel::info, "[disc io] pts base(ms):", (double)ptsBaseMs);
    }
    pkt->pts = ptsMs - ptsBaseMs;
    pkt->dts = dtsMs - ptsBaseMs;
    pkt->duration = durMs;
    AvoxPacket packet = ffAvoxPacket(pkt.get());
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    processPacket(packet);
    sleepTask(true, 1);
  }
}

void IOParseDisc::releaseIoContext() {
  if (ioCtx) {
    av_freep(&ioCtx->buffer);
    av_freep(&ioCtx);
  } else if (ioBuffer) {
    av_freep(&ioBuffer);
  }
  ioBuffer = nullptr;
  ioCtx = nullptr;
}

void IOParseDisc::onClose() {
  bInterruptRead.store(true);
  stopTask();
  fmtCtx.reset();
  releaseIoContext();
  closeBd();
  bInterruptRead.store(false);
}

void IOParseDisc::preSeek() { bInterruptRead.store(true); }

void IOParseDisc::pause(bool bFlag) {
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseDisc::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  }
  return SeekType::none;
}

void IOParseDisc::seekTo(double progress) {}

bool IOParseDisc::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    bInterruptRead.store(false);
    return false;
  }
  bSeeking = true;
  bool bOk = false;
  bInterruptRead.store(true);
  pauseTask();
  bIoPausedAck.store(false);
  for (int i = 0; i < 10 && !bIoPausedAck.load(); ++i) {
    sleepTask(false, 20);
  }
  bInterruptRead.store(false);
  // 引擎传相对 ms, mpegts 域是绝对时间戳: 加回 BD 基偏再交给解容器
  const int64_t base = ptsBaseMs > 0 ? ptsBaseMs : 0;
  const int64_t targetTs = (pos + base) * 1000;
  int ret = avformat_seek_file(fmtCtx.get(), -1, INT64_MIN, targetTs,
                               INT64_MAX, AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_seek_file failed")
  }
  bOk = (ret == 0);
  resumeTask();
  return bOk;
}

int64_t IOParseDisc::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  const int64_t durUs = fmtCtx->duration;
  if (durUs == AV_NOPTS_VALUE) {
    return 0;
  }
  return durUs / (AV_TIME_BASE / 1000);
}

double IOParseDisc::progress() const {
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

void IOParseDisc::onOptionChange(const char* key, ArgType type) {
  if (!option) {
    return;
  }
  if (std::strncmp(key, "disc.", 5) == 0) {
    applyDiscOptions();
    return;
  }
  AVSource::onOptionChange(key, type);
}

}
