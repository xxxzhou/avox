#include "VideoDecoder.hpp"

#include "../player/MediaPlayer.hpp"
#include "../player/VideoTrack.hpp"

namespace avox {

VideoDecoder::VideoDecoder() {
  h264Parse = std::make_unique<H264Parse>();
  h265Parse = std::make_unique<H265Parse>();
}

bool VideoDecoder::setContext(const VCodecDesc& vcodecDesc,
                              const VideoDesc& srcDesc_) {
  codecDesc = vcodecDesc;
  srcDesc = srcDesc_;
  codecId = codecDesc.vcodecId;
  bCheckAcc = false;
  // 看看是否有必要启动线程
  return onVaild();
}

ConfigAddType VideoDecoder::pushConfig(const AvoxPacket& data) {
  PacketBuf buf(data);
  return pushConfig(buf);
}

ConfigAddType VideoDecoder::pushConfig(const PacketBuf& data) {
  ConfigAddType ctype =
      addConfigPacket(configPackets, data, codecDesc.vcodecId);
  // 编码器确定进来的包是avcc/annexb,具体编码器需要不同格式
  if (!bCheckAcc) {
    bvcc = checkAvccPacket(data.buff.data(), data.size);
    LOGFLF(LogLevel::info, "bvcc:", bvcc);
    bCheckAcc = true;
  }
  if (ctype == ConfigAddType::update || ctype == ConfigAddType::updateSize) {
    LOGFLF(LogLevel::info, "update video config");
    // 检查是否引起大小变化
    int32_t twidth = params.width;
    int32_t theight = params.height;
    parseConfigs();
    if (twidth != params.width || theight != params.height) {
      LOGFLF(LogLevel::info, "video size changed:", params.width, "x",
             params.height, " old size:", twidth, "x", theight);
      ctype = ConfigAddType::updateSize;
    }
  }
  // 配置帧发生了实质变化: 该参数集对解码器而言是新的,必须喂进去。
  // 注意此时 configPackets 已被覆盖,解码器侧再做"内容是否相同"的比对
  // 只会得到"相同"的结论,所以这里要单独记下,否则新参数集永远进不了解码器。
  bConfigChanged = ctype == ConfigAddType::add ||
                   ctype == ConfigAddType::update ||
                   ctype == ConfigAddType::updateSize;
  return ctype;
}

bool VideoDecoder::parseConfigs() {
  VCodecId codecId = codecDesc.vcodecId;
  params.width = srcDesc.width;
  params.height = srcDesc.height;
  params.yuvType = srcDesc.type;
  params.fps = srcDesc.fps;
  if (codecId != VCodecId::h264 && codecId != VCodecId::h265) {
    // wmv3/vc1/rv/mpeg4等无参数集可解析: 直接采信容器侧srcDesc宽高
    // (IOParse 层已从 codecpar 填充), 否则codecCtx尺寸为0解码全拒
    return params.width > 0 && params.height > 0;
  }
  if (codecId == VCodecId::h264) {
    if (configPackets.size() < 2) {
      return false;
    }
    for (auto& buf : configPackets) {
      if (!h264Parse->parse(buf.buff.data(), buf.size)) {
        AvoxPacket packet = getPacket(buf);
        uint8_t nalu = getH264NalUnit(packet.data.data + packet.prefixSize);
        LOGFLF(LogLevel::warn,
               "parse h264 config fail,config:", getNalName(codecId, nalu),
               " data:", packet.data);
        return false;
      }
    }
    H264ContextPtr h264Context = h264Parse->h264Contex;
    H264SpsPtr sps = h264Context->sps;
    H264PpsPtr pps = h264Context->pps;
    if (pps && sps) {
      params.yuvType = getYuvType(sps->chroma_format_idc);
      vec2i size = getSize(*sps);
      params.width = size.x;
      params.height = size.y;
      float fps = getFpsFromSps(*sps);
      if (fps > 0) {
        params.fps = fps;
      }
      params.yBitDepth = sps->bit_depth_luma_minus8 + 8;
      params.uvBitDepth = sps->bit_depth_chroma_minus8 + 8;
      return true;
    }
  } else if (codecId == VCodecId::h265) {
    if (configPackets.size() < 3) {
      return false;
    }
    for (auto& buf : configPackets) {
      if (!h265Parse->parse(buf.buff.data(), buf.size)) {
        AvoxPacket packet = getPacket(buf);
        uint8_t nalu = getH265NalUnit(packet.data.data + packet.prefixSize);
        LOGFLF(LogLevel::warn,
               "parse h265 config fail,config:", getNalName(codecId, nalu),
               " data:", packet.data);
        return false;
      }
    }
    H265ContextPtr h265Context = h265Parse->h265Contex;
    H265VpsPtr vps = h265Context->vpsSet[0];
    H265SpsPtr sps = h265Context->spsSet[0];
    H265PpsPtr pps = h265Context->ppsSet[0];
    if (vps && sps && pps) {
      // 从SPS获取分辨率信息
      params.width = sps->pic_width_in_luma_samples;
      params.height = sps->pic_height_in_luma_samples;
      params.yuvType = getYuvType(sps->chroma_format_idc);
      // 位深处理
      params.yBitDepth = sps->bit_depth_luma_minus8 + 8;
      params.uvBitDepth = sps->bit_depth_chroma_minus8 + 8;
      // 从VUI参数获取帧率
      if (sps->vui_parameters_present_flag &&
          sps->vui.vui_timing_info_present_flag) {
        float fps = (float)sps->vui.vui_time_scale /
                    (float)sps->vui.vui_num_units_in_tick;
        if (fps > 0) {
          params.fps = fps;
        }
      }
      return true;
    }
  }
  return false;
}

void VideoDecoder::copyConfigs(std::vector<PacketBuf>& configs) {
  // 深拷贝配置数据
  configs.clear();
  for (int32_t i = 0; i < configPackets.size(); ++i) {
    configs.push_back(configPackets[i]);
  }
}

DecodeResult VideoDecoder::decoder(avox::PacketBufPtr packet) {
  AvoxPacket vdata = {};
  vdata.data = {packet->buff.data(), packet->size, true};
  vdata.pts = packet->pts;
  vdata.dts = packet->dts;
  vdata.prefixSize = packet->prefixSize;
  vdata.frameType = packet->frameType;
  return decoderImp(vdata);
}

DecodeResult VideoDecoder::decoderImp(AvoxPacket& vdata) {
  // ffmpeg只要extradata与包格式对应就行,不需要转vcc/annexb
  // MAC平台原生解码需要avcc/hvcc,如果是annexb,需要转换
  if (bMustVcc && !bvcc) {
    splitAnnexbNalu(vdata, spiltBufs);
    if (spiltBufs.size() > 0) {
      // LOGFLF(LogLevel::info,"spilt buf:",spiltBufs.size());
      // 先检查里面是否有annexb3
      int32_t annexb3Count = 0;
      for (auto& buf : spiltBufs) {
        if (buf.prefixSize == 3) {
          annexb3Count++;
        }
      }
      // 如果有annexb3,转成avcc/hvcc需要重新分配内存
      if (annexb3Count > 0) {
        annexbBufs.resize(vdata.data.size + annexb3Count);
        int32_t index = 0;
        for (auto& buf : spiltBufs) {
          int32_t bAnnexB3 = buf.prefixSize == 3 ? 1 : 0;
          memcpy(annexbBufs.data() + index + bAnnexB3, buf.data.data,
                 buf.data.size);
          if (bAnnexB3) {
            annexbBufs[index] = 0;
          }
          buf.data = {annexbBufs.data() + index, buf.data.size + bAnnexB3,
                      true};
          buf.prefixSize = 4;
          index += buf.data.size + bAnnexB3;
        }
        vdata.data = {annexbBufs.data(), (int32_t)annexbBufs.size(), true};
      }
      // 分段的annexb的头换成avcc格式
      for (auto& buf : spiltBufs) {
        annexb2AvccPacket(buf);
      }
    } else {
      annexb2AvccPacket(vdata);
    }
  }
  // android需要annexb格式
  if (bMustAnnexb && bvcc) {
    uint32_t naluLength = (vdata.data.data[0] << 24) |
                          (vdata.data.data[1] << 16) |
                          (vdata.data.data[2] << 8) | vdata.data.data[3];
    if (vdata.data.size > naluLength + 4) {
      splitAvccNalu(vdata, spiltBufs);
    }
    if (spiltBufs.size() > 0) {
      // 分段的avcc的头换成annexb格式
      for (auto& buf : spiltBufs) {
        avcc2AnnexbPacket(buf);
      }
    } else {
      avcc2AnnexbPacket(vdata);
    }
  }
  // MAC平台,现在vdata肯定是vcc的头
  if (bMustVcc) {
    uint32_t naluLength = (vdata.data.data[0] << 24) |
                          (vdata.data.data[1] << 16) |
                          (vdata.data.data[2] << 8) | vdata.data.data[3];
    if (vdata.data.size > naluLength + 4) {
      splitAvccNalu(vdata, spiltBufs);
    }
    if (spiltBufs.size() > 0) {
      // 分段的avcc的头换成annexb格式
      for (auto& buf : spiltBufs) {
        decode(buf);
      }
      return DecodeResult::success;
    } else {
      return decode(vdata);
    }
  }
  return decode(vdata);
}

}
