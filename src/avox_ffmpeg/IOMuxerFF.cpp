#include "IOMuxerFF.hpp"

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

void regFFMuxer() {
  RegFunc ffIoReg = {"ffmpeg muxer init", []() {
                       MuxerDesc ffDesc = {};
                       ffDesc.name = "ffmpeg";
                       AvoxManager::Get().muxers.regInitFunc(
                           MuxerType::ffmpeg, ffDesc,
                           []() -> IOMuxer* { return new IOMuxerFF(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(ffIoReg);
}

IOMuxerFF::IOMuxerFF() {
  // check_all_protocols();
  packet = getUniquePtr(av_packet_alloc());
}

IOMuxerFF::~IOMuxerFF() { onClose(); }

bool IOMuxerFF::onInit() {
  // 视频一定需要配置帧
  if (bHaveVideo && videoConfigs.empty()) {
    return false;
  }
  const AVOutputFormat* avFormat =
      av_guess_format(nullptr, url.c_str(), nullptr);
  AVFormatContext* tempOut = nullptr;
  int32_t ret = 0;
  const char* formatName = nullptr;
  AVCodecID acodecId = getFFCodecId(aDesc.codecId);
  // mp4只支持AAC,如果非AAC音频,自动换成mov格式
  if (avFormat && strcmp(avFormat->name, "mp4") == 0) {
    if (acodecId != AV_CODEC_ID_AAC) {
      avFormat = av_guess_format("mov", nullptr, nullptr);
    }
    ret = avformat_alloc_output_context2(&tempOut, avFormat, nullptr,
                                         url.c_str());
  } else if (!avFormat) {
    // 无扩展名的协议 URL，按协议映射到对应的 muxer 名称
    std::string protocol = url.substr(0, url.find("://"));
    std::string muxerName = protocol;
    // RTMP 系列承载 FLV 数据，FFmpeg 无 "rtmp" muxer，需映射到 flv
    if (protocol == "rtmp" || protocol == "rtmps" || protocol == "rtmpt" ||
        protocol == "rtmpts") {
      muxerName = "flv";
    } else if (protocol == "rtsps") {
      muxerName = "rtsp";
    }
    avFormat = av_guess_format(muxerName.c_str(), nullptr, nullptr);
    ret = avformat_alloc_output_context2(&tempOut, avFormat, nullptr,
                                         url.c_str());
  }
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avformat_alloc_output_context2 failed");
    return false;
  }
  fmtCtx = getUniquePtr(tempOut);
  AVCodecID vcodecId = getFFCodecId(vDesc.codecId);
  if (!videoConfigs.empty() && vcodecId != AV_CODEC_ID_NONE) {
    const AVCodec* codec = avcodec_find_decoder(vcodecId);
    videoStream = avformat_new_stream(fmtCtx.get(), codec);
    if (videoStream) {
      AVCodecParameters* params = avcodec_parameters_alloc();
      params->codec_type = AVMEDIA_TYPE_VIDEO;
      params->codec_id = vcodecId;
      params->width = vDesc.desc.width;
      params->height = vDesc.desc.height;
      params->codec_tag = 0;
      if (vDesc.desc.type != YuvType::other) {
        params->format = getFFVideoFormat(vDesc.desc.type);
      }
      std::vector<uint8_t> extradata;
      // 数据包与配置帧统一使用annexb
      annexbCombin(videoConfigs, extradata);
      params->extradata_size = extradata.size();
      params->extradata =
          (uint8_t*)av_malloc(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
      memcpy(params->extradata, extradata.data(), extradata.size());
      // 把params信息给stream
      avcodec_parameters_copy(videoStream->codecpar, params);
      avcodec_parameters_free(&params);
    }
  }
  // 音频
  if (acodecId != AV_CODEC_ID_NONE) {
    const AVCodec* codec = avcodec_find_decoder(acodecId);
    audioStream = avformat_new_stream(fmtCtx.get(), codec);
    // audioStream->nb_frames = 1024;
    if (audioStream) {
      AVCodecParameters* params = avcodec_parameters_alloc();
      params->codec_type = AVMEDIA_TYPE_AUDIO;
      params->codec_id = acodecId;
      params->sample_rate = aDesc.desc.sampleRate;
      params->ch_layout.nb_channels = aDesc.desc.channels;
      params->format = getFFAudioFormat(aDesc.desc.format);
      // AAC现测试来看，RTSP需要设置extradata
      if (acodecId == AV_CODEC_ID_AAC) {
        params->frame_size = 1024;
        // 解码配置信息
        params->extradata_size = 5;
        params->extradata =
            (uint8_t*)av_malloc(5 + AV_INPUT_BUFFER_PADDING_SIZE);
        ascHeader(aDesc.desc, params->extradata, 2);
      }
      // 把params信息给stream
      avcodec_parameters_copy(audioStream->codecpar, params);
      avcodec_parameters_free(&params);
    }
  }
  AVDictionary* dict = nullptr;
  // RTSP推流参数: TCP传输(UDP跨网段易丢包导致超时), 低延迟发送
  if (url.find("rtsp://") == 0 || url.find("rtsps://") == 0) {
    av_dict_set(&dict, "rtsp_transport", "tcp", 0);
    av_dict_set(&dict, "muxdelay", "0.1", 0);
  }
  av_dump_format(fmtCtx.get(), 0, url.c_str(), 1);
  // 输出成文件
  if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open2(&fmtCtx->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &dict);
    if (ret < 0) {
      av_dict_free(&dict);
      AVOX_FFMEPG_LOG(ret, "avio_open2 failed");
      return false;
    }
  }
  ret = avformat_write_header(fmtCtx.get(), &dict);
  av_dict_free(&dict);
  if (ret < 0) {
    // 中断初始化,不需要一直试了
    bInitFailed = true;
    AVOX_FFMEPG_LOG(ret, "avformat_write_header failed");
    return false;
  }
  LOGFLF(LogLevel::info, "success,url:", url);
  return true;
}

void IOMuxerFF::onPushPacket(const AvoxPacket& buffer) {
  if (!fmtCtx) {
    return;
  }
  PackType packType = (PackType)buffer.packtype;
  bool bData = packType == PackType::video || packType == PackType::audio;
  bool bConfig = packType == PackType::vconfig || packType == PackType::aconfig;
  bool bVideo = packType == PackType::video || packType == PackType::vconfig;
  bool bAudio = packType == PackType::audio || packType == PackType::aconfig;
  // 第一个非配置帧的时间
  // if (firstPts == AVOX_NOVALID_PTS && bData) {
  //   firstPts = buffer->pts;
  // }
  AVStream* stream = bVideo ? videoStream : audioStream;
  if (!stream) {
    return;
  }
  av_packet_unref(packet.get());
  packet->stream_index = stream->index;
  packet->data = buffer.data.data;
  packet->size = buffer.data.size;
  if (buffer.frameType == 1) {
    packet->flags |= AV_PKT_FLAG_KEY;
  }
  packet->pts = buffer.pts;
  packet->dts = buffer.dts;
  // buffer里的时间都是毫秒,转化成对应流时间
  if (stream->time_base.num != 0) {
    packet->pts = av_rescale_q(buffer.pts, {1, 1000}, stream->time_base);
    packet->dts = av_rescale_q(buffer.dts, {1, 1000}, stream->time_base);
  }
  // AvoxData ad = {packet->data, std::min(10, packet->size), true};
  // log(LogLevel::info, "push packet:", ad, " pts:", packet->pts,
  //     " size:", packet->size);
  int ret = av_interleaved_write_frame(fmtCtx.get(), packet.get());
  if (ret < 0) {
    // 致命错误: 输出汇已不可用(broken pipe/磁盘满/文件关闭/IO错误/EOF)
    // 仅这类才销毁fmtCtx, 避免后续每次写都刷错误日志
    bool bFatal = ret == AVERROR_EOF || ret == AVERROR(EPIPE) ||
                  ret == AVERROR(ENOSPC) || ret == AVERROR(EBADF) ||
                  ret == AVERROR(EIO) || ret == AVERROR(EFBIG);
    if (bFatal) {
      AVError aerr = ffIoError(ret);
      char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
      av_strerror(ret, errBuf, sizeof(errBuf));
      AVOX_FFMEPG_LOG(ret, "av_interleaved_write_frame fatal",
                     " msg:", errBuf, " buffer size:", buffer.data.size,
                     " pts:", buffer.pts, " video:", bVideo, " audio:", bAudio);
      onError(aerr, errBuf);
      fmtCtx.reset();
    } else {
      // 非致命(如DTS非单调EINVAL): 只丢这一帧, 不销毁fmtCtx
      // 保fmtCtx活着, onClose才能av_write_trailer写出moov, 文件不至于整盘作废
      // 只记首帧, 避免脏流刷屏; 写成功时nonFatalDropCount清零
      if (nonFatalDropCount == 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        AVOX_FFMEPG_LOG(ret, "av_interleaved_write_frame non-fatal, drop frame",
                       " msg:", errBuf, " buffer size:", buffer.data.size,
                       " pts:", buffer.pts, " video:", bVideo, " audio:", bAudio);
      }
      nonFatalDropCount++;
    }
  } else {
    nonFatalDropCount = 0;
  }
}

void IOMuxerFF::onClose() {
  if (!fmtCtx) {
    return;
  }
  av_write_trailer(fmtCtx.get());
  fmtCtx.reset();
}

}
