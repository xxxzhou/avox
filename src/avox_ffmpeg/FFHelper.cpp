#include "FFHelper.hpp"

#include "FFExport.h"
#include "avox/module/AvoxManager.hpp"
#include "decoder/FFADecoder.hpp"
#include "decoder/FFVDecoder.hpp"
#include "decoder/FFVkDecoder.hpp"
#include "encoder/FFAEncoder.hpp"
#include "encoder/FFVEncoder.hpp"

namespace avox {

// 注册FFmpeg里的解码器
void regFFCodec() {
  RegFunc ffCodecReg = {
      "ffmpeg codec regedit", []() {
        avformat_network_init();
        void* codec_iterator = nullptr;
        const AVCodec* codec;
        // 遍历所有解码器
        while ((codec = av_codec_iterate(&codec_iterator))) {
          // log(LogLevel::info, "found codec:", codec->name, " type:",
          // codec->type);
          if (av_codec_is_decoder(codec)) {
            if (codec->type == AVMEDIA_TYPE_VIDEO &&
                ffVCodec(codec->id) != VCodecId::none) {
              // 注册解码器
              VCodecDesc vdesc = {};
              vdesc.bHardware = avcodec_get_hw_config(codec, 0) != nullptr;
              vdesc.name = codec->name;
              vdesc.codecId = codec->id;
              vdesc.vcodecId = ffVCodec(codec->id);
              vdesc.desc = "ffmpeg_" + std::string(codec->long_name);
              AvoxManager::Get().vDecoders.regInitFunc(
                  ffVCodec(codec->id), vdesc,
                  [codec]() -> VideoDecoder* { return new FFVDecoder(); });
              // log(LogLevel::info, "decoder video:", codec->name,
              //     " codecId:", codec->id, " hardwrar:", vdesc.bHardware);
            }
            if (codec->type == AVMEDIA_TYPE_AUDIO &&
                ffACodec(codec->id) != ACodecId::none) {
              ACodecDesc adesc = {};
              adesc.codecId = codec->id;
              adesc.acodecId = ffACodec(codec->id);
              adesc.bHardware = codec->capabilities & AV_CODEC_CAP_HARDWARE;
              adesc.name = "ffmpeg_" + std::string(codec->name);
              AvoxManager::Get().aDecoders.regInitFunc(
                  ffACodec(codec->id), adesc,
                  []() -> AudioDecoder* { return new FFADecoder(); });
              // log(LogLevel::info, "decoder audio:", codec->name,
              //     " codecId:", codec->id);
            }
          }
          if (av_codec_is_encoder(codec)) {
            if (codec->type == AVMEDIA_TYPE_VIDEO &&
                ffVCodec(codec->id) != VCodecId::none) {
              VCodecDesc vdesc = {};
              vdesc.bHardware = avcodec_get_hw_config(codec, 0) != nullptr;
              vdesc.name = codec->name;
              vdesc.codecId = codec->id;
              vdesc.vcodecId = ffVCodec(codec->id);
              vdesc.desc = "ffmpeg_" + std::string(codec->long_name);
              AvoxManager::Get().vEncoders.regInitFunc(
                  ffVCodec(codec->id), vdesc,
                  [codec]() -> VideoEncoder* { return new FFVEncoder(); });
              //  log(LogLevel::info, "encoder video:", codec->name,
              //      " codecId:", codec->id, " hardwrar:", vdesc.bHardware);
            }
            if (codec->type == AVMEDIA_TYPE_AUDIO &&
                ffACodec(codec->id) != ACodecId::none) {
              ACodecDesc adesc = {};
              adesc.codecId = codec->id;
              adesc.acodecId = ffACodec(codec->id);
              adesc.bHardware = codec->capabilities & AV_CODEC_CAP_HARDWARE;
              adesc.name = "ffmpeg_" + std::string(codec->name);
              AvoxManager::Get().aEncoders.regInitFunc(
                  ffACodec(codec->id), adesc,
                  []() -> AudioEncoder* { return new FFAEncoder(); });
              // log(LogLevel::info, "encoder audio:", codec->name,
              //     " codecId:", codec->id);
            }
          }
        }
      }};
  AvoxManager::Get().initFuncs.push_back(ffCodecReg);
}

AVError ffIoError(int err) {
  switch (err) {
    case AVERROR_EOF:
      return AVError::endOfFile;
    case AVERROR(ETIMEDOUT):
      return AVError::netTimeout;
    case AVERROR(ECONNREFUSED):
    case AVERROR(ECONNRESET):
      return AVError::netShutdown;
    case AVERROR(ENOSYS):
      return AVError::urlNoSupport;
    case AVERROR(EIO):
      return AVError::deviceError;
    case AVERROR(EAGAIN):
      return AVError::dataNoVaild; 
    default:
      return AVError::other;
  }
  return AVError::other;
}

VCodecId ffVCodec(AVCodecID codecId) {
  switch (codecId) {
    case AV_CODEC_ID_H264:
      return VCodecId::h264;
    case AV_CODEC_ID_H265:
      return VCodecId::h265;
    default:
      return VCodecId::none;
  }
}

ACodecId ffACodec(AVCodecID codecId) {
  switch (codecId) {
    case AV_CODEC_ID_AAC:
      return ACodecId::aac;
    case AV_CODEC_ID_PCM_ALAW:
      return ACodecId::g711a;
    case AV_CODEC_ID_PCM_MULAW:
      return ACodecId::g711u;
    case AV_CODEC_ID_OPUS:
      return ACodecId::opus;
    case AV_CODEC_ID_PCM_S16LE:
      return ACodecId::pcms16le;
    case AV_CODEC_ID_PCM_S24LE:
      return ACodecId::pcms24le;
    case AV_CODEC_ID_MP3:
      return ACodecId::mp3;
    case AV_CODEC_ID_AC3:
      return ACodecId::ac3;
    default:
      return ACodecId::none;
  }
}

AVCodecID getFFCodecId(VCodecId codecId) {
  switch (codecId) {
    case VCodecId::h264:
      return AV_CODEC_ID_H264;
    case VCodecId::h265:
      return AV_CODEC_ID_H265;
    default:
      return AV_CODEC_ID_NONE;
  }
}
AVCodecID getFFCodecId(ACodecId codecId) {
  switch (codecId) {
    case ACodecId::aac:
      return AV_CODEC_ID_AAC;
    case ACodecId::g711a:
      return AV_CODEC_ID_PCM_ALAW;
    case ACodecId::g711u:
      return AV_CODEC_ID_PCM_MULAW;
    case ACodecId::opus:
      return AV_CODEC_ID_OPUS;
    case ACodecId::pcms16le:
      return AV_CODEC_ID_PCM_S16LE;
    case ACodecId::pcms24le:
      return AV_CODEC_ID_PCM_S24LE;
    case ACodecId::mp3:
      return AV_CODEC_ID_MP3;
    case ACodecId::ac3:
      return AV_CODEC_ID_AC3;
    default:
      return AV_CODEC_ID_NONE;
  }
}

YuvType ffYuvType(AVPixelFormat format) {
  switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return YuvType::yuv420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
      return YuvType::yuv422P;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
      return YuvType::yuv444P;
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV420P10BE:
      return YuvType::yuv420P10;
    case AV_PIX_FMT_NV12:
      return YuvType::nv12;
  }
  return YuvType::other;
}

