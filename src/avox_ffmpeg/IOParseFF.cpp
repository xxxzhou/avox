#include "IOParseFF.hpp"

#include <libavutil/intreadwrite.h>
#include <libavutil/log.h>

#include <algorithm>

#include "avox/Avox.hpp"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

// FFmpeg av_log 级别 -> avox LogLevel
static LogLevel ffToAvoxLevel(int ffLevel) {
  switch (ffLevel) {
    case AV_LOG_QUIET:
      return LogLevel::info;
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
    case AV_LOG_ERROR:
      return LogLevel::error;
    case AV_LOG_WARNING:
      return LogLevel::warn;
    case AV_LOG_INFO:
      return LogLevel::info;
    case AV_LOG_VERBOSE:
    case AV_LOG_DEBUG:
      return LogLevel::debug;
    default:
      return LogLevel::debug;
  }
}

// FFmpeg av_log 回调 -> avox logMsg, 跟 ZLMediaKit onZmLog 同模式
static void onFFLog(void* avcl, int ffLevel, const char* fmt, va_list vl) {
  if (ffLevel <= AV_LOG_QUIET) return;
  // 过滤低于当前 av_log_level 的日志 (av_log 默认只输出 <= warning)
  if (ffLevel > av_log_get_level()) return;
  LogLevel avoxLevel = ffToAvoxLevel(ffLevel);
  // 格式化 FFmpeg 日志消息, 带上模块名(h264/libx265/mpegts等)便于定位来源
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, vl);
  std::string msg;
  const AVClass* avc = avcl ? *(const AVClass* const*)avcl : nullptr;
  if (avc && avc->item_name) {
    string_format(msg, "[FF][", avc->item_name((void*)avcl), "] ", buf);
  } else {
    string_format(msg, "[FF] ", buf);
  }
  logMsg(avoxLevel, msg.c_str());
}

