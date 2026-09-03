#pragma once

#include "../AvoxCodec.h"
#include "H26XHelper.hpp"
#include "H26XNalReader.hpp"

namespace avox {

struct H264NalUnit {
  H264NAL nal = H264NAL::NAL_NULL;
  uint8_t RefIdc = 0;
  uint8_t ExtFlag = 0;
 
  // 可丢弃
  bool dropAble() const {
    return nal == H264NAL::NAL_SEI || nal == H264NAL::NAL_AUD;
  }
  // 可解码
  bool decodeAble() const {
    // 如果I帧是分开的，nal_ptr[1] & 0x80表示一帧的开始
    return nal >= H264NAL::NAL_B_P && nal <= H264NAL::NAL_IDR;
  }
  bool configFrame() const {
    return nal == H264NAL::NAL_SPS || nal == H264NAL::NAL_PPS;
  }
  bool keyFrame() const { return nal == H264NAL::NAL_IDR && decodeAble(); }
};

using H264NalUnitPtr = std::shared_ptr<H264NalUnit>;
using H264SpsPtr = std::shared_ptr<H264SpsSyntax>;
using H264PpsPtr = std::shared_ptr<H264PpsSyntax>;
using H264SliceHeaderPtr = std::shared_ptr<H264SliceHeaderSyntax>;
using H264SeiPtr = std::shared_ptr<H264SeiSyntax>;

struct H264ContextSyntax {
  H264SpsPtr sps = nullptr;
  H264PpsPtr pps = nullptr;
  std::unordered_map<int32_t, H264SpsPtr> spsSet;
  std::unordered_map<int32_t, H264PpsPtr> ppsSet;
};

using H264ContextPtr = std::shared_ptr<H264ContextSyntax>;

/**
 * @brief H264解析器类
 *
 * 该类用于解析H264视频流的语法结构，包括NAL单元、SPS、PPS、SEI等。
 */
class H264Parse {
 public:
  H264Parse(/* args */);
  ~H264Parse();

 public:
  // 是否启用SEI解析
  bool enableParseSEI = false;
  // 是否启用SLICE解析
  bool enableParseSLICE = false;

 public:
  H264ContextPtr h264Contex = nullptr;
  H264NalUnitPtr curUnit = nullptr;
  H264SliceHeaderPtr curSlice = nullptr;
  H264SeiPtr curSei = nullptr;

 private:
  H26XNalReader br = {};
  // annexb(3-4),avcc(1,2,4)
  int32_t naluSize = 4;

 public:
  // 解析H264视频流
  bool parse(const uint8_t* data, int32_t size, bool bFindStartCode = true);
  // 解析SPS语法结构
  bool parseSpsSyntax();
  // 解析PPS语法结构
  bool parsePpsSyntax();
  // 解析解码参考图像标记语法结构
  bool parseDecodedReferencePictureMarkingSyntax(
      H264NalUnitPtr unit, H264DecodedReferencePictureMarkingSyntax& drpm);

  // 解析HRD语法结构
  bool parseHrdSyntax(H264HrdSyntax& hrd);

  // 解析VUI语法结构
  bool parseVuiSyntax(H264VuiSyntax& vui);

  // 解析SEI语法结构
  bool parseSeiSyntax(H264SeiSyntax& sei);

  // 解析SLICE头语法结构
  bool parseSliceHeaderSyntax(H264NalUnitPtr unit,
                              H264SliceHeaderSyntax& slice);

  // 解析子SPS语法结构
  bool parseSubSpsSyntax(H264SpsSyntax& sps, H264SubSpsSyntax& subSps);

  // 解析SPS MVC语法结构
  bool parseSpsMvcSyntax(H264SpsMvcSyntax& mvc);

  // 解析MVC VUI语法结构
  bool parseMvcVuiSyntax(H264MvcVuiSyntax& mvcVui);

 private:
  // 解析NAL SVC语法结构
  bool parseNalSvcSyntax(H264NalSvcSyntax& svc);

  // 解析NAL 3D AVC语法结构
  bool parseNal3dAvcSyntax(H264Nal3dAvcSyntax& avc);

  // 解析NAL MVC语法结构
  bool parseNalMvcSyntax(H264NalMvcSyntax& mvc);

  // 解析缩放列表语法结构
  bool parseScalingListSyntax(std::vector<int32_t>& scalingList,
                              int32_t sizeOfScalingList,
                              int32_t& useDefaultScalingMatrixFlag);

  // 解析参考图像列表修改语法结构
  bool parseReferencePictureListModificationSyntax(
      H264SliceHeaderSyntax& slice,
      H264ReferencePictureListModificationSyntax& rplm);

  // 解析预测权重表语法结构
  bool parsePredictionWeightTableSyntax(const H264SpsSyntax& sps,
                                        H264SliceHeaderSyntax& slice,
                                        H264PredictionWeightTableSyntax& pwt);

 private: /* SEI */
  // 解析SEI缓冲区周期语法结构
  bool parseSeiBufferPeriodSyntax(H264SeiBufferPeriodSyntax& bp);

  // 解析SEI图像定时语法结构
  bool parseSeiPictureTimingSyntax(const H264VuiSyntax& vui,
                                   H264SeiPictureTimingSyntax& pt);

  // 解析SEI用户数据注册语法结构
  bool parseSeiUserDataRegisteredSyntax(uint32_t payloadSize,
                                        H264SeiUserDataRegisteredSyntax& udr);

  // 解析SEI用户数据未注册语法结构
  bool parseSeiUserDataUnregisteredSyntax(
      uint32_t payloadSize, H264SeiUserDataUnregisteredSyntax& udn);

  // 解析SEI恢复点语法结构
  bool parseSeiRecoveryPointSyntax(H264SeiRecoveryPointSyntax& pt);

  // 解析SEI内容光级别信息语法结构
  bool parseSeiContentLigntLevelInfoSyntax(
      H264SeiContentLigntLevelInfoSyntax& clli);

  // 解析SEI显示方向语法结构
  bool parseSeiDisplayOrientationSyntax(H264SeiDisplayOrientationSyntax& dot);

  // 解析SEI主显示颜色体积语法结构
  bool parseSeiMasteringDisplayColourVolumeSyntax(
      H264MasteringDisplayColourVolumeSyntax& mdcv);

  // 解析SEI胶片颗粒特性语法结构
  bool parseSeiFilmGrainSyntax(H264SeiFilmGrainSyntax& fg);

  // 解析SEI帧打包排列语法结构
  bool parseSeiFramePackingArrangementSyntax(
      H264SeiFramePackingArrangementSyntax& fpa);

  // 解析SEI替代传输特性语法结构
  bool parseSeiAlternativeTransferCharacteristicsSyntax(
      H264SeiAlternativeTransferCharacteristicsSyntax& atc);

  // 解析环境观看环境语法结构
  bool parseAmbientViewingEnvironmentSyntax(
      H264AmbientViewingEnvironmentSyntax& awe);
};

}