ColorSpaceDesc ffColorSpace(AVCodecParameters* par) {
  ColorSpaceDesc cs = {};
  // 矩阵标准: VUI/容器标记优先, 未标记按高清/标清惯例
  switch (par->color_space) {
    case AVCOL_SPC_BT709:
      cs.standard = YuvStandard::bt709;
      break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      cs.standard = YuvStandard::bt2020;
      break;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_CHROMA_DERIVED_NCL:
    case AVCOL_SPC_CHROMA_DERIVED_CL:
      cs.standard = YuvStandard::bt601;
      break;
    default:
      cs.standard = par->height >= 720 ? YuvStandard::bt709 : YuvStandard::bt601;
      break;
  }
  // 量程: MPEG 系为 16~235; 未标记时 H264/H265/MPEG 家族按 VUI 默认 limited
  switch (par->color_range) {
    case AVCOL_RANGE_MPEG:
      cs.range = YuvRange::limited;
      break;
    case AVCOL_RANGE_JPEG:
      cs.range = YuvRange::full;
      break;
    default:
      cs.range = (par->codec_id == AV_CODEC_ID_H264 ||
                  par->codec_id == AV_CODEC_ID_HEVC ||
                  par->codec_id == AV_CODEC_ID_MPEG2VIDEO ||
                  par->codec_id == AV_CODEC_ID_MPEG4)
                     ? YuvRange::limited
                     : YuvRange::full;
      break;
  }
  return cs;
}