void regFFIO() {
  RegFunc ffIoReg = {"ffmpeg io init", []() {
                       // 桥接 FFmpeg av_log -> avox logMsg
                       av_log_set_callback(onFFLog);
                       av_log_set_level(AV_LOG_WARNING);
                       IoPlanDesc ffDesc = {};
                       ffDesc.name = "ffmpeg format";
                       AvoxManager::Get().ioSources.regInitFunc(
                           IoPlan::ffmpeg, ffDesc,
                           []() -> AVSource* { return new IOParseFF(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(ffIoReg);
}

// 解码中断回调函数
int decode_interrupt_cb(void* ctx) {
  IOParseFF* ioSource = static_cast<IOParseFF*>(ctx);
  if (!ioSource) {
    return 1;
  }
  // seek 窗口内或 close 后打断 IO 线程阻塞的 av_read_frame: seek 让 seekTo 能
  // 独占 fmtCtx, close 让 stopTask 的 join 不干等对端断开
  return ioSource->interruptIo() ? 1 : 0;
}

IOParseFF::IOParseFF() {
  taskName = "ffmpeg io parse";
  // bDisableAudio = true;
}

IOParseFF::~IOParseFF() {
  // 析构不经 close(): 本级先打断再 join, 拖到 ~RunTask 时 fmtCtx 已析构(线程还在 av_read_frame 会 UAF)
  bStopIo = true;
  stopTask();
}

bool IOParseFF::parseStream(int32_t streamId, AVCodecParameters* codecpar) {
  // 如果不是视频和音频，直接返回
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
      codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    return true;
  }
  AVBSFContext* vbsf = nullptr;
  int32_t ret = 0;
  if (!bDisableVideo && codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (codecpar->codec_id == AV_CODEC_ID_H264 ||
        codecpar->codec_id == AV_CODEC_ID_H265) {
      // 得到SPS/PPS/VPS信息
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
      // 保存音频配置信息
      // updateConfig(apack);
    }
  }
  return true;
}

bool IOParseFF::parseH26xConfig(int32_t streamId, const uint8_t* extradata,
                                int32_t size, AVCodecID codecId) {
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t*)extradata, std::min(50, size), true};
  log(LogLevel::info, "ffmpeg io extradata size:", extradataBuf);
  // 直播Annexb,本地avcc 检查是否是AVCC格式
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
    AvoxPacket sps_pps = {};
    sps_pps.data.bRef = true;
    sps_pps.data.data = packet.buff.data();
    sps_pps.data.size = packet.size;
    sps_pps.prefixSize = packet.prefixSize;
    sps_pps.pts = 0;
    sps_pps.dts = 0;
    sps_pps.packtype = (int32_t)PackType::vconfig;
    sps_pps.index = vIndexMaps[streamId];
    dispatch(&IAVSourceOb::onPacket, sps_pps);
    // 保存SPS/PPS/VPS信息
    updateConfig(sps_pps);
  }
  return true;
}

void IOParseFF::parseAACConfig(int32_t streamId, const uint8_t* extradata,
                               int32_t size) {
  // ASC格式信息
  if (size >= 2) {
    // 前两个字节是音频配置信息
    unsigned int config = (extradata[0] << 8) | extradata[1];
    // 1:AAC-Main 2:AAC-SLC 2:AAC-SSR 3:AAC-LTP
    // 解析AAC对象类型
    int profile = (config >> 11) & 0x1F;
    // 解析采样率索引
    int sampling_frequency_index = (config >> 7) & 0x0F;
    static const int sampling_frequencies[] = {96000, 88200, 64000, 48000,
                                               44100, 32000, 24000, 22050,
                                               16000, 11025, 8000,  7350};
    // 解析声道配置
    int channel_configuration = (config >> 3) & 0x0F;
    LOGFLF(LogLevel::info,
           "aac sample rate:", sampling_frequencies[sampling_frequency_index],
           " profile:", profile, " channel:", channel_configuration);
    // ASC格式信息
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
    // 保存音频配置信息
    // updateConfig(asc);
    bAACExtradata = true;
  }
}

void IOParseFF::onRunTask() {
  // 解析IO流媒体格式
  AVFormatContext* temp = avformat_alloc_context();
  temp->interrupt_callback.callback = decode_interrupt_cb;
  temp->interrupt_callback.opaque = this;
  AVDictionary* dict = nullptr;
  LOGFLF(LogLevel::info, "ffmpeg io timeout ms:", timeoutMs,
         " rtsp transport:", rtspTransport);
  // timeoutMs 转为微秒 str
  const std::string timeoutStr = std::to_string(timeoutMs * 1000);
  // 最大延迟 以微秒为单位
  av_dict_set(&dict, "max_delay", timeoutStr.c_str(), 0);
  // 连接超时 以微秒为单位
  av_dict_set(&dict, "stimeout", timeoutStr.c_str(), 0);
  // http/tcp 读超时(微秒): OSS 半开连接(对端静默丢弃不发 RST)时, 不加此项
  // av_read_frame 会无限阻塞, IO 线程卡死; 加了之后超时返回错误走重连路径.
  // stimeout 只对 RTSP 生效, http 不认, 故额外加 timeout
  av_dict_set(&dict, "timeout", timeoutStr.c_str(), 0);
  // RTSP 相关: 使用 TCP 传输(本地文件/HTTP 会忽略)
  av_dict_set(&dict, "rtsp_transport", rtspTransport.c_str(), 0);
  // 重排队列大小
  av_dict_set(&dict, "reorder_queue_size", "2000", 0);
  // 10MB 缓冲区
  av_dict_set(&dict, "buffer_size", "10485760", 0);
  // HTTP/HTTPS 断线自动重连: OSS/CDN 的 keep-alive 被 server 单方面断开时
  // (seek 后命中死连接 -> "partial file" / TLS 握手失败),
  // 自动重建连接而非致命错误. 不开 reconnect_at_eof, 避免真直播流无限重连
  av_dict_set(&dict, "reconnect", "1", 0);
  av_dict_set(&dict, "reconnect_streamed", "1", 0);
  av_dict_set(&dict, "reconnect_delay_max", "5", 0);
  // 关闭 http 连接复用(keep-alive): 半开死连接(对端静默丢弃)若被复用, 重连
  // 后仍走死连接拿不到数据; 关闭后每次读取新建连接, 半开故障自然隔离.
  // fmp4 本就多 range 短连接, 性能影响可忽略
  av_dict_set(&dict, "http_persistent", "0", 0);
  // 强制重复发送 SPS/PPS
  // av_dict_set(&dict, "repeat_headers", "1", 0);
  int ret = avformat_open_input(&temp, url.c_str(), nullptr, &dict);
  av_dict_free(&dict);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_open_input failed")
    avformat_free_context(temp);
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open input failed");
    return;
  }
  fmtCtx = getUniquePtr(temp);
  // 检查是否有全局头,相应的SPS/VPS/ATDS保存在extradata里
  if (fmtCtx->iformat->flags & AVFMT_GLOBALHEADER) {
  }
  if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_find_stream_info failed");
    dispatch(&IAVSourceOb::onError, ffIoError(ret), "open stream failed");
    return;
  }
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 跳过封面图等附加静态图流: 不是可播放的视频轨, 且其包会被误判污染
    // avcc/annexb 判定
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      vdesc.trackId = st->index;
      // vdesc.timeBase = {st->time_base.num, st->time_base.den};
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = av_q2d(st->codecpar->framerate);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      vdesc.desc.colorSpace = ffColorSpace(st->codecpar);
      addVideoDesc(vdesc);
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               !bDisableAudio) {
      ATrackDesc adesc = {};
      adesc.codecId = ffACodec(st->codecpar->codec_id);
      // 如果是不支持的音频格式,自动关闭音频
      if (adesc.codecId == ACodecId::none) {
        LOGFLF(LogLevel::warn,
               "unsupported audio codec:", st->codecpar->codec_id);
        bDisableAudio = true;
        continue;
      }
      adesc.trackId = st->index;
      // adesc.timeBase = {st->time_base.num, st->time_base.den};
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat(st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      addAudioDesc(adesc);
      // 单独给AAC配置头文件使用
      audioDesc = adesc.desc;
    }
  }
  SeekType stype = seekType();
  bSeek = false;
  if (stype != SeekType::none) {
    bSeek = true;
  }
  if (checkLocalPath(url.c_str())) {
    // 本地文件(非流媒体协议),单独区分以便按本地策略处理
    sourceMode = AVSourceMode::local;
  } else if (fmtCtx->duration <= 0) {
    sourceMode = AVSourceMode::live;
  } else {
    sourceMode = AVSourceMode::downLive;
  }
  trackReady();
  bAACExtradata = false;
  // open前可能已设置bFastRead+speed>1，初始化完成后应用非阻塞读取
  if (bFastRead && speed > 1.0) {
    fmtCtx->flags |= AVFMT_FLAG_NONBLOCK;
    LOGFLF(LogLevel::info, "speed:", speed, " set nonblock read on start");
  }
  LOGFLF(LogLevel::info, "io duration:", fmtCtx->duration);
  // 检查是否有音频流
  bool bHavaAudio = audioTracks.size() > 0;
  // 保存解码配置信息
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto& st = fmtCtx->streams[i];
    // 查找流信息，包含SPS/PPS/ATDS等
    if (!parseStream(i, st->codecpar)) {
    }
  }
  // 已经解析配置信息，如SPS,PPS后,开始读取IO数据
  AVPacketPtr bsfPkt = getUniquePtr(av_packet_alloc());
  AVPacketPtr adtsPkt = getUniquePtr(av_packet_alloc());
  while (running()) {
    // 检查暂停
    if (pauseing()) {
      // 已退出 av_read_frame, 通知 seekTo 可以安全操作 fmtCtx
      bIoPausedAck.store(true);
      sleepTask(false, 10);
      continue;
    }
    AVPacketPtr pkt = getUniquePtr(av_packet_alloc());
    if ((ret = av_read_frame(fmtCtx.get(), pkt.get())) < 0) {
      if (ret == AVERROR_EOF) {
        dispatch(&IAVSourceOb::onComplete);
        break;
      }
      // 被 seek 打断(interrupt_callback 返回1)或非阻塞暂无数据: 继续循环,
      // 循环顶会处理暂停. 打断窗口(preSeek→pauseTask 之间约 50ms)内循环顶尚
      // 未暂停, 会在此 EXIT→continue 裸转, 必须阻塞 sleep 避免 100% CPU 空转
      if (ret == AVERROR_EXIT || ret == AVERROR(EAGAIN)) {
        sleepTask(false, 1);
        continue;
      }
      // seek 打断窗口内(preSeek/seekTo 置位 bInterruptRead)的 INVALIDDATA
      // 是打断的副产物: 网络读被截停时 mov demuxer 把它报成 "partial
      // file"(而非干净的 EXIT), 不是真实故障. 直接 continue 回循环顶, 等
      // seekTo pauseTask→pauseing() 持 ack 安全重定位
      if (bInterruptRead.load()) {
        continue;
      }
      // 其余 IO 错误(OSS 截断 range 致 INVALIDDATA / 连接断开等): 上报错误
      AVOX_FFMEPG_LOG(ret, "read frame failed");
      dispatch(&IAVSourceOb::onError, ffIoError(ret), "read frame failed");
      break;
    }
    int32_t streamId = pkt->stream_index;
    auto st = fmtCtx->streams[streamId];
    // 跳过封面图等附加静态图流的数据包, 避免污染 avcc/annexb 判定(见 onRunTask
    // 建流处)
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
    // 忽略其他类型
    if (packType == PackType::other) {
      continue;
    }
    int32_t prefixSize = 0;
    AVPacket* refPkt = pkt.get();
    if (packType == PackType::video) {
      prefixSize = 4;
    } else if (packType == PackType::audio) {
      if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        // 添加adts头，前面可能没有extradata数据,在这添加
        if (bAdtsHeader((uint8_t*)pkt->data, pkt->size) && !bAACExtradata) {
          // 如果是AAC流,并且没有extradata数据,把第一个包的adts头做为配置包
          bAACExtradata = true;
          AvoxPacket adts = {};
          adts.data.bRef = true;
          adts.data.data = (uint8_t*)pkt->data;
          adts.data.size = 7;
          adts.prefixSize = 7;
          adts.packtype = (int32_t)PackType::aconfig;
          adts.index = aIndexMaps[streamId];
          adts.pts = 0;
          adts.dts = 0;
          dispatch(&IAVSourceOb::onPacket, adts);
        }
      }
    }
    // B帧可能没有pts
    if (refPkt->pts == AV_NOPTS_VALUE) {
      refPkt->pts = refPkt->dts;
    }
    // 时间全转成毫秒
    refPkt->pts = av_rescale_q(refPkt->pts, st->time_base, {1, 1000});
    refPkt->dts = av_rescale_q(refPkt->dts, st->time_base, {1, 1000});
    refPkt->duration = av_rescale_q(refPkt->duration, st->time_base, {1, 1000});
    AvoxPacket packet = ffAvoxPacket(refPkt);
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    processPacket(packet);
    sleepTask(true, 1);
  }
}

