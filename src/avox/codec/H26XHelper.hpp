#pragma once

#include "../AvoxCodec.h"
#include "../AvoxMath.h"
#include "../module/LogHelper.hpp"
#include "../source/PacketBuf.hpp"
#include "H264Common.hpp"

namespace avox {

#if AVOX_DEBUG
#define MPP_H26X_SYNTAXT_STRICT_CHECK(cond, msg, exp) \
  if (!(cond)) {                                      \
    LOGFLF(LogLevel::warn, msg);                      \
    assert(false);                                    \
    exp;                                              \
  }
#define MPP_H26X_SYNTAXT_NORMAL_CHECK(cond, msg, exp) \
  if (!(cond)) {                                      \
    LOGFLF(LogLevel::warn, msg);                      \
    exp;                                              \
  }
#else
#define MPP_H26X_SYNTAXT_STRICT_CHECK(cond, msg, exp) \
  if (!(cond)) {                                      \
    exp;                                              \
  }
#define MPP_H26X_SYNTAXT_NORMAL_CHECK(cond, msg, exp) \
  if (!(cond)) {                                      \
    exp;                                              \
  }
#endif

// 表示当前帧未被引用，即当前帧不会被用作参考帧。
const uint64_t unused_for_reference = 0;
// 表示当前帧被用作短期参考帧，即当前帧会被用作参考帧，但是只会在短时间内被使用。
const uint64_t used_for_short_term_reference = 1 << 0U;
// 表示当前帧被用作长期参考帧，即当前帧会被用作参考帧，并且会在长时间内被使用。
const uint64_t used_for_long_term_reference = 1 << 1U;
// 表示当前帧不存在，即当前帧不会被解码或显示。
const uint64_t non_existing = 1 << 2U;
const int64_t no_long_term_frame_indices = -1;

// H264/H265太通用了,为了好分析调试,需要知道NAL具体信息
// 从 zlmediakit/ext-codec/H264|H265 复制过来
#define AVOX_MAP_H264_NAL(XX) \
  XX(NAL_NULL, 0, "NULL")    \
  XX(NAL_B_P, 1, "B_P")      \
  XX(NAL_DPA, 2, "DPA")      \
  XX(NAL_DPB, 3, "DPB")      \
  XX(NAL_DPC, 4, "DPC")      \
  XX(NAL_IDR, 5, "IDR")      \
  XX(NAL_SEI, 6, "SEI")      \
  XX(NAL_SPS, 7, "SPS")      \
  XX(NAL_PPS, 8, "PPS")      \
  XX(NAL_AUD, 9, "AUD")      \
  XX(NAL_SPS_EXT, 13, "SPS") \
  XX(NAL_SUB_SPS, 15, "SPS") \
  XX(NAL_VDRD, 24, "VDRD")

enum class H264NAL : uint8_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_H264_NAL(XX)
#undef XX
};

#define AVOX_MAP_H265_NAL(XX)                   \
  XX(NAL_TRAIL_N, 0, "TRAIL_N")                \
  XX(NAL_TRAIL_R, 1, "TRAIL_R")                \
  XX(NAL_TSA_N, 2, "TSA_N")                    \
  XX(NAL_TSA_R, 3, "TSA_R")                    \
  XX(NAL_STSA_N, 4, "STSA_N")                  \
  XX(NAL_STSA_R, 5, "STSA_R")                  \
  XX(NAL_RADL_N, 6, "RADL_N")                  \
  XX(NAL_RADL_R, 7, "RADL_R")                  \
  XX(NAL_RASL_N, 8, "RASL_N")                  \
  XX(NAL_RASL_R, 9, "RASL_R")                  \
  XX(NAL_BLA_W_LP, 16, "BLA_W_LP")             \
  XX(NAL_BLA_W_RADL, 17, "BLA_W_RADL")         \
  XX(NAL_BLA_N_LP, 18, "BLA_N_LP")             \
  XX(NAL_IDR_W_RADL, 19, "IDR_W_RADL")         \
  XX(NAL_IDR_N_LP, 20, "IDR_N_LP")             \
  XX(NAL_CRA_NUT, 21, "CRA_NUT")               \
  XX(NAL_RSV_IRAP_VCL22, 22, "RSV_IRAP_VCL22") \
  XX(NAL_RSV_IRAP_VCL23, 23, "RSV_IRAP_VCL23") \
  XX(NAL_VPS, 32, "VPS")                       \
  XX(NAL_SPS, 33, "SPS")                       \
  XX(NAL_PPS, 34, "PPS")                       \
  XX(NAL_AUD, 35, "AUD")                       \
  XX(NAL_EOS_NUT, 36, "EOS_NUT")               \
  XX(NAL_EOB_NUT, 37, "EOB_NUT")               \
  XX(NAL_FD_NUT, 38, "FD_NUT")                 \
  XX(NAL_SEI_PREFIX, 39, "SEI_PREFIX")         \
  XX(NAL_SEI_SUFFIX, 40, "SEI_SUFFIX")         \
  XX(NAL_UNSPEC63, 63, "UNSPEC63")