AudioFormat ffAudioFromat(int32_t audioFormat) {
  switch (audioFormat) {
    case AV_SAMPLE_FMT_U8:
      return AudioFormat::AVOX_AUDIO_U8;
    case AV_SAMPLE_FMT_S16:
      return AudioFormat::AVOX_AUDIO_S16;
    case AV_SAMPLE_FMT_S32:
      return AudioFormat::AVOX_AUDIO_S32;
    case AV_SAMPLE_FMT_FLT:
      return AudioFormat::AVOX_AUDIO_FLT;
    case AV_SAMPLE_FMT_DBL:
      return AudioFormat::AVOX_AUDIO_DBL;
    case AV_SAMPLE_FMT_S64:
      return AudioFormat::AVOX_AUDIO_S64;
    case AV_SAMPLE_FMT_U8P:
      return AudioFormat::AVOX_AUDIO_U8P;
    case AV_SAMPLE_FMT_S16P:
      return AudioFormat::AVOX_AUDIO_S16P;
    case AV_SAMPLE_FMT_S32P:
      return AudioFormat::AVOX_AUDIO_S32P;
    case AV_SAMPLE_FMT_FLTP:
      return AudioFormat::AVOX_AUDIO_FLTP;
    case AV_SAMPLE_FMT_DBLP:
      return AudioFormat::AVOX_AUDIO_DBLP;
    case AV_SAMPLE_FMT_S64P:
      return AudioFormat::AVOX_AUDIO_S64P;
  }
  return AudioFormat::other;
}

AvoxPacket ffAvoxPacket(AVPacket* ffpacket) {
  AvoxPacket packet = {};
  packet.index = ffpacket->stream_index;
  packet.pts = ffpacket->pts;
  packet.dts = ffpacket->dts;
  packet.duration = ffpacket->duration;
  packet.frameType = ffpacket->flags & AV_PKT_FLAG_KEY;
  // 数据,在进入队列前,引用都在,所以不用复制
  packet.data.bRef = true;
  packet.data.size = ffpacket->size;
  packet.data.data = ffpacket->data;
  return packet;
}

AVPixelFormat getFFVideoFormat(YuvType type) {
  switch (type) {
    case YuvType::yuv420P:
      return AV_PIX_FMT_YUV420P;
    case YuvType::yuv422P:
      return AV_PIX_FMT_YUV422P;
    case YuvType::yuv444P:
      return AV_PIX_FMT_YUV444P;
    case YuvType::uyvy422_10B:
      return AV_PIX_FMT_YUV422P10LE;
    case YuvType::yuv420P10:
      return AV_PIX_FMT_YUV420P10LE;
    case YuvType::nv12:
      return AV_PIX_FMT_NV12;
    default:
      return AV_PIX_FMT_NONE;
  }
}

AVSampleFormat getFFAudioFormat(AudioFormat format) {
  switch (format) {
    case AudioFormat::AVOX_AUDIO_U8:
      return AV_SAMPLE_FMT_U8;
    case AudioFormat::AVOX_AUDIO_S16:
      return AV_SAMPLE_FMT_S16;
    case AudioFormat::AVOX_AUDIO_S32:
      return AV_SAMPLE_FMT_S32;
    case AudioFormat::AVOX_AUDIO_FLT:
      return AV_SAMPLE_FMT_FLT;
    case AudioFormat::AVOX_AUDIO_DBL:
      return AV_SAMPLE_FMT_DBL;
    case AudioFormat::AVOX_AUDIO_S64:
      return AV_SAMPLE_FMT_S64;
    case AudioFormat::AVOX_AUDIO_U8P:
      return AV_SAMPLE_FMT_U8P;
    case AudioFormat::AVOX_AUDIO_S16P:
      return AV_SAMPLE_FMT_S16P;
    case AudioFormat::AVOX_AUDIO_S32P:
      return AV_SAMPLE_FMT_S32P;
    case AudioFormat::AVOX_AUDIO_FLTP:
      return AV_SAMPLE_FMT_FLTP;
    case AudioFormat::AVOX_AUDIO_DBLP:
      return AV_SAMPLE_FMT_DBLP;
    case AudioFormat::AVOX_AUDIO_S64P:
      return AV_SAMPLE_FMT_S64P;
    case AudioFormat::other:
    default:
      return AV_SAMPLE_FMT_NONE;  // 表示未找到匹配的 FFmpeg 音频格式
  }
}

void ffAudioFrame(AvoxAFrame& frame, AVFrame* avFrame) {
  AudioFormat format = ffAudioFromat(avFrame->format);
  bool bPlanar = bAPlaneFormat(format);
  int32_t planeSize = avFrame->linesize[0];
  int32_t channelSize = avFrame->ch_layout.nb_channels;
  int32_t dataSize = planeSize;
  // 如果是平面格式
  if (bPlanar && channelSize > 1) {
    dataSize = planeSize * channelSize;
  }
  // 数据变为交叉格式,方便按时间统一处理
}

std::string getUrlProtocol(const char* url_) {
  std::string url = url_;
  if (url.find("rtsp://") == 0) {
    return "rtsp";
  } else if (url.find("rtmp://") == 0) {
    return "flv";
  } else if (url.find("http://") == 0 || url.find("https://") == 0) {
    // 对于HTTP/HTTPS，可以进一步检查URL后缀或内容类型
    // 这里简单返回"flv"作为默认值，实际应用中可能需要更复杂的判断
    return "flv";
  } else if (url.find("m3u8") != std::string::npos) {
    return "hls";
  }
  return "";
}

}