bool IOParseFF::onOpen() {
  // 实例可能复用重开(先 close 再 open), 清掉上次的停止打断
  bStopIo = false;
  return startTask();
}

void IOParseFF::onClose() {
  // 先置打断再停线程: 阻塞中的 av_read_frame 被 interrupt_callback 截停返回
  // AVERROR_EXIT, 读循环回循环顶见 running()==false 退出, stopTask 立即 join 到
  bStopIo = true;
  stopTask();
}

void IOParseFF::pause(bool bFlag) {
  // 看协议，有些协议支持服务器暂停，这种最好
  if (fmtCtx) {
    int ret = 0;
    // ret == AVERROR(ENOSYS) 表示该协议不支持, 属正常, 忽略即可
    if (bFlag) {
      ret = av_read_pause(fmtCtx.get());
    } else {
      av_read_play(fmtCtx.get());
    }
  }
  if (bFlag) {
    pauseTask();
  } else {
    resumeTask();
  }
}

SeekType IOParseFF::seekType() const {
  if (!fmtCtx || !fmtCtx->pb) {
    return SeekType::none;
  }
  // return fmtCtx->iformat->flags & AVFMT_NOFILE;
  if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_NORMAL) == AVIO_SEEKABLE_NORMAL) {
    return SeekType::normal;
  } else if ((fmtCtx->pb->seekable & AVIO_SEEKABLE_TIME) ==
             AVIO_SEEKABLE_TIME) {
    return SeekType::time;
  }
  return SeekType::none;
}