enum class H265NAL : uint8_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_H265_NAL(XX)
#undef XX
};

// 检查是否是否Annexb格式，返回3/4是nalu头，否则返回-1
AVOX_EXPORT int32_t checkAnnexbHeader(const uint8_t* extradata, int32_t size);
// 检查是否是AVCC,返回NAL长度前缀字节数（1/2/4），否则返回-1
AVOX_EXPORT int32_t checkAvccHeader(const uint8_t* extradata, int32_t size);
// 检查是否是HVCC,返回NAL长度前缀字节数（1/2/4），否则返回-1
AVOX_EXPORT int32_t checkHvccHeader(const uint8_t* extradata, int32_t size);

// H26x里的图像format对应的YuvType
AVOX_EXPORT YuvType getYuvType(int32_t format);

AVOX_EXPORT vec2i getSize(const H264SpsSyntax& sps);
AVOX_EXPORT double getFpsFromSps(const H264SpsSyntax& sps);
// annexb配置载荷(SPS/PPS[/VPS])提取视频尺寸: 裸流/私有协议源无容器desc可用,
// mp4等封装write_header必需宽高; 解析失败返回false(desc可能被部分填写)
AVOX_EXPORT bool h26xConfigVideoSize(VCodecId codecId, const uint8_t* data,
                                    int32_t size, VideoDesc& desc);

// 配置帧合并成avcc格式的extradata https://zhuanlan.zhihu.com/p/665881743
AVOX_EXPORT bool h264CombinExtradata(const std::vector<PacketBuf>& packets,
                                    std::vector<uint8_t>& extradata);
// avcc的extradata分解成sps/pps
AVOX_EXPORT void h264SplitAvcc(const uint8_t* extradata, int32_t size,
                              std::vector<PacketBuf>& packets);
// annexb的extradata分解成sps/pps
AVOX_EXPORT void h264SplitNalu(const uint8_t* extradata, int32_t size,
                              std::vector<PacketBuf>& packets);
// 配置帧合并成hvcc格式的extradata https://zhuanlan.zhihu.com/p/665881743
AVOX_EXPORT bool h265CombinExtradata(const std::vector<PacketBuf>& packets,
                                    std::vector<uint8_t>& extradata);
// hvcc的extradata分解成vps/sps/pps
AVOX_EXPORT void h265SplitHvcc(const uint8_t* extradata, int32_t size,
                              std::vector<PacketBuf>& packets);
// annexb的extradata分解成vps/sps/pps
AVOX_EXPORT void h265SplitNalu(const uint8_t* extradata, int32_t size,
                              std::vector<PacketBuf>& packets);

// 合并annexb配置帧成extradata
AVOX_EXPORT void annexbCombin(const std::vector<PacketBuf>& packets,
                             std::vector<uint8_t>& extradata);
// annexb为4的数据包,可以直接把头4字节转换成avcc相应长度包,直接修改包内数据
AVOX_EXPORT bool annexb2AvccPacket(AvoxPacket& packet);
// 把avcc的包转换成annexb的包
AVOX_EXPORT bool avcc2AnnexbPacket(AvoxPacket& packet);
// 拆分annexb格式数据包
AVOX_EXPORT void splitAnnexbNalu(const AvoxPacket& data, std::vector<AvoxPacket>& nalus);
// 拆分avcc/hvcc格式数据包
AVOX_EXPORT void splitAvccNalu(const AvoxPacket& data, std::vector<AvoxPacket>& nalus);

// 得到nalu类型
AVOX_EXPORT uint8_t getH264NalUnit(uint8_t* nalu);
AVOX_EXPORT uint8_t getH265NalUnit(uint8_t* nalu);
inline uint8_t getNalUnit(VCodecId codeId, uint8_t* nalu) {
  if (codeId == VCodecId::h264) {
    return getH264NalUnit(nalu);
  } else if (codeId == VCodecId::h265) {
    return getH265NalUnit(nalu);
  }
  return 0;
}
inline uint8_t getNalUnit(VCodecId codeId, const AvoxPacket& data) {
  uint8_t* nalu = data.data.data + data.prefixSize;
  return getNalUnit(codeId, nalu);
}

