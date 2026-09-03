#include "FFAEncoder.hpp"

#include "avox/player/Player.hpp"

namespace avox {

FFAEncoder::FFAEncoder() {}

FFAEncoder::~FFAEncoder() {}

AudioDesc FFAEncoder::getSupportDesc(const AudioDesc& desc) {
  if (enDesc.codecId == ACodecId::aac) {
    outDesc.format = AudioFormat::AVOX_AUDIO_FLTP;
  } else if (enDesc.codecId == ACodecId::g711a ||
             enDesc.codecId == ACodecId::g711u) {
    outDesc.format = AudioFormat::AVOX_AUDIO_S16;
    outDesc.sampleRate = 8000;
    outDesc.channels = 1;
  }
  return outDesc;
}

DecodeResult FFAEncoder::onPreEncoder() {
  AVCodecID vCodecId = getFFCodecId(enDesc.codecId);
  const AVCodec* codec = avcodec_find_encoder(vCodecId);
  if (!codec) {
    LOGFLF(LogLevel::info, "avcodec_find_encoder failed,codecId:",
           getACodecName(enDesc.codecId));
    return DecodeResult::openFailed;
  }
  for (const enum AVSampleFormat* p = codec->sample_fmts;
       *p != AV_SAMPLE_FMT_NONE; p++) {
    LOGFLF(LogLevel::info, "supported format: ", av_get_sample_fmt_name(*p));
  }
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  codecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
  codecCtx->profile = FF_PROFILE_AAC_LOW;
  codecCtx->time_base = {1, 1000};
  codecCtx->sample_rate = enDesc.desc.sampleRate;
  if (enDesc.codecId == ACodecId::aac) {
    codecCtx->frame_size = 1024;
  } else if (enDesc.codecId == ACodecId::g711a ||
             enDesc.codecId == ACodecId::g711u) {
    codecCtx->profile = FF_PROFILE_UNKNOWN;
    codecCtx->frame_size = 160;
  }
  av_channel_layout_default(&codecCtx->ch_layout, enDesc.desc.channels);
  codecCtx->sample_fmt = getFFAudioFormat(enDesc.desc.format);
  int32_t ret = avcodec_open2(codecCtx.get(), codec, nullptr);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
    return DecodeResult::openFailed;
  }
  if (enDesc.codecId == ACodecId::g711a || enDesc.codecId == ACodecId::g711u) {
    codecCtx->frame_size = 160;
  }
  if (codecCtx->frame_size <= 0) {
    codecCtx->frame_size = 1024;
  }
  int32_t frameSize = codecCtx->frame_size * enDesc.desc.channels *
                      audioFormatSize(enDesc.desc.format);
  if (frameSize <= 0) {
    LOGFLF(LogLevel::warn,
           "frameSize not valid,frameSize:", codecCtx->frame_size);
    return DecodeResult::dataNoReady;
  }
  LOGFLF(LogLevel::info, "codec:", getACodecName(enDesc.codecId),
         " frameSize:", frameSize);
  curFrame.setSize(frameSize);
  curFrame.setPts(AVOX_NOVALID_PTS);
  frame = getUniquePtr(av_frame_alloc());
  return DecodeResult::success;
}

DecodeResult FFAEncoder::encode(const AvoxAFrame& aframe) {
  if (!codecCtx || !frame) {
    DecodeResult initRet = onPreEncoder();
    if (initRet != DecodeResult::success) {
      return initRet;
    }
  }
  return fillFrame(aframe);
}

DecodeResult FFAEncoder::encode() {
  frame->nb_samples = codecCtx->frame_size;
  frame->format = codecCtx->sample_fmt;
  frame->sample_rate = codecCtx->sample_rate;
  frame->ch_layout = codecCtx->ch_layout;
  frame->pts = curFrame.getPts();
  // log(LogLevel::info, "encode audio pts:", frame->pts,
  //     " frame size:", codecCtx->frame_size);
  if (enDesc.desc.channels == 1) {
    frame->data[0] = curFrame.point();
    frame->linesize[0] = codecCtx->frame_size;
  } else if (enDesc.desc.channels == 2) {
    int32_t size = codecCtx->frame_size / 2;
    frame->data[0] = curFrame.point();
    frame->data[1] = curFrame.point() + size;
    frame->linesize[0] = size;
    frame->linesize[1] = size;
  }
  int ret = ret = avcodec_send_frame(codecCtx.get(), frame.get());
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      return DecodeResult::complete;
    }
    if (ret == AVERROR(EAGAIN)) {
      return DecodeResult::dataNoReady;
    } else {
      AVOX_FFMEPG_LOG(ret, "avcodec_send_frame failed");
      return DecodeResult::dataError;
    }
  }
  while (true) {
    AVPacket* packet = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx.get(), packet);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        break;
      } else {
        AVOX_FFMEPG_LOG(ret, "avcodec_receive_packet failed");
        return DecodeResult::dataError;
      }
    }
    // 时间戳转为毫秒 time_base本身是ms可以不需要
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::audio;
    // log(LogLevel::info, "onPacket pts:", cpacket.pts, " size:",
    // cpacket.data.size);
    dispatch(&IEncoderOb::onPacket, cpacket);
    av_packet_unref(packet);
  }
  return DecodeResult::success;
}

}