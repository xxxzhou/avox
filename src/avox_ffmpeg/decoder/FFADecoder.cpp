#include "FFADecoder.hpp"

#include "avox/player/MediaPlayer.hpp"

namespace avox {

FFADecoder::FFADecoder() {}

FFADecoder::~FFADecoder() { close(); }

bool FFADecoder::onVaild() {
  int32_t ret = 0;
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  return true;
}

namespace {

// 这些编码的容器extradata是解码器初始化必需
// (asf的wma格式块/rm的cook、atrac3描述), 没有就等配置帧而不是裸开
bool needExtradata(AVCodecID codecId) {
  return codecId == AV_CODEC_ID_WMAV1 || codecId == AV_CODEC_ID_WMAV2 ||
         codecId == AV_CODEC_ID_WMAPRO || codecId == AV_CODEC_ID_COOK ||
         codecId == AV_CODEC_ID_ATRAC3;
}

}  // namespace

DecodeResult FFADecoder::onPreDecoder() {
  // 等待配置帧来
  AVCodecID codecId = (AVCodecID)codecDesc.codecId;
  // aac需要配置帧填充extradata
  if ((codecId == AV_CODEC_ID_AAC || needExtradata(codecId)) && !confPkt) {
    return DecodeResult::noConfig;
  }
  auto codec = avcodec_find_decoder(codecId);
  // av_parser_init(codecId);
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  // 填充音频解码信息
  AVChannelLayout in_ch_layout = {};
  av_channel_layout_default(&in_ch_layout, outDesc.channels);
  codecCtx->sample_rate = outDesc.sampleRate;
  codecCtx->ch_layout = in_ch_layout;
  codecCtx->sample_fmt = (AVSampleFormat)getFFAudioFormat(outDesc.format);
  // 容器块对齐: wma1/2要求非0, cook靠它解析extradata子包(为0报PatchWELCOME)
  codecCtx->block_align = outDesc.blockAlign;
  if (codecId == AV_CODEC_ID_AAC) {
    AacSC aacsc = {};
    splitAAConfig(*confPkt, aacsc, bAdts);
    if (bAdts) {
      // 配置帧是ADTS头(ZLMediaKit直播流)：从解析结果构造2字节AudioSpecificConfig
      uint8_t asc[2] = {};
      asc[0] = (aacsc.objectType << 3) | (aacsc.confSampleIndex >> 1);
      asc[1] = ((aacsc.confSampleIndex & 0x01) << 7) | (aacsc.confChannel << 3);
      codecCtx->extradata_size = 2;
      codecCtx->extradata =
          (uint8_t*)av_malloc(2 + AV_INPUT_BUFFER_PADDING_SIZE);
      memcpy(codecCtx->extradata, asc, 2);
    } else {
      // 配置帧是ASC(本地流/FFmpeg/WebRTC)
      codecCtx->extradata_size = confPkt->size;
      codecCtx->extradata =
          (uint8_t*)av_malloc(confPkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
      memcpy(codecCtx->extradata, confPkt->buff.data(), confPkt->size);
    }
  } else if (needExtradata(codecId) && confPkt) {
    // wma/cook/atrac3: 容器extradata原样透传(无解析, 解码器按声明格式读)
    codecCtx->extradata_size = confPkt->size;
    codecCtx->extradata =
        (uint8_t*)av_malloc(confPkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(codecCtx->extradata, confPkt->buff.data(), confPkt->size);
  }
  int32_t ret = avcodec_open2(codecCtx.get(), codec, nullptr);
  AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
  if (ret < 0) {
    return DecodeResult::openFailed;
  }
  // 如果输入是平面格式，输出转成非平面格式
  if (bAPlaneFormat(outDesc.format)) {
    outDesc.format = nPlaneFormat(outDesc.format);
  }
  dispatch(&IAudioDecoderOb::onAudioDesc);
  return DecodeResult::success;
}

DecodeResult FFADecoder::decode(const AvoxPacket& packet) {
  AVCodecID codecId = (AVCodecID)codecDesc.codecId;
  if ((codecId == AV_CODEC_ID_AAC || needExtradata(codecId)) && !confPkt) {
    return DecodeResult::noConfig;
  }
  if (!codecCtx) {
    return onPreDecoder();
  }
  // ADTS模式：ZLMediaKit直播流数据带ADTS头，但FFmpeg AAC解码器配置了extradata后
  // 期望收到不带ADTS头的裸AAC数据，需要去掉ADTS头
  if (bAdts && codecId == AV_CODEC_ID_AAC) {
    auto* data = packet.data.data;
    auto size = packet.data.size;
    if (bAdtsHeader(data, size)) {
      // ADTS头长度：protection_absent为1时7字节，为0时9字节(含2字节CRC)
      int32_t adtsLen = (data[1] & 0x01) ? 7 : 9;
      AvoxPacket strippedPkt = packet;
      strippedPkt.data.data = data + adtsLen;
      strippedPkt.data.size = size - adtsLen;
      return decodePacket(strippedPkt);
    }
  }
  DecodeResult bRet = decodePacket(packet);
  return bRet;
}

void FFADecoder::flush() { flushContext(); }

void FFADecoder::onClose() {
  if (codecCtx) {
    codecCtx.reset();
  }
}

void FFADecoder::onFrame(AVFrame* avFrame, bool bDrop) {
  AudioFormat format = ffAudioFromat(avFrame->format);
  bool resetData = bAPlaneFormat(format);
  int32_t channelSize = avFrame->ch_layout.nb_channels;
  int32_t elementSize = audioFormatSize(format);
  int32_t dataSize = avFrame->nb_samples * elementSize * channelSize;
  if (resetData && channelSize <= 1) {
    resetData = false;
  }
  // 如果是平面格式，转成非平面格式
  if (resetData) {
    planeAudio.resize(dataSize);
    uint8_t* dst = planeAudio.data();
    for (int32_t sample = 0; sample < avFrame->nb_samples; ++sample) {
      for (int32_t ch = 0; ch < channelSize; ++ch) {
        uint8_t* src = avFrame->data[ch] + sample * elementSize;
        memcpy(dst, src, elementSize);
        dst += elementSize;
      }
    }
  }
  AvoxAFrame aframe = {};
  aframe.pts = avFrame->best_effort_timestamp;
  // 转成非平面格式（需要调用avcodec_fill_audio_frame
  aframe.buffer.data = resetData ? planeAudio.data() : avFrame->data[0];
  aframe.buffer.size = dataSize;
  aframe.buffer.bRef = true;
  dispatch(&IAudioDecoderOb::onDecode, aframe);
  //   log(LogLevel::info, "pts:", avFrame->pts,
  //       " sampleRate:", avFrame->sample_rate,
  //       " channels:", avFrame->ch_layout.nb_channels,
  //       " format:", getAudioFormatName(ffAudioFromat(avFrame->format)));
}

void FFADecoder::onError(int32_t ret) {}

}