// 是否新帧
bool h264NaluNewFrame(uint8_t* nalu);
bool h265NaluNewFrame(uint8_t* nalu);
bool naluNewFrame(VCodecId codeId, uint8_t* nalu);

// 得到nalu类型名称
const char* getNalName(H264NAL nal);
const char* getNalName(H265NAL nal);
const char* getNalName(VCodecId codeId, uint8_t nal);

// 是否可丢弃
AVOX_EXPORT bool naluDropAble(H264NAL nal);
AVOX_EXPORT bool naluDropAble(H265NAL nal);
inline bool naluDropAble(VCodecId codeId, uint8_t nal) {
  if (codeId == VCodecId::h264) {
    return naluDropAble(static_cast<H264NAL>(nal));
  } else if (codeId == VCodecId::h265) {
    return naluDropAble(static_cast<H265NAL>(nal));
  }
  return false;
}

// 是否数据帧
bool naluDataFrame(H264NAL nal);
bool naluDataFrame(H265NAL nal);
inline bool naluDataFrame(VCodecId codeId, uint8_t nal) {
  if (codeId == VCodecId::h264) {
    return naluDataFrame(static_cast<H264NAL>(nal));
  } else if (codeId == VCodecId::h265) {
    return naluDataFrame(static_cast<H265NAL>(nal));
  }
  return false;
}

// 检查是否是关键帧
AVOX_EXPORT bool naluKeyFrame(H264NAL nal);
AVOX_EXPORT bool naluKeyFrame(H265NAL nal);
inline bool naluKeyFrame(VCodecId codeId, uint8_t nal) {
  if (codeId == VCodecId::h264) {
    return naluKeyFrame(static_cast<H264NAL>(nal));
  } else if (codeId == VCodecId::h265) {
    return naluKeyFrame(static_cast<H265NAL>(nal));
  }
  return false;
}

// 检查是否是IDR帧
AVOX_EXPORT bool naluConfigFrame(H264NAL nal);
AVOX_EXPORT bool naluConfigFrame(H265NAL nal);
inline bool naluConfigFrame(VCodecId codeId, uint8_t nal) {
  if (codeId == VCodecId::h264) {
    return naluConfigFrame(static_cast<H264NAL>(nal));
  } else if (codeId == VCodecId::h265) {
    return naluConfigFrame(static_cast<H265NAL>(nal));
  }
  return false;
}

struct H264Picture {
  uint64_t id = 0;
  /* inherit from nal unit */
  // 表示当前图片是否为场编码
  uint8_t field_pic_flag = 0;
  // 表示当前图片是否为底场
  uint8_t bottom_field_flag = 0;
  // 表示当前图片的帧序号低8位
  uint8_t pic_order_cnt_lsb = 0;
  // 表示当前图片的长期帧索引
  uint32_t long_term_frame_idx = 0;
  // 表示是否存在内存管理控制操作5
  bool has_memory_management_control_operation_5 = 0;

  /* 8.2.1 Decoding process for picture order count */
  // 表示当前图片的顶场帧序号 奇数行
  int32_t TopFieldOrderCnt = 2147483647;
  // 表示当前图片的底场帧序号 偶数行
  int32_t BottomFieldOrderCnt = 2147483647;
  // 表示前一帧的帧序号高8位
  int32_t prevPicOrderCntMsb = 0;
  // 表示帧序号偏移量
  int64_t FrameNumOffset = 0;

  /* 8.2.4 Decoding process for reference picture lists construction */
  // 表示最大帧序号
  uint64_t MaxFrameNum = 0;
  // 表示当前帧序号
  uint32_t FrameNum = 0;
  // 表示帧序号环绕值 短期参考图像，一个GOP一个周期
  int64_t FrameNumWrap = 0;
  // 表示短期图片序号 短期参考图像帧号
  int64_t PicNum = 0;

  /* 8.2.5 Decoded reference picture marking process */
  // 表示参考图片标记 1短期 2长期
  uint64_t referenceFlag = 0;
  // 表示最大长期帧索引
  int64_t MaxLongTermFrameIdx = 0;
  // 表示当前长期帧索引
  int64_t LongTermFrameIdx = 0;
  // 表示长期图片序号 长期参考图像帧号
  uint32_t LongTermPicNum = 0;
};

using H264PicturePtr = std::shared_ptr<H264Picture>;

// 得到视频帧的显示顺序POC
int32_t PicOrderCnt(const H264PicturePtr& picX);

}
