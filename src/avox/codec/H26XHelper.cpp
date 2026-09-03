#include "H26XHelper.hpp"

#include <assert.h>

#include "H264Common.hpp"
#include "H264Parse.hpp"
#include "H265Common.hpp"
#include "H265Parse.hpp"

namespace avox {

int32_t checkAnnexbHeader(const uint8_t* extradata, int32_t size) {
  if (size < 3) {
    return -1;
  }
  if (extradata[0] == 0 && extradata[1] == 0 && extradata[2] == 1) {
    return 3;
  }
  if (size >= 4 && extradata[0] == 0 && extradata[1] == 0 &&
      extradata[2] == 0 && extradata[3] == 1) {
    return 4;
  }
  return -1;
}

int32_t checkAvccHeader(const uint8_t* extradata, int32_t size) {
  // AVCC格式头结构:
  // 第1字节: configurationVersion(0x01)
  // 第5字节: lengthSizeMinusOne (低2位)
  if (size < 5 || extradata[0] != 0x01) {
    return -1;
  }
  const uint8_t nalu_length_size = (extradata[4] & 0x03) + 1;
  return (nalu_length_size == 1 || nalu_length_size == 2 ||
          nalu_length_size == 4)
             ? nalu_length_size
             : -1;
}

// 新增HVCC检测（HEVC的extradata格式）
int32_t checkHvccHeader(const uint8_t* extradata, int32_t size) {
  // HVCC格式头结构:
  // 第1字节: configurationVersion(0x01)
  // 第22字节: lengthSizeMinusOne (低2位)
  if (size < 23 || extradata[0] != 0x01) {
    return -1;
  }
  const uint8_t nalu_length_size = (extradata[21] & 0x03) + 1;
  return (nalu_length_size == 1 || nalu_length_size == 2 ||
          nalu_length_size == 4)
             ? nalu_length_size
             : -1;
}

YuvType getYuvType(int32_t format) {
  switch (format) {
    case 1:
      return YuvType::yuv420P;
    case 2:
      return YuvType::yuv422P;
    case 3:
      return YuvType::yuv444P;
    default:
      return YuvType::other;
  }
  return YuvType::other;
}

const char* getNalName(H264NAL nal) {
  switch (nal) {
#define XX(name, value, str) \
  case H264NAL::name:        \
    return str;
    AVOX_MAP_H264_NAL(XX)
#undef XX
    default:
      return "invalid";
  }
  return nullptr;
}

const char* getNalName(H265NAL nal) {
  switch (nal) {
#define XX(name, value, str) \
  case H265NAL::name:        \
    return str;
    AVOX_MAP_H265_NAL(XX)
#undef XX
    default:
      return "invalid";
  }
  return nullptr;
}

const char* getNalName(VCodecId codeId, uint8_t nal) {
  if (codeId == VCodecId::h264) {
    return getNalName(static_cast<H264NAL>(nal));
  } else if (codeId == VCodecId::h265) {
    return getNalName(static_cast<H265NAL>(nal));
  }
  return "";
}

vec2i getSize(const H264SpsSyntax& sps) {
  vec2i size = {};
  int32_t horStride = (sps.pic_width_in_mbs_minus1 + 1) * 16;
  int32_t virStride = (sps.pic_height_in_map_units_minus1 + 1) * 16;
  size.x =
      horStride - (sps.frame_crop_left_offset + sps.frame_crop_right_offset) *
                      sps.context.CropUnitX;
  size.y =
      virStride - (sps.frame_crop_top_offset + sps.frame_crop_bottom_offset) *
                      sps.context.CropUnitY;
  return size;
}

// 参考3rdparty\ZLMediaKit\ext-codec\SPSParser.c{
double getFpsFromSps(const H264SpsSyntax& sps) {
  double fps = 0.0;
  if (sps.vui_seq_parameters.timing_info_present_flag) {
    fps = static_cast<float>(sps.vui_seq_parameters.time_scale) /
          static_cast<float>(sps.vui_seq_parameters.num_units_in_tick) / 2.0f;
  }
  switch (static_cast<int>(fps)) {
    case 23:  // 23.98
      return 23.98;
    case 24:
      return 24;
    case 25:
      return 25;
    case 29:  // 29.97
      return 29.97;
    case 30:
      return 30;
    case 50:
      return 50;
    case 59:  // 59.94
      return 59.94;
    case 60:
      return 60;
    case 6:
      return 6;
    case 8:
      return 8;
    case 12:
      return 12;
    case 15:
      return 15;
    case 10:
      return 10;
    default:
      return 0;
  }
  return fps;
}

// 配置帧载荷拆NALU逐个喂解析器(SPS/PPS需同一上下文按序入)
bool h26xConfigVideoSize(VCodecId codecId, const uint8_t* data, int32_t size,
                         VideoDesc& desc) {
  std::vector<PacketBuf> nalus;
  if (codecId == VCodecId::h264) {
    h264SplitNalu(data, size, nalus);
    H264Parse parse;
    for (auto& nalu : nalus) {
      parse.parse(nalu.buff.data(), nalu.size);
    }
    H264SpsPtr sps = parse.h264Contex ? parse.h264Contex->sps : nullptr;
    if (!sps) {
      return false;
    }
    vec2i sz = getSize(*sps);
    desc.width = sz.x;
    desc.height = sz.y;
    desc.fps = getFpsFromSps(*sps);
    desc.type = getYuvType(sps->chroma_format_idc);
  } else if (codecId == VCodecId::h265) {
    h265SplitNalu(data, size, nalus);
    H265Parse parse;
    for (auto& nalu : nalus) {
      parse.parse(nalu.buff.data(), nalu.size);
    }
    H265SpsPtr sps =
        parse.h265Contex ? parse.h265Contex->spsSet[0] : nullptr;
    if (!sps) {
      return false;
    }
    desc.width = (int32_t)sps->pic_width_in_luma_samples;
    desc.height = (int32_t)sps->pic_height_in_luma_samples;
    desc.type = getYuvType(sps->chroma_format_idc);
  } else {
    return false;
  }
  return desc.width > 0 && desc.height > 0;
}

// (8-1) 得到视频帧的显示顺序POC
int32_t PicOrderCnt(const H264PicturePtr& picX) {
  if (picX->field_pic_flag == 0) {
    // 编码帧 coded frame
    return std::min(picX->TopFieldOrderCnt, picX->BottomFieldOrderCnt);
  } else if (picX->bottom_field_flag == 0 /* && picX->field_pic_flag == 1 */) {
    // 编码场,顶场
    return picX->TopFieldOrderCnt;
  } else if (picX->bottom_field_flag == 1 /* && picX->field_pic_flag == 1 */) {
    // 编码场,底场
    return picX->BottomFieldOrderCnt;
  } else {
    assert(false);
    return 0;
  }
}

// 后续去参考libavformat/avc.c/ff_isom_write_avcc完善
bool h264CombinExtradata(const std::vector<PacketBuf>& packets,
                         std::vector<uint8_t>& extradata) {
  std::vector<PacketBuf> spss;
  std::vector<PacketBuf> ppss;
  for (auto& packet : packets) {
    uint8_t nalu_type = packet.buff[packet.prefixSize] & 0x1F;
    if (nalu_type == 7) {
      spss.push_back(packet);
    } else if (nalu_type == 8) {
      ppss.push_back(packet);
    }
  }
  int32_t sps_count = spss.size();
  int32_t pps_count = ppss.size();
  if (sps_count < 1 || pps_count < 1) {
    return false;
  }
  if (spss[0].size < 4) {
    return false;
  }
  uint8_t profile = spss[0].buff[spss[0].prefixSize + 1];
  uint8_t compat = spss[0].buff[spss[0].prefixSize + 2];
  uint8_t level = spss[0].buff[spss[0].prefixSize + 3];

  // 构建AVCC格式extradata
  extradata.clear();
  // 预分配足够空间
  extradata.reserve(512);
  // 1. 版本信息 (AVOXHeader) configurationVersion
  extradata.push_back(0x01);
  // AVOXProfileIndication
  extradata.push_back(profile);
  // profile_compatibility
  extradata.push_back(compat);
  // AVOXLevelIndication
  extradata.push_back(level);

  // NALU 长度所占字节数（这里固定为 4）
  extradata.push_back(0xFC | 0x03);
  // SPS数量
  extradata.push_back(0xE0 | sps_count);
  for (const auto& sps_packet : spss) {
    int sps_size = sps_packet.size - sps_packet.prefixSize;
    // SPS 长度（2 字节）高3位111表示后续字段结构，低5位SPS数量1
    extradata.push_back(sps_size >> 8);
    // SPS 长度（2 字节）低8位
    extradata.push_back(sps_size & 0xFF);
    // SPS 数据
    extradata.insert(extradata.end(),
                     sps_packet.buff.data() + sps_packet.prefixSize,
                     sps_packet.buff.data() + sps_packet.size);
  }
  // PPS 数量
  extradata.push_back(pps_count);
  // 处理 PPS 数据包
  for (const auto& pps_packet : ppss) {
    int pps_size = pps_packet.size - pps_packet.prefixSize;
    // PPS 长度（2 字节）
    extradata.push_back(pps_size >> 8);
    extradata.push_back(pps_size & 0xFF);
    // PPS 数据
    extradata.insert(extradata.end(),
                     pps_packet.buff.data() + pps_packet.prefixSize,
                     pps_packet.buff.data() + pps_packet.size);
  }
  return true;
}

void h264SplitAvcc(const uint8_t* extradata_, int32_t size,
                   std::vector<PacketBuf>& packets) {
  // https://www.cnblogs.com/vczf/p/13844580.html
  // avcc头的拆成avcc包
  packets.clear();
  const uint8_t* start_data = extradata_;
  uint8_t* extradata = (uint8_t*)start_data;
  int extradata_size = size;
  // 1. 跳过前4个字节 版本信息 (AVOXHeader)
  extradata = extradata + 4;
  // 2. byte_5：获取nalu length所占字节数
  // 在extradata中,nalu长固定为2字节,nalu_length_bytes这里没用到
  int32_t nalu_length_bytes = (*extradata++ & 0x03) + 1;
  while ((extradata - start_data) < extradata_size) {
    // 3. byte_6：获取下一个sps/pps个数
    uint8_t item_nb = *extradata++ & 0x1f;
    if (!item_nb) {
      continue;
    }
    while (item_nb--) {
      // 4. byte_7 byte_8: 表示sps或者pps长度
      uint32_t uint_size = (extradata[0] << 8) | extradata[1];
      // 5. 数据异常
      if ((extradata - start_data + uint_size + 2) > extradata_size) {
        break;
      }
      // 6. byte_9: 检查包类型
      uint8_t nalu_type = extradata[2];
      nalu_type = nalu_type & 0x1f;
      // 7. sps/pps
      if (nalu_type == (uint8_t)H264NAL::NAL_SPS ||
          nalu_type == (uint8_t)H264NAL::NAL_PPS) {
        // a.分配内存
        std::vector<uint8_t> cfcPkt(uint_size + 4);
        // b.avcc头,写入uint_size
        // b. 写入NALU长度（大端序）
        cfcPkt[0] = (uint_size >> 24) & 0xFF;
        cfcPkt[1] = (uint_size >> 16) & 0xFF;
        cfcPkt[2] = (uint_size >> 8) & 0xFF;
        cfcPkt[3] = uint_size & 0xFF;
        memcpy(cfcPkt.data() + 4, extradata + 2, uint_size);
        PacketBuf packet(cfcPkt);
        packet.packtype = (int32_t)PackType::vconfig;
        packet.prefixSize = 4;
        packets.push_back(packet);
      }
      extradata = extradata + 2 + uint_size;
    }
  }
}

void h264SplitNalu(const uint8_t* extradata_, int32_t size,
                   std::vector<PacketBuf>& packets) {
  packets.clear();
  const uint8_t* start_data = extradata_;
  const uint8_t* extradata = extradata_;
  int extradata_size = size;
  while ((extradata - start_data) < extradata_size) {
    // 查找下一个NALU的起始位置
    const uint8_t* nalu_start = extradata;
    int nalu_prefix_size = 0;
    if (extradata[0] == 0 && extradata[1] == 0 && extradata[2] == 0 &&
        extradata[3] == 1) {
      nalu_prefix_size = 4;
      nalu_start += 4;
    } else if (extradata[0] == 0 && extradata[1] == 0 && extradata[2] == 1) {
      nalu_prefix_size = 3;
      nalu_start += 3;
    } else {
      // 数据格式错误，跳过当前位置
      extradata++;
      continue;
    }
    // 查找下一个NALU的结束位置
    const uint8_t* nalu_end = nalu_start;
    while (nalu_end < extradata_ + extradata_size) {
      if ((nalu_end[0] == 0 && nalu_end[1] == 0 &&
           (nalu_end[2] == 0 || nalu_end[2] == 1)) ||
          (nalu_end[0] == 0 && nalu_end[1] == 0 && nalu_end[2] == 0 &&
           nalu_end[3] == 1)) {
        break;
      }
      nalu_end++;
    }
    int nalu_size = nalu_end - nalu_start;
    // 检查包类型
    uint8_t nalu_type = nalu_start[0] & 0x1f;
    if (nalu_type == (uint8_t)H264NAL::NAL_SPS ||
        nalu_type == (uint8_t)H264NAL::NAL_PPS) {
      std::vector<uint8_t> packetData(nalu_start - nalu_prefix_size, nalu_end);
      // 如果是3字节起始码，前面补0x00
      if (nalu_prefix_size == 3) {
        packetData.insert(packetData.begin(), 0x00);
      }
      PacketBuf pkt(packetData);
      pkt.prefixSize = 4;
      pkt.packtype = (int32_t)PackType::vconfig;
      packets.push_back(pkt);
    }
    extradata = nalu_end;
  }
}

void annexbCombin(const std::vector<PacketBuf>& packets,
                  std::vector<uint8_t>& extradata) {
  extradata.clear();
  for (const auto& pkt : packets) {
    // 直接拼接所有PacketBuf的数据即可
    extradata.insert(extradata.end(), pkt.buff.begin(),
                     pkt.buff.begin() + pkt.size);
  }
}

bool h265CombinExtradata(const std::vector<PacketBuf>& packets,
                         std::vector<uint8_t>& extradata) {
  std::vector<PacketBuf> vpss, spss, ppss;

  // 提取VPS/SPS/PPS（H265新增VPS参数集）
  for (auto& packet : packets) {
    uint8_t nalu_header = packet.buff[packet.prefixSize];
    uint8_t nalu_type = (nalu_header >> 1) & 0x3F;  // H265的nalu_type占6位
    if (nalu_type == 32) {                          // VPS
      vpss.push_back(packet);
    } else if (nalu_type == 33) {  // SPS
      spss.push_back(packet);
    } else if (nalu_type == 34) {  // PPS
      ppss.push_back(packet);
    }
  }
  // 校验必要参数集存在
  if (vpss.empty() || spss.empty() || ppss.empty()) {
    return false;
  }
  // 构建HEVCDecoderConfigurationRecord（HVCC）
  extradata.clear();
  extradata.reserve(256);
  /* 1. 基础配置头 */
  extradata.push_back(0x01);  // configurationVersion

  // 后面改为从spss[0]解析出的数据
  H265SpsSyntax sps = {};
  // 默认4:2:0
  sps.chroma_format_idc = 1;
  sps.bit_depth_luma_minus8 = 0;
  sps.bit_depth_chroma_minus8 = 0;
  sps.vui.min_spatial_segmentation_idc = 0;
  // 默认Level 4.0 // (pps.entropy_coding_sync_enabled_flag << 1) |
  // sps.vui.entry_point_start_present_flag;
  uint8_t parallelism_type = 0x40;

  // 从VPS提取profile信息
  const uint8_t* vps_data = vpss[0].buff.data() + vpss[0].prefixSize;
  uint8_t profile_space = (vps_data[1] >> 6) & 0x03;
  uint8_t tier_flag = (vps_data[1] >> 5) & 0x01;
  uint8_t profile_idc = vps_data[1] & 0x1F;
  extradata.push_back((profile_space << 6) | (tier_flag << 5) | profile_idc);

  // general_profile_compatibility_flags  bytes 3-6
  extradata.insert(extradata.end(), vps_data + 2, vps_data + 6);
  // general_constraint_indicator_flags bytes 7-12
  extradata.insert(extradata.end(), vps_data + 6, vps_data + 12);
  // general_level_idc (8 bits) bytes 13-14
  extradata.insert(extradata.end(), 2, 0x00);
  // general_level_idc bytes 15
  extradata.push_back(vps_data[12]);

  /* 2. 视频参数配置 */
  // min_spatial_segmentation_idc (12 bits)
  extradata.push_back((sps.vui.min_spatial_segmentation_idc >> 8) & 0x0F);
  extradata.push_back(sps.vui.min_spatial_segmentation_idc & 0xFF);
  // parallelism_type (2 bits) | chroma_format_idc (2 bits)
  uint8_t parallel_chroma =
      (parallelism_type << 6) | (sps.chroma_format_idc << 4);
  // bytes 18
  extradata.push_back(parallel_chroma | 0xF0);
  // bit_depth配置
  uint8_t bit_depth =
      (sps.bit_depth_luma_minus8 << 5) | (sps.bit_depth_chroma_minus8 << 2);
  extradata.push_back(bit_depth | 0xFC);
  // 帧率配置（示例值）avgFrameRate
  extradata.push_back({0x00});
  extradata.push_back({0x00});
  // constantFrameRate
  extradata.push_back(0x0F);

  /* 3. 参数集数组 */
  // numOfArrays
  extradata.push_back(0x03);
  // 写入参数集
  auto WriteParamSet = [&](uint8_t type, const auto& packets) {
    extradata.push_back(type);
    extradata.push_back(0x00);  // num_nalus高字节
    extradata.push_back(static_cast<uint8_t>(packets.size()));  // 低字节

    for (const auto& pkt : packets) {
      int size = pkt.size - pkt.prefixSize;
      extradata.push_back(size >> 8);
      extradata.push_back(size & 0xFF);
      const uint8_t* start = pkt.buff.data() + pkt.prefixSize;
      extradata.insert(extradata.end(), start, start + size);
    }
  };
  WriteParamSet(0x20, vpss);  // VPS
  WriteParamSet(0x21, spss);  // SPS
  WriteParamSet(0x22, ppss);  // PPS
  return true;
}

void h265SplitHvcc(const uint8_t* hvcc_data, int32_t size,
                   std::vector<PacketBuf>& packets) {
  packets.clear();
  if (size < 23 || hvcc_data[0] != 0x01) {
    return;
  }
  // 解析参数集数组
  const uint8_t* ptr = hvcc_data + 22;
  const uint8_t* end = hvcc_data + size;
  uint8_t configArrarSize = ptr[0];
  ptr += 1;
  while (ptr + 3 <= end) {
    // 读取低六位
    const uint8_t array_type = ptr[0] & 0x3f;
    const uint16_t num_nalus = (ptr[1] << 8) | ptr[2];
    ptr += 3;
    for (int i = 0; i < num_nalus && ptr + 2 <= end; ++i) {
      const uint32_t nal_size = (ptr[0] << 8) | ptr[1];
      ptr += 2;
      if (ptr + nal_size > end) {
        break;
      }
      // 构建hvcc配置帧包
      std::vector<uint8_t> annexb_data;
      annexb_data.resize(nal_size + 4);
      // b. 写入NALU长度（大端序）
      annexb_data[0] = (nal_size >> 24) & 0xFF;
      annexb_data[1] = (nal_size >> 16) & 0xFF;
      annexb_data[2] = (nal_size >> 8) & 0xFF;
      annexb_data[3] = nal_size & 0xFF;
      memcpy(annexb_data.data() + 4, ptr, nal_size);
      PacketBuf pkt(annexb_data);
      pkt.prefixSize = 4;
      pkt.packtype = (int32_t)PackType::vconfig;
      packets.push_back(pkt);
      ptr += nal_size;
    }
  }
}

void h265SplitNalu(const uint8_t* data, int32_t size,
                   std::vector<PacketBuf>& packets) {
  packets.clear();
  const uint8_t* ptr = data;
  const uint8_t* end = data + size;
  while (ptr < end) {
    // 查找起始码（支持3/4字节）
    const uint8_t* naluStart = nullptr;
    int prefixSize = 0;
    if (ptr + 4 <= end && memcmp(ptr, "\x00\x00\x00\x01", 4) == 0) {
      prefixSize = 4;
      naluStart = ptr + 4;
    } else if (ptr + 3 <= end && memcmp(ptr, "\x00\x00\x01", 3) == 0) {
      prefixSize = 3;
      naluStart = ptr + 3;
    } else {
      ptr++;
      continue;
    }
    // 查找下一个起始码
    const uint8_t* nextStart = naluStart;
    while (nextStart < end) {
      if (memcmp(nextStart, "\x00\x00\x00\x01", 4) == 0 ||
          memcmp(nextStart, "\x00\x00\x01", 3) == 0) {
        break;
      }
      nextStart++;
    }
    // 提取NALU类型（6位）
    uint8_t nalu_header = *naluStart;
    uint8_t nalu_type = (nalu_header >> 1) & 0x3F;
    // 仅处理参数集（VPS=32/SPS=33/PPS=34）
    if (nalu_type >= 32 && nalu_type <= 34) {
      std::vector<uint8_t> packetData(ptr, nextStart);
      // 如果是3字节起始码，前面补0x00
      if (prefixSize == 3) {
        packetData.insert(packetData.begin(), 0x00);
      }
      PacketBuf pkt(packetData);
      pkt.prefixSize = 4;
      pkt.packtype = (int32_t)PackType::vconfig;
      packets.push_back(pkt);
    }
    ptr = nextStart;
  }
}

// 将ANNEX-B格式的NALU数据包转为AVCC格式，非配置帧
bool annexb2AvccPacket(AvoxPacket& packet) {
  // 检查是否为 4 字节的 ANNEX-B 头
  if (packet.data.data[0] != 0x00 || packet.data.data[1] != 0x00 ||
      packet.data.data[2] != 0x00 || packet.data.data[3] != 0x01) {
    return false;
  }
  // 计算 NALU 数据长度（去除 4 字节 ANNEX-B 头）
  size_t naluSize = packet.data.size - 4;
  // 将 NALU 长度以大端字节序写入前 4 个字节
  packet.data.data[0] = static_cast<uint8_t>(naluSize >> 24);
  packet.data.data[1] = static_cast<uint8_t>(naluSize >> 16);
  packet.data.data[2] = static_cast<uint8_t>(naluSize >> 8);
  packet.data.data[3] = static_cast<uint8_t>(naluSize);
  return true;
}

bool avcc2AnnexbPacket(AvoxPacket& packet) {
  // 检查长度是否正确
  if (packet.data.size < 4) {
    return false;
  }
  uint8_t* buffer = packet.data.data;
  uint32_t naluLength =
      (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
  if (naluLength + 4 != packet.data.size) {
    return false;
  }
  packet.data.data[0] = 0;
  packet.data.data[1] = 0;
  packet.data.data[2] = 0;
  packet.data.data[3] = 1;
  return true;
}

void splitAnnexbNalu(const AvoxPacket& data, std::vector<AvoxPacket>& nalus) {
  nalus.clear();
  const uint8_t* buffer = data.data.data;
  int32_t size = static_cast<int32_t>(data.data.size);
  int32_t pos = 0;
  while (pos < size) {
    int startCodeLen = 0;
    if (pos + 4 <= size && buffer[pos] == 0x00 && buffer[pos + 1] == 0x00 &&
        buffer[pos + 2] == 0x00 && buffer[pos + 3] == 0x01) {
      startCodeLen = 4;
    } else if (pos + 3 <= size && buffer[pos] == 0x00 &&
               buffer[pos + 1] == 0x00 && buffer[pos + 2] == 0x01) {
      startCodeLen = 3;
    } else {
      ++pos;
      continue;
    }
    int32_t nalu_start = pos;
    pos += startCodeLen;
    int32_t next_start = pos;
    while (next_start < size) {
      if ((next_start + 4 <= size && buffer[next_start] == 0x00 &&
           buffer[next_start + 1] == 0x00 && buffer[next_start + 2] == 0x00 &&
           buffer[next_start + 3] == 0x01) ||
          (next_start + 3 <= size && buffer[next_start] == 0x00 &&
           buffer[next_start + 1] == 0x00 && buffer[next_start + 2] == 0x01)) {
        break;
      }
      ++next_start;
    }
    AvoxPacket nalu = data;
    nalu.data.data = const_cast<uint8_t*>(buffer + nalu_start);
    nalu.data.size = next_start - nalu_start;
    nalu.prefixSize = startCodeLen;
    // 指向外部数据，不管理内存
    nalu.data.bRef = 1;
    nalus.push_back(nalu);
    pos = next_start;
  }
}

// 处理AVCC/HVCC格式的NALU拆分
void splitAvccNalu(const AvoxPacket& data, std::vector<AvoxPacket>& nalus) {
  nalus.clear();
  const uint8_t* buffer = data.data.data;
  int32_t size = static_cast<int32_t>(data.data.size);
  int32_t pos = 0;
  // AVCC/HVCC格式处理
  while (pos + 4 <= size) {
    // 读取NALU长度（4字节大端序）
    uint32_t naluLength = (buffer[pos] << 24) | (buffer[pos + 1] << 16) |
                          (buffer[pos + 2] << 8) | buffer[pos + 3];
    if (naluLength == 0) {
      // 长度为0，跳过
      pos += 4;
      continue;
    }
    if (pos + 4 + naluLength > (uint32_t)size) {
      // 长度超出数据范围，结束处理
      break;
    }
    AvoxPacket nalu = data;
    nalu.data.data = const_cast<uint8_t*>(buffer + pos);
    nalu.data.size = naluLength + 4;
    // 指向外部数据，不管理内存
    nalu.data.bRef = 1;
    nalus.push_back(nalu);
    pos += 4 + naluLength;
  }
}

uint8_t getH264NalUnit(uint8_t* nalu) {
  uint8_t ualUnit = nalu[0] & 0x1F;
  return ualUnit;
}

uint8_t getH265NalUnit(uint8_t* nalu) {
  uint8_t ualUnit = (nalu[0] >> 1) & 0x3F;
  return ualUnit;
}

// zlmediakit h264_is_new_access_unit
bool h264NaluNewFrame(uint8_t* nalu) {
  H264NAL nal = (H264NAL)getH264NalUnit(nalu);
  if (naluConfigFrame(nal)) {
    return true;
  }
  if (nal == H264NAL::NAL_AUD || nal == H264NAL::NAL_SEI) {
    return true;
  }
  // 14-18也返回true
  if ((14 <= (uint8_t)nal && (uint8_t)nal <= 18)) {
    return true;
  }
  // 数据帧需要检查first_mb_in_slice
  if (nal >= H264NAL::NAL_B_P && nal <= H264NAL::NAL_IDR) {
    // first_mb_in_slice是UE编码,这里用启发式方法
    // 标准方法需要完整解析UE编码
    return (nalu[1] & 0x80) != 0;
  }
  return false;
}

// zlmediakit h265_is_new_access_unit
bool h265NaluNewFrame(uint8_t* nalu) {
  H265NAL nal = (H265NAL)getH265NalUnit(nalu);
  if (naluConfigFrame(nal)) {
    return true;
  }
  uint8_t nal_type = (nalu[0] >> 1) & 0x3f;
  uint8_t nuh_layer_id = ((nalu[0] & 0x01) << 5) | ((nalu[1] >> 3) & 0x1F);
  if ((nuh_layer_id == 0 &&
       (H265NAL::NAL_AUD == nal || H265NAL::NAL_SEI_SUFFIX == nal ||
        (41 <= nal_type && nal_type <= 44) ||
        (48 <= nal_type && nal_type <= 55)))) {
    return true;
  }
  // 数据帧0-23需要检查
  if (nal >= H265NAL::NAL_TRAIL_N && nal <= H265NAL::NAL_RSV_IRAP_VCL23) {
    // first_slice_segment_in_pic_flag
    return (nalu[2] & 0x80) != 0;
  }
  return false;
}
bool naluNewFrame(VCodecId codeId, uint8_t* nalu) {
  if (!nalu) {
    return false;
  }
  if (codeId == VCodecId::h264) {
    return h264NaluNewFrame(nalu);
  } else if (codeId == VCodecId::h265) {
    return h265NaluNewFrame(nalu);
  }
  return false;
}

bool naluDropAble(H264NAL nal) {
  return nal == H264NAL::NAL_SEI || nal == H264NAL::NAL_AUD;
}

bool naluDropAble(H265NAL nal) {
  return nal == H265NAL::NAL_SEI_SUFFIX || nal == H265NAL::NAL_SEI_PREFIX ||
         nal == H265NAL::NAL_AUD;
}

// 是否数据帧
bool naluDataFrame(H264NAL nal) {
  // 如果I帧是分开的，nal_ptr[1] & 0x80表示一帧的开始
  return nal >= H264NAL::NAL_B_P && nal <= H264NAL::NAL_IDR;
}

bool naluDataFrame(H265NAL nal) {
  return nal >= H265NAL::NAL_TRAIL_N && nal <= H265NAL::NAL_RSV_IRAP_VCL23;
}

bool naluConfigFrame(H264NAL nal) {
  return nal == H264NAL::NAL_SPS || nal == H264NAL::NAL_PPS;
}

bool naluConfigFrame(H265NAL nal) {
  return nal == H265NAL::NAL_SPS || nal == H265NAL::NAL_PPS ||
         nal == H265NAL::NAL_VPS;
}
// 检查是否是关键帧
bool naluKeyFrame(H264NAL nal) { return nal == H264NAL::NAL_IDR; }
bool naluKeyFrame(H265NAL nal) {
  return nal >= H265NAL::NAL_BLA_W_LP && nal <= H265NAL::NAL_RSV_IRAP_VCL23;
}

// 检查是否是IDR帧

}
