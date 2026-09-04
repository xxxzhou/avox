# 播放器FFmpeg

当初设计这个播放器时，是不需要FFmpeg也能正常使用，那这为什么还单独写一篇了，主要是FFmpeg可以做为一个保底方案，其有IO，有解码这些都可做保底方案，在平台对应方案使用不上时，做为备用方案。

## IO解协议

默认会使用zlmediakit来解媒体协议，这个当打开本地媒体文件时，可以使用。

主要有几个问题需要注意。

1. 视频的配置信息在codecpar->extradata中，这个数据一般来说，如果是本地媒体源，则数据以avcc保存，如果是流媒体源，则以Annexb保存，在这统一以把codecpar->extradata里的配置信息如ssp/pps/vps解析出来，并以Annexb分隔保存，后面如MediaCodec/VideoToolbox都需要单独设置。这里如果是zlmediakit就比较简单了，配置帧的数据本身就是以Annexb分隔的。
2. 如果是音频，又是AAC格式，和上面有点类似，如果是本地媒体源，一般是ASC格式信息，如果是流媒体信息，则是adts,注意如果是adts,其codecpar->extradata可能没有信息，需要在后面的随便一个包，一般就是第一个包，提取前面七个字节的信息生成一个配置包，供后面如解码器faad使用。

