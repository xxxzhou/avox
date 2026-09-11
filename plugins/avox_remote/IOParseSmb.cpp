#include "IOParseSmb.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// avio内部缓冲大小(单次read回调的请求上限): 256KB摊薄SMB往返,
// 容器moov解析的小读+seek也基本落在缓冲内
constexpr int kAvioBufferSize = 256 * 1024;
// SMB 默认端口(非默认时 URL 显式带 :port)
constexpr int kSmbDefaultPort = 445;

IOParseSmb::IOParseSmb() { taskName = "smb io parse"; }

IOParseSmb::~IOParseSmb() { close(); }

bool IOParseSmb::parseUrl(const std::string& url, UrlParts* out) {
  const std::string kScheme = "smb://";
  if (url.rfind(kScheme, 0) != 0) {
    return false;
  }
  size_t rest = kScheme.size();
  size_t pathStart = url.find('/', rest);
  std::string authority =
      url.substr(rest, pathStart == std::string::npos ? std::string::npos
                                                      : pathStart - rest);
  std::string path = pathStart == std::string::npos ? "" : url.substr(pathStart + 1);
  // userinfo: user[:pass](resolve 产出时已嵌入)
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    std::string ui = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = ui.find(':');
    if (colon == std::string::npos) {
      out->user = ui;
    } else {
      out->user = ui.substr(0, colon);
      out->pass = ui.substr(colon + 1);
    }
  }
  // host[:port]
  if (!authority.empty() && authority[0] == '[') {
    size_t rb = authority.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, rb - 1);
    if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
      out->port = std::atoi(authority.c_str() + rb + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
      out->host = authority.substr(0, colon);
      out->port = std::atoi(authority.c_str() + colon + 1);
    } else {
      out->host = authority;
    }
  }
  if (out->host.empty()) {
    return false;
  }
  if (out->port == 0) {
    out->port = kSmbDefaultPort;
  }
  // /share/path: 两者都必填(文件级播放)
  if (path.empty()) {
    return false;
  }
  if (path.back() == '/') {
    path.pop_back();
  }
  size_t slash = path.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= path.size()) {
    return false;
  }
  out->share = path.substr(0, slash);
  out->path = path.substr(slash + 1);
  return !out->share.empty() && !out->path.empty();
}

// ---- avio回调: FFmpeg读请求直通libsmb2 ----
int IOParseSmb::ioReadPacket(void* opaque, uint8_t* buf, int bufSize) {
  IOParseSmb* self = static_cast<IOParseSmb*>(opaque);
  if (!self || !self->smb || !self->fh) {
    return AVERROR_EXIT;
  }
  if (self->bInterruptRead.load()) {
    // seek/close打断窗口: 立即让出(av_read_frame收到EXIT回循环顶处理)
    return AVERROR_EXIT;
  }
  if ((uint64_t)self->ioPosition >= self->fileSize) {
    return AVERROR_EOF;
  }
  int32_t got = (int32_t)smb2_pread(self->smb, self->fh, buf,
                                    (uint32_t)bufSize,
                                    (uint64_t)self->ioPosition);
  if (got < 0) {
    // 网络错/超时: 映射成IO错误走播放器netTimeout路径
    return AVERROR(EIO);
  }
  self->ioPosition += got;
  return got;
}

int64_t IOParseSmb::ioSeek(void* opaque, int64_t offset, int whence) {
  IOParseSmb* self = static_cast<IOParseSmb*>(opaque);
  if (!self || !self->smb) {
    return -1;
  }
  const uint64_t size = self->fileSize;
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
  self->ioPosition = target;
  return target;
}

int IOParseSmb::decodeInterruptCb(void* ctx) {
  IOParseSmb* self = static_cast<IOParseSmb*>(ctx);
  if (!self) {
    return 1;
  }
  // seek窗口内打断in-flight读(av_read_frame阻塞中)
  return self->interruptIo() ? 1 : 0;
}

bool IOParseSmb::parseStream(int32_t streamId,
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

void IOParseSmb::parseH26xConfig(int32_t streamId,
                                 const uint8_t* extradata, int32_t size,
                                 AVCodecID codecId) {
  // 与IOParseFF同构: annexb直播格式 / avcc-hvcc本地格式
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t*)extradata, std::min(50, size), true};
  log(LogLevel::info, "[smb io] extradata size:", extradataBuf);
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
    spsPps.pts = 0;
    spsPps.dts = 0;
    dispatch(&IAVSourceOb::onPacket, spsPps);
    updateConfig(spsPps);
  }
}

