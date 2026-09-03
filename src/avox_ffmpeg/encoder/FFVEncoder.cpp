#include "FFVEncoder.hpp"

namespace avox {

struct HWEncoderInfo {
  AVHWDeviceType type;
  const char* h264;
  const char* h265;
};

FFVEncoder::FFVEncoder() {}

FFVEncoder::~FFVEncoder() {}

// avox 量程 -> FFmpeg, 与 shader 矩阵(VideoDesc.colorSpace)同源
static AVColorRange mapColorRange(YuvRange r) {
  return r == YuvRange::limited ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
}
// avox 规范 -> FFmpeg
static AVColorSpace mapColorSpace(YuvStandard s) {
  if (s == YuvStandard::bt709) {
    return AVCOL_SPC_BT709;
  }
  if (s == YuvStandard::bt2020) {
    return AVCOL_SPC_BT2020_NCL;
  }
  return AVCOL_SPC_SMPTE170M;  // BT601(FFmpeg 无 AVCOL_SPC_BT601,用 SMPTE170M)
}

DecodeResult FFVEncoder::onPreEncoder() {
  AVCodecID vCodecId = getFFCodecId(desc.codecId);
  const AVCodec* codec = avcodec_find_encoder(vCodecId);
#ifdef _WIN32
  // 如果是NV12格式，启用QSV硬编码
  if (yuvType == YuvType::nv12) {
    desc.desc.type = YuvType::nv12;
    const char* name = nullptr;
    // Intel QSV/AMD AMF/NVIDIA NVENC 硬编码支持的编码器名称
    static const HWEncoderInfo hw_encoders[] = {
        {AV_HWDEVICE_TYPE_QSV, "h264_qsv", "hevc_qsv"},
        {AV_HWDEVICE_TYPE_AMF, "h264_amf", "hevc_amf"},
        {AV_HWDEVICE_TYPE_CUDA, "h264_nvenc", "hevc_nvenc"}};
    for (const auto& enc : hw_encoders) {
      AVBufferRef* hw_device_ctx = nullptr;
      // 检查能否创建相应的硬件设备上下文，如果能创建则说明硬件编码支持
      if (av_hwdevice_ctx_create(&hw_device_ctx, enc.type, nullptr, nullptr,
                                 0) == 0) {
        av_buffer_unref(&hw_device_ctx);
        name = (desc.codecId == VCodecId::h265) ? enc.h265 : enc.h264;
        break;
      }
    }
    if (name) {
      codec = avcodec_find_encoder_by_name(name);
    } else {
      // 如果硬编码失败，则使用软件编码，但是软编码不一定支持NV12
      LOGFLF(LogLevel::warn,
             "frame is nv12,hardware encode not support "
             "qsv/amf,software encode may not support nv12");
    }
  }
#endif
  if (!codec) {
    LOGFLF(LogLevel::info,
           "avcodec_find_encoder failed,codecId:", getVCodecName(desc.codecId));
    return DecodeResult::openFailed;
  }
  FrameRate framerate = {30, 1};
  if (desc.desc.fps > 0) {
    framerate.build(desc.desc.fps);
  }
  codecCtx = getUniquePtr(avcodec_alloc_context3(codec));
  codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
  // desc.desc.type
  codecCtx->pix_fmt = getFFVideoFormat(desc.desc.type);
  // 颜色标签: 与 rgba2yuv shader 的输出(由 VideoDesc.colorSpace 驱动)对齐,
  // 否则 VUI unspecified, 播放器默认按 limited 解释 -> 饱和色裁切偏色
  codecCtx->color_range = mapColorRange(desc.desc.colorSpace.range);
  codecCtx->colorspace = mapColorSpace(desc.desc.colorSpace.standard);
  codecCtx->color_primaries = AVCOL_PRI_BT709;
  codecCtx->color_trc = AVCOL_TRC_BT709;
  codecCtx->width = desc.desc.width;
  codecCtx->height = desc.desc.height;
  YUVFormat yuvFormat = {desc.desc.width, desc.desc.height, desc.desc.type};
  codecCtx->frame_size = getYuvFrameSize(yuvFormat, 0);
  codecCtx->framerate = av_make_q(framerate.numerator, framerate.denominator);
  // 设置成毫秒，取当前时间戳,fps不对也没关系
  codecCtx->time_base = {1, 1000};
  // 4秒一个GOP
  codecCtx->gop_size = gop * framerate.asDecimal();
  // 禁用B帧
  codecCtx->max_b_frames = 0;
  codecCtx->has_b_frames = 0;
  // 根据长宽计算一个合理的bitrate
  int bitrate = desc.desc.width * desc.desc.height * framerate.asDecimal() * 3;
  codecCtx->bit_rate = bitrate;
  codecCtx->thread_count = 0;
  codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  AVDictionary* param = nullptr;
  if (desc.codecId == VCodecId::h264) {
    av_dict_set(&param, "profile", "main", 0);
  } else if (desc.codecId == VCodecId::h265) {
    av_dict_set(&param, "profile", "main", 0);
  }
  // 不同编码器 preset/码控词汇不同,按实际选中的编码器分发
  const char* codecName = codec->name ? codec->name : "";
  if (strstr(codecName, "libx264") || strstr(codecName, "libx265")) {
    // x264/x265 软编码: 用 CRF 模式 + ultrafast preset
    av_dict_set(&param, "preset", "ultrafast", 0);
    av_dict_set(&param, "crf", "23", 0);
    codecCtx->bit_rate = 0;
  } else if (strstr(codecName, "_qsv")) {
    // Intel QSV: preset 用 veryfast,保持 bit_rate
    av_dict_set(&param, "preset", "veryfast", 0);
  } else if (strstr(codecName, "_nvenc")) {
    // NVIDIA NVENC: p1(最快) ~ p7(最慢)
    av_dict_set(&param, "preset", "p1", 0);
    av_dict_set(&param, "tune", "ll", 0);
  } else if (strstr(codecName, "_amf")) {
    // AMD AMF: 用 quality=speed
    av_dict_set(&param, "quality", "speed", 0);
  }
  // 子类硬编码
  onAttachContext();
  // 打开编码器
  int32_t ret = avcodec_open2(codecCtx.get(), codec, &param);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
    return DecodeResult::openFailed;
  }
  frame = getUniquePtr(av_frame_alloc());
  frameCount = 0;
  return DecodeResult::success;
}

