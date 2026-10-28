#include "FFAEncoder.hpp"

#include "avox/player/Player.hpp"

namespace avox {
namespace {

// libmp3lame 只有固定采样率档位, 其余采样率就近落到 44.1k/48k
int32_t mp3SampleRate(int32_t rate) {
  switch (rate) {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
      return rate;
    default:
      return rate > 44100 ? 48000 : 44100;
  }
}

}  // namespace

FFAEncoder::FFAEncoder() {}

FFAEncoder::~FFAEncoder() {}

AudioDesc FFAEncoder::getSupportDesc(const AudioDesc& desc) {
  if (enDesc.codecId == ACodecId::aac) {
    outDesc.format = AudioFormat::AVOX_AUDIO_FLTP;
    return outDesc;
  }
  if (enDesc.codecId == ACodecId::g711a ||
      enDesc.codecId == ACodecId::g711u) {
    outDesc.format = AudioFormat::AVOX_AUDIO_S16;
    outDesc.sampleRate = 8000;
    outDesc.channels = 1;
    return outDesc;
  }
  if (enDesc.codecId == ACodecId::mp3) {
    // libmp3lame 只收平面格式且最多双声道; 源是 6 声道 flt, 不下混就开不起来
    outDesc.format = AudioFormat::AVOX_AUDIO_FLTP;
    if (outDesc.channels > 2) {
      outDesc.channels = 2;
    }
    outDesc.sampleRate = mp3SampleRate(outDesc.sampleRate);
    return outDesc;
  }
  // 其余编码器的支持格式各不相同(源通常是 flt), 按编码器支持表落一个
  const AVCodec* codec = avcodec_find_encoder(getFFCodecId(enDesc.codecId));
  const enum AVSampleFormat* fmts = nullptr;
  int numFmts = 0;
  if (codec) {
    avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                 0, (const void**)&fmts, &numFmts);
  }
  if (fmts && numFmts > 0) {
    const AVSampleFormat want = getFFAudioFormat(outDesc.format);
    bool supported = false;
    for (int i = 0; i < numFmts; ++i) {
      if (fmts[i] == want) {
        supported = true;
        break;
      }
    }
    if (!supported) {
      AVSampleFormat pick = fmts[0];
      for (int i = 0; i < numFmts; ++i) {
        if (fmts[i] == AV_SAMPLE_FMT_FLTP) {
          pick = fmts[i];
          break;
        }
      }
      outDesc.format = ffAudioFromat(pick);
    }
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
  // 9.0: AVCodec::sample_fmts 已删除, 改用 avcodec_get_supported_config
  const enum AVSampleFormat* fmts = nullptr;
  int numFmts = 0;
  avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                               (const void**)&fmts, &numFmts);
  for (int i = 0; fmts && i < numFmts; i++) {
    LOGFLF(LogLevel::info, "supported format: ", av_get_sample_fmt_name(fmts[i]));
  }
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  codecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
  if (enDesc.codecId == ACodecId::aac) {
    codecCtx->profile = AV_PROFILE_AAC_LOW;
  }
  codecCtx->time_base = {1, 1000};
  codecCtx->sample_rate = enDesc.desc.sampleRate;
  if (enDesc.codecId == ACodecId::aac) {
    codecCtx->frame_size = 1024;
  } else if (enDesc.codecId == ACodecId::g711a ||
             enDesc.codecId == ACodecId::g711u) {
    codecCtx->profile = AV_PROFILE_UNKNOWN;
    codecCtx->frame_size = 160;
  }
  av_channel_layout_default(&codecCtx->ch_layout, enDesc.desc.channels);
  codecCtx->sample_fmt = getFFAudioFormat(enDesc.desc.format);
  // 音频旋钮: 码率只在显式/自动解析出值时才覆盖(0 = 保持 codec 缺省, 存量不变)
  const int32_t audioBitrate = getTargetBitrate();
  if (audioBitrate > 0) {
    codecCtx->bit_rate = audioBitrate;
  }
  // FFmpeg 原生 aac 不支持 VBR 质量档与 HE-AAC 编码: 设了明确告警, 不静默
  if (enDesc.codecId == ACodecId::aac &&
      getVbrQuality() >= 1) {
    LOGFLF(LogLevel::warn,
           "ffmpeg aac ignores vbrQuality, use bitrate instead; quality:",
           getVbrQuality());
  }
  if (enDesc.codecId == ACodecId::aac &&
      getProfileLevel() == AudioEncodeProfile::he) {
    LOGFLF(LogLevel::warn, "ffmpeg aac cannot encode HE-AAC, fallback LC");
  }
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
  // 帧缓冲布局: 平面格式 [ch][frame_size], 交织格式 [frame_size*ch]
  const int32_t channels = enDesc.desc.channels;
  const int32_t bytesPerSample = audioFormatSize(enDesc.desc.format);
  if (bAPlaneFormat(enDesc.desc.format)) {
    const int32_t planeBytes = codecCtx->frame_size * bytesPerSample;
    for (int32_t ch = 0; ch < AV_NUM_DATA_POINTERS; ++ch) {
      frame->data[ch] = ch < channels
                            ? curFrame.point() + (size_t)ch * planeBytes
                            : nullptr;
      frame->linesize[ch] = ch < channels ? planeBytes : 0;
    }
  } else {
    frame->data[0] = curFrame.point();
    frame->linesize[0] = codecCtx->frame_size * channels * bytesPerSample;
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