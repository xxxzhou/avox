#include "FFVDecoder.hpp"

#include <libavutil/mastering_display_metadata.h>

#include "avox/codec/H26XHelper.hpp"
#include "avox/player/MediaPlayer.hpp"

#ifdef _WIN32
#include "avox_windows/WinCommon.hpp"
#endif

namespace avox {

FFVDecoder::FFVDecoder() { codecTH = VCodecTh::cpu; }

FFVDecoder::~FFVDecoder() { close(); }

bool FFVDecoder::onVaild() {
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
  if ((codecId == AV_CODEC_ID_H264 && configPackets.size() < 2) ||
      (codecId == AV_CODEC_ID_H265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  // 如果codecCtx已经存在,说明在重置
  if (codecCtx) {
    avcodec_flush_buffers(codecCtx.get());
    codecCtx.reset();
  }
  // d3d11va/vulkan这些，非独立解码器，需要借助通用解码器，然后硬件加速
  // 在这借onAttachContext生成对应的d3d11/vulkan硬件环境
  const AVCodec* codec = nullptr;
  if (codecDesc.bHardware) {
    codec = avcodec_find_decoder(codecId);
  } else {
    codec = avcodec_find_decoder_by_name(codecDesc.name.c_str());
  }
  if (!codec) {
    LOGFLF(LogLevel::error,
           "avcodec_find_decoder failed, bHardware:", codecDesc.bHardware,
           " codecId:", codecId, " codec:", codecDesc.name.c_str());
    return DecodeResult::openFailed;
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
    // H265解码，配置帧的格式最好与数据包格式一致
    if (bvcc) {
      if (codecId == AV_CODEC_ID_H264) {
        h264CombinExtradata(configPackets, extradata);
      } else {
        h265CombinExtradata(configPackets, extradata);
      }
    } else {
      annexbCombin(configPackets, extradata);
    }
    AvoxData extradataBuf = {extradata.data(), (int32_t)extradata.size(), true};
    log(LogLevel::info, "ffmpeg decoder extradata size:", extradataBuf);
    // extradata包含SPS,PPS等编码信息
    codecCtx->extradata_size = extradata.size();
    codecCtx->extradata =
        (uint8_t*)av_malloc(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(codecCtx->extradata, extradata.data(), extradata.size());
    // 别的一些设置
    codecCtx->thread_count = 1;
  } else if (!configPackets.empty()) {
    // VC-1/WMV3/RV30/RV40等: 无nalu结构, 容器extradata原样透传
    // (ASF的sequence header/RM的codec数据, 解码器初始化必需)
    std::vector<uint8_t> extradata;
    for (auto& p : configPackets) {
      extradata.insert(extradata.end(), p.buff.begin(),
                       p.buff.begin() + p.size);
    }
    codecCtx->extradata_size = (int32_t)extradata.size();
    codecCtx->extradata =
        (uint8_t*)av_malloc(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(codecCtx->extradata, extradata.data(), extradata.size());
  }
  int32_t ret = avcodec_open2(codecCtx.get(), codec, nullptr);
  AVOX_FFMEPG_LOG(ret, "avcodec_open2 failed");
  if (ret == 0 && codecCtx->hw_device_ctx) {
    LOGFLF(LogLevel::warn, "[hwfmt] negotiated pix_fmt:",
           (int32_t)codecCtx->pix_fmt);
  }
  if (ret < 0) {
    return DecodeResult::openFailed;
  }
  return DecodeResult::success;
}

// 参数集内容是否已在configPackets里(逐字节相同)
static bool hasSameConfig(const std::vector<PacketBuf>& configs,
                          const AvoxPacket& packet) {
  for (auto& buf : configs) {
    if (buf.size == packet.data.size && buf.size > 0 &&
        memcmp(buf.buff.data(), packet.data.data, buf.size) == 0) {
      return true;
    }
  }
  return false;
}

DecodeResult FFVDecoder::decode(const AvoxPacket& packet) {
  AVCodecID codecId = (AVCodecID)codecDesc.codecId;
  // 本次调用对应的参数集是否刚被 pushConfig 更新过(消费掉,只对当前包生效)
  bool bChanged = bConfigChanged;
  bConfigChanged = false;
  if ((codecId == AV_CODEC_ID_H264 && configPackets.size() < 2) ||
      (codecId == AV_CODEC_ID_H265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  if (!codecCtx) {
    // 无带外参数集容器(vp9/webm等): 首包建上下文后必须继续喂当前包,
    // 在此吞掉会丢起始关键帧 → 首个GOP报"Not all references"直到下一关键帧
    // (2026-09-18 实测 vp9 webm 首 GOP 正常出画, 本行为已验证)
    DecodeResult result = onPreDecoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 纯参数集包(SPS/PPS/VPS)无slice NAL, 喂avcodec会持续报"no frame!":
  // 内容与已存配置相同才跳过, 变化过的仍要喂, 让ffmpeg更新流内参数集
  // 注意: pushConfig 是就地覆盖 configPackets 的, 对刚更新的参数集来说
  // hasSameConfig 必然为真, 所以必须用 bChanged 把它排除掉, 否则seek跨段
  // (同分辨率但SPS/PPS不同)时新参数集永远喂不进解码器, 解码器会拿旧PPS
  // 解新段切片 -> 切片头字段乱值 / hardware accelerator failed to decode
  if (naluConfigFrame(this->codecId, getNalUnit(this->codecId, packet)) &&
      hasSameConfig(configPackets, packet) && !bChanged) {
    return DecodeResult::success;
  }
  // VC-1/WMV3/RV30/RV40等: extradata已按vconfig消费进codecCtx,
  // 原样再喂一次只会报无效帧
  if (codecId != AV_CODEC_ID_H264 && codecId != AV_CODEC_ID_H265 &&
      hasSameConfig(configPackets, packet)) {
    return DecodeResult::success;
  }
  // if(packet.frameType == 1){
  //   LOGFLF(LogLevel::info, "I pts:", packet.pts);
  // }
  // SEI 裸流兜底: ffmpeg>=9 不导出带内 MDCV/CLL, 从包内 SEI 自提
  scanHdrSei(packet);
  DecodeResult bRet = decodePacket(packet);
  return bRet;
}

void FFVDecoder::flush() { flushContext(); }

void FFVDecoder::onClose() {
  if (codecCtx) {
    codecCtx.reset();
  }
}

// 解析单条 HDR side data (ST2086 mastering / CLL) 进 meta
static void parseHdrSideData(AVFrameSideData* sd, HdrMeta& meta) {
  if (sd->type == AV_FRAME_DATA_MASTERING_DISPLAY_METADATA &&
      sd->size >= sizeof(AVMasteringDisplayMetadata)) {
    auto* md = (AVMasteringDisplayMetadata*)sd->data;
    if (md->has_luminance && md->max_luminance.den > 0) {
      meta.maxLuminance = (uint32_t)(av_q2d(md->max_luminance) + 0.5);
    }
    if (md->has_luminance && md->min_luminance.den > 0) {
      meta.minLuminance = (uint32_t)(av_q2d(md->min_luminance) + 0.5);
    }
    // 色度坐标按 0.00002 增量编码, 归一化到 0..1
    if (md->has_primaries) {
      for (int32_t g = 0; g < 3; ++g) {
        for (int32_t c = 0; c < 2; ++c) {
          if (md->display_primaries[g][c].den > 0) {
            meta.primaries[g * 2 + c] =
                (float)(av_q2d(md->display_primaries[g][c]) / 50000.0);
          }
        }
      }
      for (int32_t c = 0; c < 2; ++c) {
        if (md->white_point[c].den > 0) {
          meta.whitePoint[c] = (float)(av_q2d(md->white_point[c]) / 50000.0);
        }
      }
    }
    meta.valid = true;
  } else if (sd->type == AV_FRAME_DATA_CONTENT_LIGHT_LEVEL &&
             sd->size >= sizeof(AVContentLightMetadata)) {
    auto* cl = (AVContentLightMetadata*)sd->data;
    meta.maxCLL = cl->MaxCLL;
    meta.maxFALL = cl->MaxFALL;
    meta.valid = true;
  }
}

// SEI RBSP 反仿真(去 00 00 03 的 03)后提取 137(mdcv)/144(clli) 固定宽
// 载荷, 语义对齐 AVMasteringDisplayMetadata/AVContentLightMetadata
static void parseHdrSeiRbsp(const uint8_t* rbsp, int32_t size, HdrMeta& meta) {
  int32_t i = 0;
  auto rd16 = [&](int32_t o) -> uint16_t {
    return (uint16_t)((rbsp[i + o] << 8) | rbsp[i + o + 1]);
  };
  auto rd32 = [&](int32_t o) -> uint32_t {
    return ((uint32_t)rbsp[i + o] << 24) | ((uint32_t)rbsp[i + o + 1] << 16) |
           ((uint32_t)rbsp[i + o + 2] << 8) | rbsp[i + o + 3];
  };
  while (i + 2 <= size) {
    uint32_t type = 0, paySize = 0;
    while (i < size && rbsp[i] == 0xFF) {
      type += 255;
      i++;
    }
    if (i >= size) {
      break;
    }
    type += rbsp[i++];
    while (i < size && rbsp[i] == 0xFF) {
      paySize += 255;
      i++;
    }
    if (i >= size) {
      break;
    }
    paySize += rbsp[i++];
    if (i + (int32_t)paySize > size) {
      break;
    }
    if (type == 137 && paySize >= 24) {
      // 基色序 G,B,R, xy 各 16bit 按 1/50000; 亮度 32bit 按 1e-4 nits
      for (int32_t g = 0; g < 3; g++) {
        meta.primaries[g * 2] = rd16(g * 4) / 50000.0f;
        meta.primaries[g * 2 + 1] = rd16(g * 4 + 2) / 50000.0f;
      }
      meta.whitePoint[0] = rd16(12) / 50000.0f;
      meta.whitePoint[1] = rd16(14) / 50000.0f;
      meta.maxLuminance = rd32(16) / 10000;
      meta.minLuminance = rd32(20) / 10000;
      meta.valid = true;
    } else if (type == 144 && paySize >= 4) {
      meta.maxCLL = rd16(0);
      meta.maxFALL = rd16(2);
      meta.valid = true;
    }
    i += (int32_t)paySize;
  }
}

void FFVDecoder::scanHdrSei(const AvoxPacket& packet) {
  if (codecId != VCodecId::h264 && codecId != VCodecId::h265) {
    return;
  }
  std::vector<AvoxPacket> nalus;
  if (bvcc) {
    splitAvccNalu(packet, nalus);
  } else {
    splitAnnexbNalu(packet, nalus);
  }
  HdrMeta meta = {};
  for (auto& nalu : nalus) {
    const uint8_t* d = nalu.data.data;
    int32_t size = nalu.data.size;
    if (size <= nalu.prefixSize) {
      continue;
    }
    d += nalu.prefixSize;
    size -= nalu.prefixSize;
    bool bSei = false;
    int32_t hdrSize = 0;
    if (codecId == VCodecId::h265) {
      // (forbidden<<7 | type<<1 | layer高5位): PREFIX_SEI=39, SUFFIX=40
      bSei = size >= 2 && ((d[0] & 0x7E) >> 1) == 39;
      hdrSize = 2;
    } else {
      bSei = size >= 1 && (d[0] & 0x1F) == 6;
      hdrSize = 1;
    }
    if (!bSei) {
      continue;
    }
    // 反仿真进临时缓冲 (载荷很小, SEI NAL 罕见, 开销可忽略)
    seiRbsp.resize(size - hdrSize);
    int32_t out = 0;
    int32_t zeros = 0;
    for (int32_t k = hdrSize; k < size; k++) {
      uint8_t b = d[k];
      if (zeros >= 2 && b == 0x03) {
        zeros = 0;
        continue;
      }
      zeros = (b == 0) ? zeros + 1 : 0;
      seiRbsp[out++] = b;
    }
    parseHdrSeiRbsp(seiRbsp.data(), out, meta);
  }
  updateHdrMeta(meta);
}

void FFVDecoder::updateHdrMeta(const HdrMeta& meta) {
  if (!meta.valid) {
    return;
  }
  if (meta.maxCLL == hdrMeta.maxCLL &&
      meta.maxLuminance == hdrMeta.maxLuminance &&
      meta.maxFALL == hdrMeta.maxFALL) {
    return;
  }
  hdrMeta = meta;
  LOGFLF(LogLevel::info, "[hdrmeta] sei maxLum:", meta.maxLuminance,
         " cll:", meta.maxCLL, " fall:", meta.maxFALL);
  dispatch(&IVideoDecoderOb::onHdrMeta, meta);
}

void FFVDecoder::onFrame(AVFrame* avFrame, bool bDrop) {
  // E2E 诊断: HDR 元数据缺失的第一现场 (仅前 3 帧打印)
  static int32_t sideDataLogs = 0;
  if (sideDataLogs < 3) {
    sideDataLogs++;
    LOGFLF(LogLevel::info, "[hdrmeta] frame sd:", avFrame->nb_side_data,
           " ctx decoded sd:",
           codecCtx ? codecCtx->nb_decoded_side_data : -1,
           " ctx coded sd:", codecCtx ? codecCtx->nb_coded_side_data : -1);
  }
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
  // HDR 静态元数据: ST2086 + CLL, 有标记才解析, 峰值相关字段变化才回调。
  // ffmpeg>=7.1(avcodec>=62) 把 MDCV/CLL 归为静态元数据收进
  // codecCtx->decoded_side_data, 帧级 side_data 恒空; 旧版才挂 AVFrame。
  // 两处都查: 上下文有取上下文, 否则退回帧级。
  HdrMeta meta = {};
  bool fromCtx = false;
  if (codecCtx && codecCtx->nb_decoded_side_data > 0) {
    for (int32_t i = 0; i < codecCtx->nb_decoded_side_data; ++i) {
      parseHdrSideData(codecCtx->decoded_side_data[i], meta);
    }
    fromCtx = meta.valid;
  }
  if (!fromCtx) {
    for (int32_t i = 0; i < avFrame->nb_side_data; ++i) {
      parseHdrSideData(avFrame->side_data[i], meta);
    }
  }
  updateHdrMeta(meta);
  updateHdrMeta(meta);
  dispatch(&IVideoDecoderOb::onDecode, frame);
  // log(LogLevel::info, "ffmpeg decoder frame pts:", frame.pts);
  // if (avFrame->pict_type == AV_PICTURE_TYPE_I) {
  //   log(LogLevel::info, "I pts:", frame.pts);
  // }
  // log(LogLevel::info, "pts:", avFrame->pts, " width:", avFrame->width,
  //     " height:", avFrame->height,
  //     " yuvtype:", getYuvTypeStr(ffYuvType((AVPixelFormat)avFrame->format)));
}

void FFVDecoder::onError(int32_t ret) {
  if (ret == AVERROR_EOF) {
    dispatch(&IVideoDecoderOb::onVideoComplete);
  }
}

void FFVDecoder::onAttachContext() {}

void FFVDecoder::onDetachContext() {}

}