``` C++
class IOParseFF : public AVSource, public RunTask {
 public:
  IOParseFF();
  virtual ~IOParseFF();

protected:
  AVFormatContextPtr fmtCtx = nullptr;
  AVBSFContextPtr bsf = nullptr;
  std::vector<uint8_t> aacData;
  AudioDesc audioDesc = {};
  // 如果是直播流,可能存在aac extradata,需要把首帧的adts加入
  bool bAACExtradata = false;

private:
  // 解析IO流媒体格式
  bool parseStream(int32_t streamId, AVCodecParameters* codecpar);
  bool parseH26xConfig(int32_t streamId, const uint8_t* extradata, int32_t size,
                       AVCodecID codeId);
  void parseAACConfig(int32_t streamId, const uint8_t* extradata, int32_t size);
  // RunTask
protected:
  virtual void onRunTask() override;

public:
  // 初始化，打开文件/网络流
  virtual bool onOpen() override;
  // 关闭
  virtual void onClose() override;
  // 暂停
  virtual void pause(bool bFlag) override;
  // 能seek吗?
  virtual SeekType seekType() const override;
  // 0-1
  virtual void seekTo(double progress) override;
  // 毫秒时间
  virtual bool seekTo(int64_t pos) override;
  // 返回总时长，单位毫秒
  virtual int64_t duration() const override;
  // 返回进度，0~1
  virtual double progress() const override;

public:
  virtual bool vaild() { return running(); }
  virtual void onSpeed() override;
};


void regFFIO() {
  RegFunc ffIoReg = {"ffmpeg io init", []() {
                       IoPlanDesc ffDesc = {};
                       ffDesc.name = "ffmpeg format";
                       AvoxManager::Get().ioSources.regInitFunc(
                           IoPlan::ffmpeg, ffDesc,
                           []() -> AVSource* { return new IOParseFF(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(ffIoReg);
}

// 解码中断回调函数
int decode_interrupt_cb(void *ctx) {
  IOParseFF *ioParse = static_cast<IOParseFF *>(ctx);
  if (!ioParse) {
    return 1;
  }
  return 0;
}

IOParseFF::IOParseFF() {
  taskName = "ffmpeg io parse";
  // 如果是apple平台，ffmpeg硬解需要分包
#if __APPLE__
  bvcc = false;
#else
  bvcc = true;
#endif
}

IOParseFF::~IOParseFF() {}

bool IOParseFF::parseStream(int32_t streamId, AVCodecParameters *codecpar) {
  // 如果不是视频和音频，直接返回
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
      codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    return true;
  }
  AVBSFContext *vbsf = nullptr;
  int32_t ret = 0;
  if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (codecpar->codec_id == AV_CODEC_ID_H264 ||
        codecpar->codec_id == AV_CODEC_ID_H265) {
      // 得到SPS/PPS/VPS信息
      parseH26xConfig(streamId, codecpar->extradata, codecpar->extradata_size,
                      codecpar->codec_id);
    }
  }
  if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && codecpar->extradata_size > 0){
    if(codecpar->codec_id == AV_CODEC_ID_AAC) {
      parseAACConfig(streamId, codecpar->extradata, codecpar->extradata_size);
      bAACExtradata = true;
    }else {
      AvoxPacket adts = {};
      adts.data.bRef = true;
      adts.data.data = codecpar->extradata;
      adts.data.size = codecpar->extradata_size;
      adts.prefixSize = 0;
      adts.packtype = (int32_t)PackType::aconfig;
      adts.index = aIndexMaps[streamId];
      adts.pts = 0;
      adts.dts = 0;
      dispatch(&IAVSourceOb::onPacket, adts);
    }
  }
  return true;
}

bool IOParseFF::parseH26xConfig(int32_t streamId, const uint8_t *extradata,
                                int32_t size, AVCodecID codecId) {
  std::vector<PacketBuf> packets;
  AvoxData extradataBuf = {(uint8_t *)extradata, size, true};
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
  for (auto &packet : packets) {
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

void IOParseFF::parseAACConfig(int32_t streamId, const uint8_t *extradata,
                               int32_t size) {
  // ASC格式信息
  if (size >= 2) {
    // 前两个字节是音频配置信息
    unsigned int config = (extradata[0] << 8) | extradata[1];
    // 0:AAC-Main 1:AAC-SLC 2:AAC-SSR 3:AAC-LTP
    // 解析AAC对象类型
    int profile = (config >> 11) & 0x1F;
    // 解析采样率索引
    int sampling_frequency_index = (config >> 7) & 0x0F;
    static const int sampling_frequencies[] = {96000, 88200, 64000, 48000,
                                               44100, 32000, 24000, 22050,
                                               16000, 11025, 8000,  7350};
    if (sampling_frequency_index < 12) {
      log(LogLevel::info,
          "AAC sample rate: ", sampling_frequencies[sampling_frequency_index]);
    }

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
    bAACExtradata = true;
  }
}

void IOParseFF::onRunTask() {
  // 解析IO流媒体格式
  AVFormatContext *temp = avformat_alloc_context();
  temp->interrupt_callback.callback = decode_interrupt_cb;
  temp->interrupt_callback.opaque = this;
  AVDictionary *dict = nullptr;
  av_dict_set(&dict, "max_delay", "3000000", 0);
  av_dict_set(&dict, "stimeout", "2000000", 0);
  av_dict_set(&dict, "reorder_queue_size", "2000", 0);
  int ret = avformat_open_input(&temp, uri.c_str(), nullptr, &dict);
  av_dict_free(&dict);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_open_input failed")
    avformat_free_context(temp);
    dispatch(&IOParseOb::onError, ffIoError(ret), "open input failed");
    return;
  }
  fmtCtx = getUniquePtr(temp);
  // 检查是否有全局头,相应的SPS/VPS/ATDS保存在extradata里
  if (fmtCtx->iformat->flags & AVFMT_GLOBALHEADER) {
  }
  if ((ret = avformat_find_stream_info(fmtCtx.get(), nullptr)) < 0) {
    dispatch(&IOParseOb::onError, ffIoError(ret), "open stream failed");
    return;
  }
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto &st = fmtCtx->streams[i];
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !bDisableVideo) {
      VTrackDesc vdesc = {};
      vdesc.codecId = ffVCodec(st->codecpar->codec_id);
      vdesc.trackId = i;
      vdesc.timeBase = {st->time_base.num, st->time_base.den};
      vdesc.desc.width = st->codecpar->width;
      vdesc.desc.height = st->codecpar->height;
      vdesc.desc.fps = av_q2d(st->codecpar->framerate);
      vdesc.desc.type = ffYuvType((AVPixelFormat)st->codecpar->format);
      videoTracks.push_back(vdesc);
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
      adesc.trackId = i;
      adesc.timeBase = {st->time_base.num, st->time_base.den};
      adesc.desc.sampleRate = st->codecpar->sample_rate;
      adesc.desc.format = ffAudioFromat(st->codecpar->format);
      adesc.desc.channels = st->codecpar->ch_layout.nb_channels;
      audioTracks.push_back(adesc);
      // 单独给AAC配置头文件使用
      audioDesc = adesc.desc;
    }
  }
  dispatch(&IOParseOb::onOpen);
  // 重置相应状态
  bFirstPacket = true;
  baseTimeMS = 0;
  prePts = AV_NOPTS_VALUE;
  bAACExtradata = false;
  // 检查是否有音频流
  bool bHavaAudio = audioTracks.size() > 0;
  // 保存解码配置信息
  for (int32_t i = 0; i < fmtCtx->nb_streams; i++) {
    auto &st = fmtCtx->streams[i];
    // 查找流信息，包含SPS/PPS/ATDS等
    if (!parseStream(i, st->codecpar)) {
    }
  }
  // 已经解析配置信息，如SPS,PPS后,开始读取IO数据
  AVPacketPtr pkt = getUniquePtr(av_packet_alloc());
  AVPacketPtr bsfPkt = getUniquePtr(av_packet_alloc());
  AVPacketPtr adtsPkt = getUniquePtr(av_packet_alloc());
  while (running()) {
    if ((ret = av_read_frame(fmtCtx.get(), pkt.get())) < 0) {
      if (ret == AVERROR_EOF) {
        dispatch(&IOParseOb::onComplete);
      } else {
        dispatch(&IOParseOb::onError, ffIoError(ret), "read frame failed");
      }
      break;
    }
    int32_t streamId = pkt->stream_index;
    auto st = fmtCtx->streams[streamId];
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
    AVPacket *refPkt = pkt.get();
    if (packType == PackType::video && bsf) {
      av_bsf_send_packet(bsf.get(), pkt.get());
      av_bsf_receive_packet(bsf.get(), bsfPkt.get());
      prefixSize = 4;
      refPkt = bsfPkt.get();
    } else if (packType == PackType::audio) {
      if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        // 添加adts头
        if (!bAdtsHeader((uint8_t *)pkt->data, pkt->size)) {
          const int32_t adtsHeaderSize = 7;
          // 尽量减少重新分配的机率
          if (aacData.size() < pkt->size + adtsHeaderSize) {
            aacData.resize(pkt->size * 2 + adtsHeaderSize);
          }
          // 添加adts头
          adtsHeader(audioDesc, aacData.data(), pkt->size);
          // 复制数据
          memcpy(aacData.data() + adtsHeaderSize, pkt->data, pkt->size);
          adtsPkt->data = aacData.data();
          adtsPkt->size = pkt->size + adtsHeaderSize;
          // av_packet_copy_props(adtsPkt.get(), pkt.get());
          adtsPkt->pts = pkt->pts;
          adtsPkt->dts = pkt->dts;
          adtsPkt->duration = pkt->duration;
          adtsPkt->flags = pkt->flags;
          adtsPkt->stream_index = pkt->stream_index;
          // 更新当前引用packet
          refPkt = adtsPkt.get();
          prefixSize = 7;
        }else if(!bAACExtradata){
          // 如果是AAC流,并且没有extradata数据,把第一个包的adts头做为配置包
          bAACExtradata = true;
          // 如果是直播流,前面可能没有extradata数据,在这添加
          AvoxPacket adts = {};
          adts.data.bRef = true;
          adts.data.data = (uint8_t *)pkt->data;
          adts.data.size = 7;
          adts.prefixSize = 7;
          adts.packtype = (int32_t)PackType::aconfig;
          adts.index = streamId;
          adts.pts = 0;
          adts.dts = 0;
          dispatch(&IOParseOb::onPacket, adts, 0);
        }
      }
    }
    // 去掉ffmpeg添加的信息
    av_packet_free_side_data(refPkt);
    // B帧可能没有pts
    if (refPkt->pts != AV_NOPTS_VALUE) {
      refPkt->pts = refPkt->dts;
    }
    // 时间全转成毫秒
    refPkt->pts = av_rescale_q(refPkt->pts, st->time_base, {1, 1000});
    refPkt->dts = av_rescale_q(refPkt->dts, st->time_base, {1, 1000});
    refPkt->duration = av_rescale_q(refPkt->duration, st->time_base, {1, 1000});
    // 如果有音频，以第一个音频包的时间为基准，因为视频包前几个PTS可能无效
    // FFmpeg拉流有可能前几个视频包的PTS会特别小
    if (bFirstPacket) {
      if ((bHavaAudio && packType == PackType::audio) ||
          (!bHavaAudio && packType == PackType::video)) {
        baseTimeMS = refPkt->pts;
        if (baseTimeMS < -86400000) {
          log(LogLevel::warn, "ffmepg io base time too samell:", refPkt->pts);
          baseTimeMS = 0;
        }
        log(LogLevel::info, "ffmepg io base time:", baseTimeMS);
        bFirstPacket = false;
      }
    }
    prePts = refPkt->pts;
    AvoxPacket packet = ffAvoxPacket(refPkt);
    packet.packtype = (int32_t)packType;
    packet.prefixSize = prefixSize;
    // 减去baseTimeMS
    if (packet.pts < baseTimeMS) {
      packet.pts = baseTimeMS;
    }
    if (packet.dts < baseTimeMS) {
      packet.dts = baseTimeMS;
    }
    packet.pts = packet.pts - baseTimeMS;
    packet.dts = packet.dts - baseTimeMS;
    dispatch(&IOParseOb::onPacket, packet, 0);
    // 测试，模拟网络不好，卡顿的情况
    // int32_t sleepMs = randInt(20,60);
    // std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    // 加入暂停功能
    waitPause();
  }
}

bool IOParseFF::open(const char *url) {
  uri = url;
  return startTask();
}

void IOParseFF::close() { stopTask(); }

void IOParseFF::pause(bool bFlag) { bPause = bFlag; }

SeekType IOParseFF::seekType() const {
  if (!fmtCtx) {
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

bool IOParseFF::seekTo(int64_t pos) {
  SeekType st = seekType();
  if (!fmtCtx || st == SeekType::none) {
    return false;
  }
  bool bSeek = false;
  pos += baseTimeMS;
  prePts = pos;
  // 先暂停IO线程
  pauseTask();
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

int64_t IOParseFF::position() const {
  if (!fmtCtx) {
    return 0;
  }
  if (prePts != AV_NOPTS_VALUE) {
    return prePts - baseTimeMS;
  }
  return 0;
}
```

