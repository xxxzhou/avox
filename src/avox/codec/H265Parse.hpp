#pragma once

#include "../AvoxCodec.h"
#include "H265Common.hpp"
#include "H26XNalReader.hpp"

namespace avox {

enum class H265StreamFormat {
  none,
  annexb,
  hvcc,
};

struct H265NalUnit {
  H265NAL nal = H265NAL::NAL_UNSPEC63;
  uint8_t nuh_layer_id = 0;
  uint8_t nuh_temporal_id_plus1 = 0;
  // 是否I帧开头(如果是网络包,数据帧可能是分开的)
  bool bIStart = false;

  // 可丢弃
  bool dropAble() {
    return nal == H265NAL::NAL_SEI_SUFFIX || nal == H265NAL::NAL_SEI_PREFIX ||
           nal == H265NAL::NAL_AUD;
  }
  // 可解码
  bool decodeAble() {
    // 如果I帧是分开的，nal_ptr[1] & 0x80表示一帧的开始
    return nal >= H265NAL::NAL_TRAIL_N && nal <= H265NAL::NAL_RSV_IRAP_VCL23;
  }
  // 配置帧
  bool configFrame() {
    return nal == H265NAL::NAL_SPS || nal == H265NAL::NAL_PPS ||
           nal == H265NAL::NAL_VPS;
  }
  bool keyFrame() {
    return nal >= H265NAL::NAL_BLA_W_LP && nal <= H265NAL::NAL_RSV_IRAP_VCL23 &&
           decodeAble();
  }
};

using H265NalUnitPtr = std::shared_ptr<H265NalUnit>;
using H265SpsPtr = std::shared_ptr<H265SpsSyntax>;
using H265PpsPtr = std::shared_ptr<H265PpsSyntax>;
using H265VpsPtr = std::shared_ptr<H265VPSSyntax>;
using H265SliceHeaderPtr = std::shared_ptr<H265SliceHeaderSyntax>;
using H265SeiPtr = std::shared_ptr<H265SeiMessageSyntax>;

struct H265ContextSyntax {
  std::unordered_map<int32_t, H265VpsPtr> vpsSet;
  std::unordered_map<int32_t, H265SpsPtr> spsSet;
  std::unordered_map<int32_t, H265PpsPtr> ppsSet;

  std::unordered_map<uint32_t, uint32_t> NumNegativePics;
  std::unordered_map<uint32_t, uint32_t> NumPositivePics;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint8_t>>
      UsedByCurrPicS0;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint8_t>>
      UsedByCurrPicS1;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>>
      DeltaPocS0;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>>
      DeltaPocS1;
  std::unordered_map<uint32_t, uint32_t> NumDeltaPocs;

  std::unordered_map<uint32_t, uint32_t> PocLsbLt;
  std::unordered_map<uint32_t, uint8_t> UsedByCurrPicLt;
};

using H265ContextPtr = std::shared_ptr<H265ContextSyntax>;

/**
 * @sa ITU-T H.265 (2021) - 8.3 Slice decoding process
 */
struct H265Picture {
  H265SliceHeaderPtr slice = nullptr;
  /* 8.3.1 Decoding process for picture order count */
  int64_t PicOrderCntVal;
  int64_t PicOrderCntMsb;
};

using H265PicturePtr = std::shared_ptr<H265Picture>;

class H265Parse {
 public:
  H265Parse();
  ~H265Parse();

 public:
  H265ContextPtr h265Contex = nullptr;
  // H265PictureContextPtr _prevTid0Pic;
  H265NalUnitPtr curUnit = nullptr;
  H265SliceHeaderPtr curSlice = nullptr;
  H265PicturePtr prePic = nullptr;

 private:
  H26XNalReader br = {};
  // 当前解析的流格式(一般直播流为annexb，媒体文件/点播流为avcc)
  H265StreamFormat ioType = H265StreamFormat::none;
  // annexb(3-4),avcc(1,2,4)
  int32_t naluSize = 4;

 public:
  // 解析H265视频流
  bool parse(const uint8_t* data, int32_t size, bool bFindStartCode = true);

 public:
  bool parsePpsSyntax();
  bool parseSpsSyntax();
  bool parseVPSSyntax();
  bool parseSliceHeaderSyntax(H265NalUnitPtr nal, H265SliceHeaderPtr slice);

 private: /* pps */
  bool parsePps3dSyntax(H265PpsSyntax& pps, H265Pps3dSyntax& pps3d);
  bool parsePpsRangeSyntax(H265SpsPtr sps, H265PpsSyntax& pps,
                           H265PpsRangeSyntax& ppsRange);
  bool parsePpsSccSyntax(H265PpsSccSyntax& ppsScc);

 private: /* sps */
  bool parseSpsRangeSyntax(H265SpsRangeSyntax& spsRange);
  bool parseSps3DSyntax(H265Sps3DSyntax& sps3d);
  bool parseSpsSccSyntax(H265SpsSyntax& sps, H265SpsSccSyntax& spsScc);
  bool parseVuiSyntax(H265SpsSyntax& sps, H265VuiSyntax& vui);

 private: /* slice */
  bool parseRefPicListsModificationSyntax(
      H265SpsSyntax& sps, H265PpsSyntax& pps, H265SliceHeaderSyntax& slice,
      H265RefPicListsModificationSyntax& rplm);
  bool parsePredWeightTableSyntax(H265NalUnit& header, H265SpsSyntax& sps,
                                  H265SliceHeaderSyntax& slice,
                                  H265PredWeightTableSyntax& pwt);

 private:
  bool parseHrdSyntax(uint8_t commonInfPresentFlag,
                      uint32_t maxNumSubLayersMinus, H265HrdSyntax& hrd);
  bool parseSubLayerHrdSyntax(uint32_t subLayerId, H265HrdSyntax& hrd,
                              H265SubLayerHrdSyntax& slHrd);
  bool parsePTLSyntax(uint8_t profilePresentFlag,
                      uint32_t maxNumSubLayersMinus1, H265PTLSyntax& ptl);
  bool parseScalingListDataSyntax(H265ScalingListDataSyntax& sld);
  bool parseStRefPicSetSyntax(H265SpsSyntax& sps, uint32_t stRpsIdx,
                              H265StRefPicSetSyntax& stps);
  bool parseColourMappingTable(H265ColourMappingTable& cmt);
  bool parsePpsMultilayerSyntax(H265PpsMultilayerSyntax& ppsMultilayer);
  bool parseDeltaDltSyntax(H265Pps3dSyntax& pps3d, H265DeltaDltSyntax& dd);
};

}