void IOParseFF::seekTo(double progress) {}

void IOParseFF::preSeek() {
  // 撤背压(pauseIOPacket)同时打断: 读线程失背压后第一次 av_read_frame 即被
  // interrupt 截停返回 AVERROR_EXIT → 循环顶 if(pauseing()) 持 ack 等待 seekTo.
  // 抢在狂奔到 EOF 之前
  bInterruptRead.store(true);
}

bool IOParseFF::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    // preSeek 可能已置位 bInterruptRead: 早退必须清, 否则读线程被钉死在
    // EXIT→continue 空转, 且错误路径的 bInterruptRead 守卫会把真实故障的
    // recover 一起屏蔽 → 播放器挂 buffering
    bInterruptRead.store(false);
    return false;
  }
  // seek保护期: 第一个video I帧到达前不做重复GOP检测, 避免seek后I帧被误判为重复
  bSeeking = true;
  bool bSeek = false;
  // 无条件打断 IO 线程阻塞中的 av_read_frame 再暂停: interrupt_callback 让
  // av_read_frame 返回 AVERROR_EXIT, 读循环 continue 回循环顶 if(pauseing()) 持
  // ack 暂停. 必须打断本地也打断 —— cmdSeek 先 pauseIOPacket 撤了下游背压,
  // 若不打断读线程会狂奔到真 EOF 抢在 seek 前 onComplete → 卡死. 打断返回
  // EXIT(非 EOF), 读线程到不了 EOF 分支, 安全.
  bInterruptRead.store(true);
  pauseTask();
  // 等 IO 线程确认已退出 av_read_frame (最多约200ms), 之后再独占 fmtCtx 做
  // seek, 避免 avformat_seek_file 与 av_read_frame 并发操作同一 fmtCtx 崩溃
  bIoPausedAck.store(false);
  for (int i = 0; i < 10 && !bIoPausedAck.load(); ++i) {
    sleepTask(false, 20);
  }
  // IO 线程已确认不在读, 清除打断标记, 让 avformat_seek_file 自身不被中断
  bInterruptRead.store(false);
  const int64_t target_ts = pos * 1000;
  if (st == SeekType::normal) {
    int ret = avformat_seek_file(fmtCtx.get(), -1, INT64_MIN, target_ts,
                                 INT64_MAX, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
      AVOX_FFMEPG_LOG(ret, "avformat_seek_file failed");
    }
    bSeek = (ret == 0);
  } else if (st == SeekType::time) {
    // 基于字节位置的跳转 (单位：字节)
    if (fmtCtx->pb) {
      int64_t ret =
          avio_seek_time(fmtCtx->pb, -1, target_ts, AVSEEK_FLAG_BACKWARD);
      if (ret < 0) {
        AVOX_FFMEPG_LOG(ret, "avio_seek failed");
      }
      bSeek = (ret == 0);
    }
  }
  // 恢复IO线程
  resumeTask();
  return bSeek;
}

int64_t IOParseFF::duration() const {
  if (!fmtCtx) {
    return 0;
  }
  // FFmpeg 的 duration 单位是微秒（AV_TIME_BASE）
  const int64_t duration_us = fmtCtx->duration;
  if (duration_us == AV_NOPTS_VALUE) {
    // 直播流或无固定时长的情况
    return 0;
  }
  // 转换为毫秒（同时避免溢出）
  return duration_us / (AV_TIME_BASE / 1000);
}

double IOParseFF::progress() const {
  const int64_t dur = duration();
  if (dur <= 0) {
    return 0.0;
  }
  const int64_t pos = position();
  if (pos <= 0) {
    return 0.0;
  }
  // 计算进度并限制在合理范围
  double progress = (double)(pos) / dur;
  return std::max(0.0, std::min(progress, 1.0));
}

void IOParseFF::onSpeed() {
  if (speed != 1.0) {
    // ffmpeg无发送Scale的手段(av_dict无对应选项),倍速仅zlmediakit IO支持
    LOGFLF(LogLevel::warn, "speed:", speed,
           " no support on ffmpeg io, use zlmediakit io");
  }
}

}