void IOParseSmb::parseAACConfig(int32_t streamId,
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
  LOGFLF(LogLevel::info, "[smb io] aac rate:", freqs[sampleIdx],
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

bool IOParseSmb::onOpen() {
  UrlParts p;
  if (!parseUrl(url, &p)) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "unsupported url(expect smb://[user:pass@]host[:port]/share/path)");
    return false;
  }
  smb = smb2_init_context();
  if (!smb) {
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport,
             "smb2_init_context failed");
    return false;
  }
  smb2_set_timeout(smb, std::max<int32_t>(1, (timeoutMs + 999) / 1000));
  if (!p.user.empty()) {
    smb2_set_user(smb, p.user.c_str());
    smb2_set_password(smb, p.pass.c_str());
  }
  if (smb2_connect_share(smb, p.host.c_str(), p.share.c_str(),
                         p.user.empty() ? nullptr : p.user.c_str()) != 0) {
    std::string err = smb2_get_error(smb);
    LOGFLF(LogLevel::error, "[smb io] connect share failed:", err.c_str());
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport, err.c_str());
    smb2_destroy_context(smb);
    smb = nullptr;
    return false;
  }
  fh = smb2_open(smb, p.path.c_str(), O_RDONLY);
  if (!fh) {
    std::string err = smb2_get_error(smb);
    LOGFLF(LogLevel::error, "[smb io] open file failed:", p.path.c_str(),
           " ", err.c_str());
    dispatch(&IAVSourceOb::onError, AVError::urlNoSupport, err.c_str());
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);
    smb = nullptr;
    return false;
  }
  struct smb2_stat_64 st = {};
  if (smb2_fstat(smb, fh, &st) != 0) {
    dispatch(&IAVSourceOb::onError, AVError::other, "fstat failed");
    smb2_close(smb, fh);
    fh = nullptr;
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);
    smb = nullptr;
    return false;
  }
  fileSize = (uint64_t)st.smb2_size;
  ioPosition = 0;
  // 自定义avio上下文: read/seek回调对接libsmb2, seekable=NORMAL使
  // mov/mkv解容器按可seek文件处理(duration索引可用)
  ioBuffer = (unsigned char*)av_malloc(kAvioBufferSize);
  ioCtx = avio_alloc_context(ioBuffer, kAvioBufferSize, 0, this, ioReadPacket,
                             nullptr, ioSeek);
  ioCtx->seekable = AVIO_SEEKABLE_NORMAL;
  return startTask();
}

void IOParseSmb::onRunTask() {
  // 打开流: CUSTOM_IO由本类供pb, avformat只做探测+解封装
  AVFormatContext* temp = avformat_alloc_context();
  temp->pb = ioCtx;
  temp->flags |= AVFMT_FLAG_CUSTOM_IO;
  temp->interrupt_callback.callback = decodeInterruptCb;
  temp->interrupt_callback.opaque = this;
  LOGFLF(LogLevel::info, "[smb io] opening size:",
         (double)(fileSize / 1024 / 1024), "MB");
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
        LOGFLF(LogLevel::warn, "[smb io] unsupported video codec:",
               st->codecpar->codec_id);
        continue;
      }
      vdesc.trackId = st->index;
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = av_q2d(st->codecpar->framerate);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      addVideoDesc(vdesc);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               !bDisableAudio) {
      ATrackDesc adesc = {};
      adesc.codecId = ffACodec(st->codecpar->codec_id);
      if (adesc.codecId == ACodecId::none) {
        LOGFLF(LogLevel::warn, "[smb io] unsupported audio codec:",
               st->codecpar->codec_id);
        bDisableAudio = true;
        continue;
      }
      adesc.trackId = st->index;
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat((int32_t)st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      addAudioDesc(adesc);
      audioDesc = adesc.desc;
    }
  }
  // 无任何可播轨道(如AV1/Opus等SDK未覆盖编码): 快速报错而非让上层无限等
  if (getVideoTracks().empty() && getAudioTracks().empty()) {
    LOGFLF(LogLevel::error, "[smb io] no playable codec track, url:", url);
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
  // SMB文件是完整可seek文件: 有总时长、进度可视
  sourceMode = fmtCtx->duration > 0 ? AVSourceMode::local : AVSourceMode::live;
  trackReady();
  LOGFLF(LogLevel::info, "[smb io] duration(ms):",
         (double)(fmtCtx->duration == AV_NOPTS_VALUE
                      ? 0
                      : fmtCtx->duration / (AV_TIME_BASE / 1000)));
  // 派发各流配置包(SPS/PPS/VPS/ASC)
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    parseStream(i, st->codecpar);
  }
  // 读循环: 与IOParseFF/IOParseTorrent逐行同构(暂停确认/interrupt打断/EOF/ADTS提升)
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
      // seek打断窗口内的INVALIDDATA是截停副产物, 不算故障
      if (bInterruptRead.load()) {
        continue;
      }
      // 真实IO错误(网络断开等)
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

void IOParseSmb::releaseIoContext() {
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

void IOParseSmb::onClose() {
  // 先置打断再停线程: IO线程可能阻塞在网络读等待里,
  // 靠bInterruptRead快速退出, stopTask的join才能及时返回
  bInterruptRead.store(true);
  stopTask();
  fmtCtx.reset();
  releaseIoContext();
  if (fh) {
    smb2_close(smb, fh);
    fh = nullptr;
  }
  if (smb) {
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);
    smb = nullptr;
  }
  bInterruptRead.store(false);
}

void IOParseSmb::preSeek() { bInterruptRead.store(true); }

void IOParseSmb::pause(bool bFlag) {
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseSmb::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  }
  return SeekType::none;
}

void IOParseSmb::seekTo(double progress) {}

bool IOParseSmb::seekTo(int64_t pos) {
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

int64_t IOParseSmb::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  const int64_t durUs = fmtCtx->duration;
  if (durUs == AV_NOPTS_VALUE) {
    return 0;
  }
  return durUs / (AV_TIME_BASE / 1000);
}

double IOParseSmb::progress() const {
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

void IOParseSmb::onOptionChange(const char* key, ArgType type) {
  // smb 无私有键, 全部交给基类(timeout等)
  AVSource::onOptionChange(key, type);
}

}