## 默认软解

默认所有平台，软解都会使用ffmpeg提供的，和网上一般软解的代码类似，唯一需要注意的，返原IO里的codecpar->extradata信息，也就是配置帧的信息，因为在IO层，所有配置帧都会变成Annexb格式，给ffmpeg解码时，一般直接记录的几个配置Annexb包连接在一起就行，不过也有些解码器，和包的格式要一致，如包是用的avcc,配置信息也要用avcc才行。

``` C++
// 下面FFmpeg里对象前带个A，是对应的unique_ptr带对应释放函数的封装对象
class FFDecoder {
 public:
  FFDecoder();
  virtual ~FFDecoder();

 protected:
  AVCodecContextPtr codecCtx = nullptr;
  AVFramePtr avFrame = nullptr;
  AVPacketPtr avPacket = nullptr;
  PacketBufPtr packBuf = nullptr;
  std::vector<uint8_t> packData;
  int64_t preVPts = 0;

 protected: 
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame* avFrame, bool bDrop) = 0;
  virtual void onError(int32_t error) {} 

 public:
  // 使用FFmpeg解码原始包
  bool decodePacket(PacketBufPtr packet);
  void flushContext();
};

FFDecoder::FFDecoder() {
  avPacket = getUniquePtr(av_packet_alloc());
  avFrame = getUniquePtr(av_frame_alloc());
}

FFDecoder::~FFDecoder() {}

bool FFDecoder::decodePacket(PacketBufPtr packet) {
  if (!codecCtx) {
    return false;
  }
  avPacket->data = packet->buff.data();
  avPacket->size = packet->size;
  avPacket->pts = packet->pts;
  avPacket->dts = packet->dts;
  if (packet->frameType == 1) {
    avPacket->flags |= AV_PKT_FLAG_KEY;
  }
  int32_t ret = avcodec_send_packet(codecCtx.get(), avPacket.get());
  if (ret < 0) {
    // 忽略无效数据错误
    if (ret != AVERROR_INVALIDDATA) {
      AVOX_FFMEPG_LOG_RETURN(ret, false, "avcodec_send_packet failed");
    }
  }
  while (true) {
    ret = avcodec_receive_frame(codecCtx.get(), avFrame.get());
    if (ret < 0) {
      // 没有更多帧可接收，循环结束
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        onError(ret);
        break;
      } else {
        AVOX_FFMEPG_LOG(ret, "avcodec_receive_frame failed");
        break;
      }
    }
    // 注意,现在PacketQueue的锁下
    onFrame(avFrame.get(), false);
    // log(LogLevel::info, "packet pts:", avFrame->pts);
    // if (preVPts > avFrame->pts) {
    //   log(LogLevel::warn, "---preVPts:", preVPts);
    // }
    preVPts = avFrame->pts;
    // 释放 AVFrame 引用
    av_frame_unref(avFrame.get());
  }
  return true;
}

void FFDecoder::flushContext() {
  if (!codecCtx) {
    return;
  }
  avcodec_flush_buffers(codecCtx.get());
}

class FFVDecoder : public VideoDecoder, public FFDecoder {
public:
  FFVDecoder();
  virtual ~FFVDecoder();

  // AVDecoder
public:
  // 初始化
  virtual bool onInit() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual bool decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;

  // FFDecoder
protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;
  virtual void onError(int32_t error) override;

protected:
  virtual void onAttachContext();
};

FFVDecoder::FFVDecoder() { codecTH = VCodecTh::cpu; }

FFVDecoder::~FFVDecoder() {}

bool FFVDecoder::onInit() {
  int32_t ret = 0;
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  return true;
}

// 参考ffmpeg/codec_par.c 里avcodec_parameters_to_context实现
DecodeResult FFVDecoder::onPreDecoder() {
  AVCodecID codecId = (AVCodecID)codecDesc.codecId;
  const auto &packets = trackContext->getConfigPackets();
  if ((codecId == AV_CODEC_ID_H264 && packets.size() < 2) ||
      (codecId == AV_CODEC_ID_H265 && packets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  // d3d11va/vulkan这些，非独立解码器，需要借助通用解码器，然后硬件加速
  // 在这借onAttachContext生成对应的d3d11/vulkan硬件环境
  auto codec = avcodec_find_decoder_by_name(codecDesc.name.c_str());
  if (!codec) {
    codec = avcodec_find_decoder(codecId);
    log(LogLevel::warn, "codec:", codecDesc.name,
        " no exists,use default codec:", avcodec_get_name(codecId));
  }
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  onAttachContext();
  if (parseConfigs()) {
    // avcodec_parameters_to_context
    codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx->pix_fmt = getFFVideoFormat(params.yuvType);
    codecCtx->width = params.width;
    codecCtx->height = params.height;
  }
  if (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_H265) {
    std::vector<uint8_t> extradata;
    // H265解码，发现有些解码器要求配置帧格式与数据包要求一致
    IoPackFormat packFormat = getIoPackFormat();
    if (packFormat == IoPackFormat::annexb) {
      annexbCombin(packets, extradata);
    } else {
      if (codecId == AV_CODEC_ID_H264) {
        h264Annexb2Avcc(packets, extradata);
      } else {
        h265Annexb2Hvcc(packets, extradata);
      }
    }
    AvoxData extradataBuf = {extradata.data(), (int32_t)extradata.size(), true};
    log(LogLevel::info, "ffmpeg decoder extradata size:", extradataBuf);
    // extradata包含SPS,PPS等编码信息
    codecCtx->extradata_size = extradata.size();
    codecCtx->extradata =
        (uint8_t *)av_malloc(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(codecCtx->extradata, extradata.data(), extradata.size());
    // 别的一些设置
    codecCtx->thread_count = 1;
  }
  int32_t ret = avcodec_open2(codecCtx.get(), codec, nullptr);
  AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
  if (ret < 0) {
    return DecodeResult::openFailed;
  }
  return DecodeResult::success;
}

bool FFVDecoder::decode(const AvoxPacket & packet) { 
  bool bRet = decodePacket(packet);
  return bRet;
}

void FFVDecoder::flush() { flushContext(); }

void FFVDecoder::onFrame(AVFrame *avFrame, bool bDrop) {
  YUVFrame frame = {};
  frame.pts = avFrame->best_effort_timestamp;
  frame.dts = avFrame->pkt_dts;
  frame.format.width = avFrame->width;
  frame.format.height = avFrame->height;
  frame.format.type = ffYuvType((AVPixelFormat)avFrame->format);
  frame.data[0] = avFrame->data[0];
  frame.data[1] = avFrame->data[1];
  frame.data[2] = avFrame->data[2];
  frame.stride[0] = avFrame->linesize[0];
  frame.stride[1] = avFrame->linesize[1];
  frame.stride[2] = avFrame->linesize[2];
  frame.keyFrame = avFrame->pict_type == AV_PICTURE_TYPE_I;
  dispatch(&IVideoDecoderOb::onDecode, frame); 
}

void FFVDecoder::onError(int32_t ret) {
  if (ret == AVERROR_EOF) {
    dispatch(&IVideoDecoderOb::onComplete);
  }
}

void FFVDecoder::onAttachContext() {}
```