DecodeResult FFVEncoder::encode(const YUVFrame& yframe) {
  // 检查分辨率是否变化,如果变化,编码器重置
  bool bChange = desc.desc.width != yframe.format.width ||
                 desc.desc.height != yframe.format.height;
  if (!codecCtx || !frame || bChange) {
    if (bChange) {
      LOGFLF(LogLevel::info, "old size width:", desc.desc.width,
             " height:", desc.desc.height,
             " change size width:", yframe.format.width,
             " height:", yframe.format.height);
    }
    yuvType = yframe.format.type;
    desc.desc.width = yframe.format.width;
    desc.desc.height = yframe.format.height;
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
    // 分析配置帧，并发送配置信息
  }
  frame->format = codecCtx->pix_fmt;
  frame->width = yframe.format.width;
  frame->height = yframe.format.height;
  frame->data[0] = yframe.data[0];
  frame->data[1] = yframe.data[1];
  frame->data[2] = yframe.data[2];
  frame->linesize[0] = yframe.stride[0];
  frame->linesize[1] = yframe.stride[1];
  frame->linesize[2] = yframe.stride[2];
  // frame->pts = frameCount++;
  frame->pts = yframe.pts;
  // LOGFLF(LogLevel::info, "encode video pts:", frame->pts);
  int32_t ret = avcodec_send_frame(codecCtx.get(), frame.get());
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
    // android平台发现有时候会返回空包
    if (packet->size <= 4) {
      av_packet_unref(packet);
      continue;
    }
    // 时间戳转为毫秒 time_base本身是ms可以不需要
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::video;
    // AvoxData tempData = {cpacket.data.data, std::min(cpacket.data.size, 200),
    //                     true};
    // log(LogLevel::info, "onPacket pts:", cpacket.pts,
    //     " size:", cpacket.data.size, " data:", tempData);
    dispatch(&IEncoderOb::onPacket, cpacket);
    av_packet_unref(packet);
  }
  return DecodeResult::success;
}

void FFVEncoder::flush() {
  if (codecCtx) {
    avcodec_flush_buffers(codecCtx.get());
  }
}

}