## Windows平台硬解

Windows平台硬解，我最开始是想集成原生的Dx11VA或是vulkan本身的硬解，后面发现需要维护队列，需要解析nalu包，需要维护解码队列里图，以及长引用短引用，非短时间能集成，而我这边时间有限，故在windows平台集成ffmpeg的dx11硬解。

Windows平台有很多硬解方案，但是各有各的特点，如Intel/AMD/NVIDIA都有各自对应硬解实现方案，只有DX11/vulkan是统一在各硬件都能使用的，所以我会优先考虑这二种硬解方案。

需要注意，单独的如h264_dx11va/hevc_dx11va非一个解码器，不同h264_qsv/hevc_qsv可以通过avcodec_find_decoder_by_name直接查找到，而h264_dx11va/hevc_dx11va定义为硬件加速，是需要在h264/h265通用解码器上，使用硬件加速，就是借助hw_device_ctx的设置来完成硬解。

``` C++
#include "FFVDecoder.hpp"
#include "avox/player/VideoDecoder.hpp"
#include <queue>

#if _WIN32
#include "avox_windows/dx11/Dx11Graph.hpp"
#endif

namespace avox {

#if _WIN32

// 自定义比较函数，按 PTS 升序排序
struct ComparePTS {
  bool operator()(const GpuFrame &a, const GpuFrame &b) {
    return a.pts > b.pts; // 小顶堆
  }
};

class FFDx11Decoder : public FFVDecoder, public Dx11Context {
public:
  FFDx11Decoder();
  virtual ~FFDx11Decoder();

public:
  // 初始化
  virtual bool onVaild() override;

protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;

protected:
  virtual void onAttachContext() override;
  virtual void onDetachContext() override;

protected:
  AVBufferRef *hwBuffer = nullptr;
  // 保存DX11纹理,B帧可能用纹理数组，但是数组又不是轮循在用
  // 导致需要保存出来，当前重新生成一个纹理数组，轮循使用索引
  MComPtr<ID3D11Texture2D> copyTexture;
  int32_t index = 0;
  int32_t arraySize = 0;
};

#endif

#if _WIN32

void regFFDx11Decoder() {
  RegFunc regFunc = {"ffmpeg dx11 video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H264_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoDecoder* {
                             return new FFDx11Decoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H265_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoDecoder* {
                             return new FFDx11Decoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFDx11Decoder::FFDx11Decoder() { codecTH = VCodecTh::dx11; }

FFDx11Decoder::~FFDx11Decoder() {
  onDetachContext();
}

bool FFDx11Decoder::onVaild() {
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  return true;
}

void FFDx11Decoder::onAttachContext() {
  // 重新生成,释放老的,可能分辨率变化后重置解码器了
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
  // 如果是Debug模式，添加Debug信息
  AVDictionary* opts = NULL;
#if AVOX_DEBUG
  av_dict_set(&opts, "debug", "1", 0);
#endif
  av_dict_set_int(&opts, "surfaces", 8, 0);
  // 创建 D3D11 硬件设备上下文
  int32_t ret = av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_D3D11VA,
                                       NULL, opts, 0);
  av_dict_free(&opts);
  if (ret < 0) {
    LOGFLF(LogLevel::error, "av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  // 获取 AVHWDeviceContext 指针
  AVHWDeviceContext* hwCtx = (AVHWDeviceContext*)hwBuffer->data;
  AVD3D11VADeviceContext* dx11Ctx = (AVD3D11VADeviceContext*)hwCtx->hwctx;
  // 设置 D3D11 设备上下文
  device = dx11Ctx->device;
  setDevice(device);
  codecCtx->hw_device_ctx = av_buffer_ref(hwBuffer);
  LOGFLF(LogLevel::info, "dx11 onAttachContext hwBuffer:", hwBuffer,
         " codecCtx->hw_device_ctx:", codecCtx->hw_device_ctx);
  copyTexture.Reset();
}

void FFDx11Decoder::onDetachContext() {
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
}

void FFDx11Decoder::onFrame(AVFrame *avFrame, bool bDrop) {
  // codecCt有可能不是用的硬件解码器
  AVPixelFormat dx11Format = (AVPixelFormat)avFrame->format;
  if (dx11Format != AV_PIX_FMT_D3D11) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  // hwcontext_d3d11va.h
  ID3D11Texture2D *dx11Texture = (ID3D11Texture2D *)avFrame->data[0];
  if (!dx11Texture) {
    log(LogLevel::info, "dx11Texture is null");
    return;
  }
  int64_t queueIndex = reinterpret_cast<intptr_t>(avFrame->data[1]);
  log(LogLevel::info, "array index:", queueIndex, " pts:", avFrame->pts,
      " dts:", avFrame->pkt_dts);
  GpuFrame frame = {};
  frame.pts = avFrame->pts;
  frame.dts = avFrame->pkt_dts;
  frame.buffer = dx11Texture;
  frame.format.width = avFrame->width;
  frame.format.height = avFrame->height;  
  frame.context = this;
  frame.queueIndex = queueIndex;
  dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
  // // 将帧添加到优先队列
  // frameQueue.push(frame);
  // // 当队列中的元素数量达到阈值时，依次取出并分发
  // while (frameQueue.size() >= FRAME_QUEUE_THRESHOLD) {
  //   GpuFrame sortedFrame = frameQueue.top();
  //   frameQueue.pop();
  //   log(LogLevel::info, "array index:", sortedFrame.queueIndex,
  //       " pts:", sortedFrame.pts, " dts:", sortedFrame.dts);
  //   dispatch(&IVideoDecoderOb::onDecodeGpu, sortedFrame);
  // }
}

void FFDx11Decoder::onAttachContext() {
  codecCtx->hw_device_ctx = hwBuffer;
  LOGFLF(LogLevel::info, "dx11 onAttachContext hwBuffer:", hwBuffer,
         " codecCtx->hw_device_ctx:", codecCtx->hw_device_ctx);
}

void FFDx11Decoder::onFrameRelease(bool bRender, const GpuFrame &frame) {
  // 如果当前画面需要渲染，才做NV12转RGBA
  if (bRender) {
    // Computeshader 把NV12等格式转换为RGBA8
    dx11Graph->renderToTexture(frame);
    texture = dx11Graph->getTexture();
  }
}

#endif
```

这个数据解出来后，我这边发现是NV12格式的数据，直接显示NV12的数据到窗口一般不行，需要先做个处理，这里借助computeshader来完成，为什么不用vs/ps来做，主要是我这边只有vulkan图像滤境很完善[Vulkan移植GPUImage总结](https://zhuanlan.zhihu.com/p/373137758),dx11数据能直接走GPU传输到vulkan纹理[Vulkan与DX11交互](https://zhuanlan.zhihu.com/p/349534525),也不需要担心GPU-CPU-GPU这些数据拷贝的消耗，所以我这边只需要一个RGBA的dx11图就可以应用这边的vulkan图像处理图。

``` C++

// 把NV12纹理转换为RGBA8纹理
class Dx11Graph : public Dx11Context {
public:
  Dx11Graph() {};
  virtual ~Dx11Graph() {};

protected:
  // YUV 2 RGBA8 shader
  MComPtr<ID3D11ComputeShader> computeShader = nullptr;
  // 计算着色器资源
  std::unique_ptr<Dx11Texture> outTexture = nullptr;
  // 常量缓冲区
  std::unique_ptr<Dx11Constant> constBuf = nullptr;
  // 解码的纹理没有D3D11_BIND_SHADER_RESOURCE,不能直接生成SRV
  MComPtr<ID3D11Texture2D> inTexture = nullptr;
  MComPtr<ID3D11ShaderResourceView> yView = nullptr;
  MComPtr<ID3D11ShaderResourceView> uvView = nullptr;
  uint32_t imageWidth = 0;
  uint32_t imageHeight = 0;
  D3D11_TEXTURE2D_DESC yuvDesc = {};

protected:
  // 初始化图形管线
  void initGraph();

public:
  void renderToTexture(const GpuFrame &gpuFrame);
};

static const char *yuvToRgbaShader = R"(
Texture2D yTex: register(t0);
Texture2D uvTex: register(t1);
RWTexture2D<float4> outTex: register(u0);

cbuffer CBParameters : register(b0)
{
    uint2 inputSize;
};

// YUV 转 RGB 函数
float4 yuv2Rgb(float y, float u, float v, float a) {
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    return float4(clamp(r, 0, 1), clamp(g, 0, 1), clamp(b, 0, 1), a);
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint2 size = inputSize;    
    if(DTid.x >= size.x || DTid.y >= size.y){
        return;
    }
    float y = yTex.Load(int3(DTid, 0)).r;
    uint2 uvCoord = uint2(DTid.x / 2, DTid.y / 2);
    float2 uv = uvTex.Load(int3(uvCoord, 0)).rg - float2(0.5f, 0.5f);
    float4 rgba = yuv2Rgb(y, uv.x, uv.y, 1.0f);
    outTex[DTid] = rgba;
    // outTex[DTid] = float4(0.5f, 0.5f, 0.5f, 1.0f); 
}
)";

void Dx11Graph::initGraph() {
  if (!device || !d3dcontext) {
    return;
  }
  // 编译着色器
  ID3DBlob *shaderBlob = nullptr;
  ID3DBlob *errorBlob = nullptr;
  HRESULT hr =
      D3DCompile(yuvToRgbaShader, strlen(yuvToRgbaShader), nullptr, nullptr,
                 nullptr, "main", "cs_5_0", 0, 0, &shaderBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob) {
      log(LogLevel::warn, "Dx11Graph D3DCompile error: ",
          (char *)errorBlob->GetBufferPointer());
      errorBlob->Release();
    }
    if (shaderBlob) {
      shaderBlob->Release();
    }
    return;
  }
  // 创建计算着色器
  hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                   shaderBlob->GetBufferSize(), nullptr,
                                   &computeShader);
  if (FAILED(hr)) {
    shaderBlob->Release();
    return;
  }
  shaderBlob->Release();
  // 创建常量缓冲区
  constBuf = std::make_unique<Dx11Constant>();
  constBuf->setBufferSize(sizeof(uint32_t) * 2);
  std::vector<uint32_t> constData = {imageWidth, imageHeight};
  constBuf->cpuData = (uint8_t *)constData.data();
  constBuf->initResource(device);
  // 创建输出计算着色器资源
  outTexture = std::make_unique<Dx11Texture>();
  outTexture->setOnlyUAV(true);
  outTexture->setTextureSize(imageWidth, imageHeight,
                             DXGI_FORMAT_R8G8B8A8_UNORM);
  outTexture->initResource(device);
  // 创建输入复制纹理
  D3D11_TEXTURE2D_DESC inCopyDesc = yuvDesc;
  inCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  inCopyDesc.MipLevels = 1;
  inCopyDesc.ArraySize = 1;
  inCopyDesc.SampleDesc.Count = 1;
  inCopyDesc.Usage = D3D11_USAGE_DEFAULT;
  device->CreateTexture2D(&inCopyDesc, nullptr, &inTexture);
  // 创建输入纹理的SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = inCopyDesc.MipLevels;
  device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &yView);
  srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
  device->CreateShaderResourceView(inTexture.Get(), &srvDesc, &uvView);
  // 设置当前纹理
  texture = outTexture->texture.Get();
}

void Dx11Graph::renderToTexture(const GpuFrame &gpuFrame) {
  if (gpuFrame.buffer == nullptr) {
    return;
  }
  ID3D11Texture2D *yuvTexture = (ID3D11Texture2D *)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  if (gpuFrame.format.width != imageWidth ||
      gpuFrame.format.height != imageHeight || desc.Width != yuvDesc.Width ||
      desc.Height != yuvDesc.Height || !computeShader) {
    imageWidth = gpuFrame.format.width;
    imageHeight = gpuFrame.format.height;
    yuvDesc = desc;
    initGraph();
  }
  if (!computeShader || !outTexture || !constBuf || !inTexture || !yView ||
      !uvView) {
    return;
  }
  if (desc.ArraySize > 1) {
    // 复制指定索引的切片到临时纹理
    d3dcontext->CopySubresourceRegion(
        inTexture.Get(), D3D11CalcSubresource(0, 0, desc.MipLevels), 0, 0, 0,
        yuvTexture,
        D3D11CalcSubresource(0, gpuFrame.queueIndex, desc.MipLevels), nullptr);
    // logTexture(device, inTexture.Get());
  } else {
    // 将 yuvTexture 的数据复制给 inTexture
    d3dcontext->CopyResource(inTexture.Get(), yuvTexture);
  }
  // 设置常量缓冲区
  d3dcontext->CSSetConstantBuffers(0, 1, constBuf->buffer.GetAddressOf());
  // 设置计算着色器资源
  d3dcontext->CSSetShader(computeShader.Get(), nullptr, 0);
  // 设置输入纹理
  ID3D11ShaderResourceView *srvArray[2] = {yView.Get(), uvView.Get()};
  d3dcontext->CSSetShaderResources(0, 2, srvArray);
  // 设置输出纹理的 UAV
  ID3D11UnorderedAccessView *uavArray[1] = {outTexture->uavView.Get()};
  d3dcontext->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
  // 执行计算着色器
  uint32_t groupX = divUp(imageWidth, 16);
  uint32_t groupY = divUp(imageHeight, 16);
  d3dcontext->Dispatch(groupX, groupY, 1);
  // logTexture(device, outTexture->texture.Get());
}

```

## Vulkan渲染

如下是用于所有平台的软解CPU到Vulkan渲染，以及windows平台硬解后到vulkan渲染的通用渲染。

``` C++
class VkVideoRender : public VideoRender,
                      public RunTask,
                      public IVOutputLayerOb {
public:
  VkVideoRender() {};
  virtual ~VkVideoRender() {};

protected:
  std::unique_ptr<VkPipeGraph> graph = nullptr;
  VKTNodePtr<VkInputLayer> inputLayer = nullptr;
  VKTNodePtr<VkYUV2RGBALayer> yuv2RGBA = nullptr;
  VKTNodePtr<VkOutputLayer> outputLayer = nullptr;
  bool bPause = false;
#ifdef WIN32
  bool bDx11Decoder = false;
  IRenderContext *dx11Context = nullptr;
#endif

public:
  VkOutputLayer *getOutputLayer() {
    if (!outputLayer) {
      return nullptr;
    }
    return outputLayer->get();
  }

public:
  virtual void start() override;
  virtual void pause(bool pause) override;
  virtual void flush() override;
  virtual void close() override;

protected:
  virtual void onRunTask() override;

  // IVOutputLayerOb
public:
  virtual void onImageChange(const ImageFormat &format) override;
  virtual void onCpuData(IImageBuffer *buffer) override;

  // IWindowOb
public:
  virtual void onWinUpdate(IWindow *context) override;
};

void regVkRender() {
  RegFunc regFunc = {
      "vk render init", []() {
        VRenderDesc vkRenderDesc = {};
        vkRenderDesc.name = "Vk Render";
        AvoxManager::Get().vRender.regInitFunc(
            RenderType::Vulkan, vkRenderDesc,
            []() -> VideoRender * { return new VkVideoRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

void VkVideoRender::start() {
  // 查找track的解码器是否是FFVkDecoder
  VkContext *ctx = nullptr;
#if AVOX_ENABLE_FFMPEG
  VideoDecoder *videoDecoder = trackContext->getVideoDecoder();
  VCodecTh codecTH = videoDecoder->getCodecTh();
  if (videoDecoder) {
    if (codecTH == VCodecTh::vulkan) {
      FFVkDecoder *ffVkDecoder = dynamic_cast<FFVkDecoder *>(videoDecoder);
      if (ffVkDecoder) {
        ctx = ffVkDecoder;
      }
    }
#ifdef WIN32
    bDx11Decoder = codecTH == VCodecTh::dx11;
    if (bDx11Decoder) {
      FFDx11Decoder *dx11Decoder = dynamic_cast<FFDx11Decoder *>(videoDecoder);
      if (dx11Decoder) {
        dx11Context = dx11Decoder;
      }
    }
#endif
  }
#endif
  graph = std::make_unique<VkPipeGraph>(ctx);
  inputLayer = graph->addNode<VkInputLayer>();
  yuv2RGBA = graph->addNode<VkYUV2RGBALayer>();
  outputLayer = graph->addNode<VkOutputLayer>();
  if (bDx11Decoder) {
    inputLayer->addLine(outputLayer);
  } else {
    inputLayer->addLine(yuv2RGBA)->addLine(outputLayer);
  }
  OutputParamet outputParamet = {};
  outputParamet.bCpu = false;
  outputParamet.bGpu = true;
  outputLayer->get()->updateParamet(outputParamet);
  // 输出
  outputLayer->get()->addObserver(this);
  //
  taskName = "vk video render thread";
  addWindowOb();
  startTask();
}

void VkVideoRender::pause(bool pause) {
  if (pause) {
    pauseTask();
  } else {
    resumeTask();
  }
}

void VkVideoRender::flush() {}

void VkVideoRender::close() {
  stopTask();
  removeWindowOb();
}

void VkVideoRender::onRunTask() {
  Window *window = getWindow();
  while (running()) {
    checkWindow();
    if (window) {
      graph->setRenderType(window->getRenderContext()->getRenderType());
    }
    bool bYield = false;
    // 拿到帧数据如何处理
    auto frameAction = [&](const VideoFramePtr &frame) {
      // log(LogLevel::info, "onFrame pts:", frame->pts);
      if (frame->buffer->getCodecTh() == VCodecTh::cpu) {
        // frame->release(true);
        VideoBufferHost *vbuffer =
            dynamic_cast<VideoBufferHost *>(frame->buffer.get());
        if (!vbuffer) {
          return;
        }
        if (yuv2RGBA->get()->getParamet() != vbuffer->getYuvType()) {
          yuv2RGBA->get()->updateParamet(vbuffer->getYuvType());
        }
        inputLayer->get()->inputCpuData(vbuffer, false);
        graph->run();
      } else if (frame->buffer->getCodecTh() == VCodecTh::dx11) {
        HwVideoBuffer *vbuffer =
            dynamic_cast<HwVideoBuffer *>(frame->buffer.get());
        if (vbuffer) {
          // 把解码的纹理转换成RGBA纹理
          frame->release(true);
          if (dx11Context) {
            inputLayer->get()->inputGpuData(dx11Context);
            graph->run();
          }
        }
      }
    };
    // 确定是否需要刷新画画
    if (trackContext->syncVideo()) {
      //  FrameQueue里的线程锁下执行
      bool bGet = trackContext->getFrameQueue().dequeueAction(frameAction);
      trackContext->onFrameResult(bGet);
    }
    // 当前帧比较多时，不等待太久
    if (trackContext->getFrameQueue().size() > 5) {
      bYield = true;
    }
    sleepTask(bYield);
    // sleepTask(false,30);
    // 加入暂停功能
    waitPause();
  }
}

void VkVideoRender::onImageChange(const ImageFormat &format) {
  LOGFLF(LogLevel::info, "onImageChange width:", format.width,
         " height:", format.height,
         " format:", getImageTypeStr(format.imageType));
}

void VkVideoRender::onCpuData(IImageBuffer *buffer) {
  uint8_t *data = buffer->getPointer();
  LOGFLF(LogLevel::info, "onCpuData");
}

void VkVideoRender::onWinUpdate(IWindow *context) {
  // LOGFLF(LogLevel::info, "onUpdate");
  if (context->getRenderType() == RenderType::Vulkan) {
    VkRenderContext *vkContext = static_cast<VkRenderContext *>(context);
    VkImage winImage = vkContext->getTexture();
    VkCommandBuffer cmd = vkContext->getCommandBuffer();
    // 我们要把cs生成的图复制到正在渲染的图上,先改变渲染图的layout
    changeLayout(cmd, winImage, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    outputLayer->get()->outputGpuData(vkContext);
    // 复制完成后,改变渲染图的layout准备呈现
    changeLayout(cmd, winImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  } else {
    outputLayer->get()->outputGpuData(context);
  }
}
```

然后是vulkan窗口，dx11/dx12窗口的实现，结合[Vulkan与DX11交互](https://zhuanlan.zhihu.com/p/349534525)，可以把VkVideoRender渲染后的数据分别给到vulkan窗口，dx11/dx12窗口，这部分代码实现比较多，就不贴了。

这边等有时间，就先把ffmpeg里的vulkan硬解方案集成下，这个多个平台就多了一种硬解的备选方案，并且针对结合vulkan渲染应该也是最高效的实现方式了。