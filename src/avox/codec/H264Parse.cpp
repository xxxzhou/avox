#include "H264Parse.hpp"

#include "../AvoxMath.h"
#include "../codec/H26XHelper.hpp"
#include "../module/LogHelper.hpp"

// maximum size of reference picture lists (number of pictures)
#define AVOX_MAX_REFS 32
// maximum size of decoded picture buffer (number of frames)
#define AVOX_MAX_DPB_SIZE 16
// maximum size of decoded picture buffer (number of frames) + reff buffer
#define AVOX_MAX_DPB_SVC_SIZE 17
// maximum number of mmco's
#define AVOX_MAX_MMCOS 72

namespace avox {

// Table 7-3 – Specification of default scaling lists Default_4x4_Intra and
// Default_4x4_Inter
static std::vector<int32_t> Default_4x4_Intra = {
    6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32, 32, 37, 37, 42};
static std::vector<int32_t> Default_4x4_Inter = {
    10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34};
// Table 7-4 – Specification of default scaling lists Default_8x8_Intra and
// Default_8x8_Inter
static std::vector<int32_t> Default_8x8_Intra = {
    6,  10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18, 18, 18, 18, 23,
    23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
    27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
    31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42};
static std::vector<int32_t> Default_8x8_Inter = {
    10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34,
    9,  13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19, 19, 19, 19, 21,
    24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27,
    27, 28, 28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35};

static const vec2i atFfH264PixelSspect[17] = {
    {0, 1},   {1, 1},    {12, 11}, {10, 11}, {16, 11}, {40, 33},
    {24, 11}, {20, 11},  {32, 11}, {80, 33}, {18, 11}, {15, 11},
    {64, 33}, {160, 99}, {4, 3},   {3, 2},   {2, 1},
};

void GetH264SubWidthCAndSubHeightC(const H264SpsSyntax &sps, uint8_t &SubWidthC,
                                   uint8_t &SubHeightC) {
  SubWidthC = 1, SubHeightC = 1;
  if (sps.chroma_format_idc == 0 && sps.separate_colour_plane_flag == 0) {
    assert(false);
  } else if (sps.chroma_format_idc == 1 &&
             sps.separate_colour_plane_flag == 0) {
    SubWidthC = 2;
    SubHeightC = 2;
  } else if (sps.chroma_format_idc == 2 &&
             sps.separate_colour_plane_flag == 0) {
    SubWidthC = 2;
    SubHeightC = 1;
  } else if (sps.chroma_format_idc == 3 &&
             sps.separate_colour_plane_flag == 0) {
    // SubWidthC = 1;
    // SubHeightC = 1;
  } else if (sps.chroma_format_idc == 3 &&
             sps.separate_colour_plane_flag == 1) {
    assert(false);
  } else {
    assert(false);
  }
}

void FillH264SpsContext(H264SpsSyntax &sps) {
  uint8_t SubWidthC, SubHeightC;
  uint8_t MbWidthC, MbHeightC;

  GetH264SubWidthCAndSubHeightC(sps, SubWidthC, SubHeightC);
  MbWidthC = 16 / SubWidthC;   // (6-1)
  MbHeightC = 16 / SubHeightC; // (6-2)

  H264SpsContext &context = sps.context;

  context.BitDepthY = 8 + sps.bit_depth_luma_minus8;
  context.QpBdOffsetY = 6 * sps.bit_depth_luma_minus8;

  context.BitDepthC = 8 + sps.bit_depth_chroma_minus8;
  context.QpBdOffsetC = 6 * sps.bit_depth_chroma_minus8;

  context.RawMbBits =
      256 * context.BitDepthY + 2 * MbWidthC * MbHeightC * context.BitDepthC;

  context.MaxFrameNum = 1 << (sps.log2_max_frame_num_minus4 + 4);

  context.MaxPicOrderCntLsb = 1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4);

  context.ExpectedDeltaPerPicOrderCntCycle = 0;
  for (uint32_t i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++) {
    context.ExpectedDeltaPerPicOrderCntCycle += sps.offset_for_ref_frame[i];
  }

  context.PicWidthInMbs = sps.pic_width_in_mbs_minus1 + 1;
  context.PicWidthInSamplesL = context.PicWidthInMbs * 16;
  context.PicWidthInSamplesC = context.PicWidthInMbs * MbWidthC;

  context.PicHeightInMapUnits = sps.pic_height_in_map_units_minus1 + 1;
  context.PicSizeInMapUnits =
      context.PicWidthInMbs * context.PicHeightInMapUnits;

  context.FrameHeightInMbs =
      (2 - sps.frame_mbs_only_flag) * context.PicHeightInMapUnits;

  uint32_t ChromaArrayType =
      sps.separate_colour_plane_flag == 1 ? 0 : sps.chroma_format_idc;
  if (ChromaArrayType == 0) {
    context.CropUnitX = 1;
    context.CropUnitY = 2 - sps.frame_mbs_only_flag;
  } else {
    context.CropUnitX = SubWidthC;
    context.CropUnitY = SubHeightC * (2 - sps.frame_mbs_only_flag);
  }
}

H264Parse::H264Parse(/* args */) {
  h264Contex = std::make_shared<H264ContextSyntax>();
  curUnit = std::make_shared<H264NalUnit>();
}

H264Parse::~H264Parse() {}

bool H264Parse::parse(const uint8_t *data, int32_t size, bool bFindStartCode) {
  curUnit->nal = H264NAL::NAL_NULL;
  // 有头，可能是annexb格式,也可能是avcc格式
  if (bFindStartCode) {
    int32_t prefixSize = 4;
    bool bvcc = checkAvccPacket(data, size);
    if (!bvcc) {
      if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        prefixSize = 3;
      }
    }
    br.setData(data + prefixSize, size - prefixSize);
  } else {
    br.setData(data, size);
  }
  uint8_t zeroBit = 0;
  br.u(1, zeroBit);
  if (zeroBit != 0) {
    // assert(zeroBit == 0);
    LOGFLF(LogLevel::warn, "zeroBit != 0");
    return false;
  }
  br.u(2, curUnit->RefIdc);
  uint8_t nal_unit_type = 0;
  br.u(5, nal_unit_type);
  curUnit->nal = (H264NAL)nal_unit_type;
  switch (curUnit->nal) {
  case H264NAL::NAL_SPS: {
    return parseSpsSyntax();
  }
  case H264NAL::NAL_PPS: {
    return parsePpsSyntax();
  }
  case H264NAL::NAL_SEI: {
    if (enableParseSEI) {
      curSei = std::make_shared<H264SeiSyntax>();
      bool result = parseSeiSyntax(*curSei);
      return result;
    }
    return true;
  }
  case H264NAL::NAL_IDR:
  case H264NAL::NAL_B_P: {
    if (enableParseSLICE && h264Contex->sps && h264Contex->pps) {
      curSlice = std::make_shared<H264SliceHeaderSyntax>();
      bool result = parseSliceHeaderSyntax(curUnit, *curSlice);
      return result;
    }
    return false;
  }
  default:
    return true;
  }
  return true;
}

bool H264Parse::parseHrdSyntax(H264HrdSyntax &hrd) {
  // See also : ISO 14496/10(2020) - E.1.2 HRD parameters syntax
  br.ue(hrd.cpb_cnt_minus1);
  {
    // Hint : cpb_cnt_minus1 plus 1 specifies the number of alternative CPB
    // specifications in the bitstream. The value of cpb_cnt_minus1 shall be
    // in the range of 0 to 31, inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        hrd.cpb_cnt_minus1 >= 0 && hrd.cpb_cnt_minus1 < 31,
        "[hrd] cpb_cnt_minus1 out of range", return false);
  }
  br.u(4, hrd.bit_rate_scale);
  br.u(4, hrd.cpb_size_scale);
  {
    hrd.bit_rate_value_minus1.resize(hrd.cpb_cnt_minus1 + 1);
    hrd.cpb_size_value_minus1.resize(hrd.cpb_cnt_minus1 + 1);
    hrd.cbr_flag.resize(hrd.cpb_cnt_minus1 + 1);
  }
  for (uint32_t SchedSelIdx = 0; SchedSelIdx <= hrd.cpb_cnt_minus1;
       SchedSelIdx++) {
    br.ue(hrd.bit_rate_value_minus1[SchedSelIdx]);
    br.ue(hrd.cpb_size_value_minus1[SchedSelIdx]);
    br.u(1, hrd.cbr_flag[SchedSelIdx]);
  }
  br.u(5, hrd.initial_cpb_removal_delay_length_minus1);
  br.u(5, hrd.cpb_removal_delay_length_minus1);
  br.u(5, hrd.dpb_output_delay_length_minus1);
  br.u(5, hrd.time_offset_length);
  return true;
}

bool H264Parse::parseVuiSyntax(H264VuiSyntax &vui) {
  // Table E-1 – Meaning of sample aspect ratio indicator
  constexpr uint8_t Extended_SAR_ = 255;
  // See also : ISO 14496/10(2020) - E.1.1 VUI parameters syntax
  br.u(1, vui.aspect_ratio_info_present_flag);
  if (vui.aspect_ratio_info_present_flag) {
    br.u(8, vui.aspect_ratio_idc);
    if (vui.aspect_ratio_idc >= 1 && vui.aspect_ratio_idc <= 16) {
      int32_t sspectIndex = vui.aspect_ratio_idc;
      vui.sar_width = atFfH264PixelSspect[sspectIndex].x;
      vui.sar_height = atFfH264PixelSspect[sspectIndex].y;
    } else if (vui.aspect_ratio_idc == Extended_SAR_) {
      br.u(16, vui.sar_width);
      br.u(16, vui.sar_height);
    }
  } else {
    // Reference : FFmpeg 6.x
    vui.sar_width = 0;
    vui.sar_height = 1;
  }
  br.u(1, vui.overscan_info_present_flag);
  if (vui.overscan_info_present_flag) {
    br.u(1, vui.overscan_appropriate_flag);
  }
  br.u(1, vui.video_signal_type_present_flag);
  if (vui.video_signal_type_present_flag) {
    br.u(3, vui.video_format);
    br.u(1, vui.video_full_range_flag);
    br.u(1, vui.colour_description_present_flag);
    if (vui.colour_description_present_flag) {
      br.u(8, vui.colour_primaries);
      br.u(8, vui.transfer_characteristics);
      br.u(8, vui.matrix_coefficients);
    } else {
      // See also : Table E-5 – Matrix coefficients interpretation using
      // matrix_coefficients syntax elemen
      vui.transfer_characteristics = 2; /* Unspecified */
    }
  }
  br.u(1, vui.chroma_location_info_present_flag);
  if (vui.chroma_location_info_present_flag) {
    br.ue(vui.chroma_sample_loc_type_top_field);
    br.ue(vui.chroma_sample_loc_type_bottom_field);
  }
  br.u(1, vui.timing_info_present_flag);
  if (vui.timing_info_present_flag) {
    br.u(32, vui.num_units_in_tick);
    br.u(32, vui.time_scale);
    // Reference : FFmpeg 6.x
    if (!vui.num_units_in_tick || !vui.time_scale) {
      vui.timing_info_present_flag = 0;
    }
    br.u(1, vui.fixed_frame_rate_flag);
  }
  br.u(1, vui.nal_hrd_parameters_present_flag);
  if (vui.nal_hrd_parameters_present_flag) {
    if (!parseHrdSyntax(vui.nal_hrd_parameters)) {
      return false;
    }
  }
  br.u(1, vui.vcl_hrd_parameters_present_flag);
  if (vui.vcl_hrd_parameters_present_flag) {
    if (!parseHrdSyntax(vui.vcl_hrd_parameters)) {
      return false;
    }
  }
  if (vui.nal_hrd_parameters_present_flag ||
      vui.vcl_hrd_parameters_present_flag) {
    br.u(1, vui.low_delay_hrd_flag);
  }
  br.u(1, vui.pic_struct_present_flag);
  br.u(1, vui.bitstream_restriction_flag);
  if (vui.bitstream_restriction_flag) {
    br.u(1, vui.motion_vectors_over_pic_boundaries_flag);
    br.ue(vui.max_bytes_per_pic_denom);
    br.ue(vui.max_bits_per_mb_denom);
    br.ue(vui.log2_max_mv_length_horizontal);
    br.ue(vui.log2_max_mv_length_vertical);
    br.ue(vui.num_reorder_frames);
    br.ue(vui.max_dec_frame_buffering);
  }
  return true;
}

bool H264Parse::parseSeiSyntax(H264SeiSyntax &sei) {
  // See also : ISO 14496/10(2020) - 7.3.2.3.1 Supplemental enhancement
  // information message syntax

  uint8_t ff_byte = 0;
  do {
    br.u(8, ff_byte);
    sei.payloadType += ff_byte;
  } while (ff_byte == 0xFF);

  do {
    br.u(8, ff_byte);
    sei.payloadSize += ff_byte;
  } while (ff_byte == 0xFF);

  switch (sei.payloadType) {
  // See also : ISO 14496/10(2020) - D.1.1 General SEI message syntax
  case H264SeiType::MMP_H264_SEI_BUFFERING_PERIOD: {
    if (!parseSeiBufferPeriodSyntax(sei.bp)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_PIC_TIMING: {
    if (!parseSeiPictureTimingSyntax(h264Contex->sps->vui_seq_parameters,
                                     sei.pt)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_USER_DATA_REGISTERED_ITU_T_T35: {
    if (!parseSeiUserDataRegisteredSyntax((uint32_t)sei.payloadSize, sei.udr)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_USER_DATA_UNREGISTERED: {
    if (!parseSeiUserDataUnregisteredSyntax((uint32_t)sei.payloadSize,
                                            sei.udn)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_RECOVERY_POINT: {
    if (!parseSeiRecoveryPointSyntax(sei.rp)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_CONTENT_LIGHT_LEVEL_INFO: {
    if (!parseSeiContentLigntLevelInfoSyntax(sei.clli)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_DISPLAY_ORIENTATION: {
    if (!parseSeiDisplayOrientationSyntax(sei.dot)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_FILM_GRAIN_CHARACTERISTICS: {
    if (!parseSeiFilmGrainSyntax(sei.fg)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_FRAME_PACKING_ARRANGEMENT: {
    if (!parseSeiFramePackingArrangementSyntax(sei.fpa)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_ALTERNATIVE_TRANSFER_CHARACTERISTICS: {
    if (!parseSeiAlternativeTransferCharacteristicsSyntax(sei.atc)) {
      return false;
    }
    break;
  }
  case H264SeiType::MP_H264_SEI_AMBIENT_VIEWING_ENVIRONMENT: {
    if (!parseAmbientViewingEnvironmentSyntax(sei.awe)) {
      return false;
    }
    break;
  }
  case H264SeiType::MMP_H264_SEI_MASTERING_DISPLAY_COLOUR_VOLUME: {
    if (!parseSeiMasteringDisplayColourVolumeSyntax(sei.mpvc)) {
      return false;
    }
    break;
  }
  default:
    br.skip((size_t)(sei.payloadSize * 8));
    break;
  }
  return true;
}

bool H264Parse::parseSpsSyntax() {
  // See also : ISO 14496/10(2020) - 7.3.2.1.1 Sequence parameter set data
  // syntax
  H264SpsSyntax sps = {};
  uint8_t reserved_zero_2bits = 0;
  br.u(8, sps.profile_idc);
  br.u(1, sps.constraint_set0_flag);
  br.u(1, sps.constraint_set1_flag);
  br.u(1, sps.constraint_set2_flag);
  br.u(1, sps.constraint_set3_flag);
  br.u(1, sps.constraint_set4_flag);
  br.u(1, sps.constraint_set5_flag);
  br.u(2, reserved_zero_2bits);
  br.u(8, sps.level_idc);
  br.ue(sps.seq_parameter_set_id);
  if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
      sps.profile_idc == 122 || sps.profile_idc == 244 ||
      sps.profile_idc == 44 || sps.profile_idc == 83 || sps.profile_idc == 86 ||
      sps.profile_idc == 118 || sps.profile_idc == 128 ||
      sps.profile_idc == 138 || sps.profile_idc == 139 ||
      sps.profile_idc == 134 || sps.profile_idc == 135) {
    br.ue(sps.chroma_format_idc);
    MPP_H26X_SYNTAXT_STRICT_CHECK(!(sps.chroma_format_idc > 3),
                                  "[sps] invalid chroma_format_idc",
                                  return false);
    if (sps.chroma_format_idc == H264ChromaFormat::MMP_H264_CHROMA_444) {
      br.u(1, sps.separate_colour_plane_flag);
      MPP_H26X_SYNTAXT_NORMAL_CHECK(
          sps.separate_colour_plane_flag,
          "[sps] separate_colour_plane_flag are not supported", return false);
    }
    br.ue(sps.bit_depth_luma_minus8);
    br.ue(sps.bit_depth_chroma_minus8);
    br.u(1, sps.qpprime_y_zero_transform_bypass_flag);
    br.u(1, sps.seq_scaling_matrix_present_flag);
    if (sps.seq_scaling_matrix_present_flag) {
      // TODO : UseDefaultScalingMatrix4x4Flag and
      // UseDefaultScalingMatrix8x8Flag
      // 不支持
      return false;
      int32_t loopTime =
          (sps.chroma_format_idc != H264ChromaFormat::MMP_H264_CHROMA_444) ? 8
                                                                           : 12;
      sps.seq_scaling_list_present_flag.resize(loopTime);
      sps.ScalingList4x4.resize(6);
      sps.UseDefaultScalingMatrix4x4Flag.resize(6);
      sps.ScalingList8x8.resize(loopTime - 6);
      sps.UseDefaultScalingMatrix8x8Flag.resize(loopTime - 6);
      for (int32_t i = 0; i < loopTime; i++) {
        br.u(1, sps.seq_scaling_list_present_flag[i]);
        if (sps.seq_scaling_list_present_flag[i]) {
          if (i < 6) {
            if (!parseScalingListSyntax(
                    sps.ScalingList4x4[i], 16,
                    sps.UseDefaultScalingMatrix4x4Flag[i])) {
              return false;
            }
            if (sps.UseDefaultScalingMatrix4x4Flag[i] == 1) {
              if (i < 3) {
                sps.ScalingList4x4[i] = Default_4x4_Intra;
              } else {
                sps.ScalingList4x4[i] = Default_4x4_Inter;
              }
            }
          } else {
            if (!parseScalingListSyntax(
                    sps.ScalingList8x8[i - 6], 64,
                    sps.UseDefaultScalingMatrix8x8Flag[i])) {
              return false;
            }
            if (sps.UseDefaultScalingMatrix8x8Flag[i] == 1) {
              if (i % 2 == 0) {
                sps.ScalingList8x8[i - 6] = Default_8x8_Intra;
              } else {
                sps.ScalingList8x8[i - 6] = Default_8x8_Inter;
              }
            }
          }
        } else {
          switch (i) {
          // 4x4
          case 0: {
            sps.ScalingList4x4[i] = Default_4x4_Intra;
            break;
          }
          case 3: {
            sps.ScalingList4x4[i] = Default_4x4_Inter;
            break;
          }
          case 1:
          case 2:
          case 4:
          case 5: {
            sps.ScalingList4x4[i] = sps.ScalingList4x4[i - 1];
            break;
          }
          // 8x8
          case 6: {
            sps.ScalingList8x8[i] = Default_8x8_Intra;
            break;
          }
          case 7: {
            sps.ScalingList8x8[i] = Default_8x8_Inter;
            break;
          }
          case 8:
          case 9:
          case 10:
          case 11: {
            sps.ScalingList8x8[i] = sps.ScalingList8x8[i - 2];
            break;
          }
          default: {
            assert(false);
          }
          }
        }
      }
    }
  } else {
    sps.chroma_format_idc = 1;
  }
  if (!sps.seq_scaling_matrix_present_flag) {
    sps.separate_colour_plane_flag = 0;
    sps.bit_depth_luma_minus8 = 0;
    sps.bit_depth_chroma_minus8 = 0;
    // (7-8)
    // Flat_4x4_16[ k ] = 16, with k = 0..15
    {
      sps.ScalingList4x4.resize(6); /* 0..5 */
      for (size_t i = 0; i < 6; i++) {
        sps.ScalingList4x4[i].resize(16);
        for (size_t j = 0; j < 16; j++) {
          sps.ScalingList4x4[i][j] = 16;
        }
      }
    }
    // (7-9)
    // Flat_8x8_16[ k ] = 16, with k = 0..63
    {
      sps.ScalingList8x8.resize(6); /* 6..11 */
      for (size_t i = 0; i < 6; i++) {
        sps.ScalingList8x8[i].resize(64);
        for (size_t j = 0; j < 64; j++) {
          sps.ScalingList8x8[i][j] = 16;
        }
      }
    }
  }
  br.ue(sps.log2_max_frame_num_minus4);
  {
    // Hint : The value of log2_max_frame_num_minus4 shall be in the range of
    // 0 to 12, inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        /* sps.log2_max_frame_num_minus4>=0 && */ sps
                .log2_max_frame_num_minus4 <= 12,
        "[sps] log2_max_frame_num_minus4 out of range", return false);
  }
  br.ue(sps.pic_order_cnt_type);
  {
    // Hint : The value of pic_order_cnt_type shall be in the range of 0 to 2,
    // inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        /* sps.pic_order_cnt_type >= 0 && */ sps.pic_order_cnt_type <= 2,
        "[sps] pic_order_cnt_type out of range", return false);
  }
  if (sps.pic_order_cnt_type == 0) {
    br.ue(sps.log2_max_pic_order_cnt_lsb_minus4);
    // Hint : The value of log2_max_pic_order_cnt_lsb_minus4 shall be in the
    // range of 0 to 12, inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        /* sps.log2_max_pic_order_cnt_lsb_minus4 >= 0 && */ sps
                .log2_max_pic_order_cnt_lsb_minus4 <= 12,
        "[sps] log2_max_pic_order_cnt_lsb_minus4 out of range", return false);
  } else if (sps.pic_order_cnt_type == 1) {
    br.u(1, sps.delta_pic_order_always_zero_flag);
    br.se(sps.offset_for_non_ref_pic);
    br.se(sps.offset_for_top_to_bottom_field);
    br.ue(sps.num_ref_frames_in_pic_order_cnt_cycle);
    sps.offset_for_ref_frame.resize(sps.num_ref_frames_in_pic_order_cnt_cycle +
                                    1);
    for (uint32_t i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++) {
      br.se(sps.offset_for_ref_frame[i]);
    }
  }
  br.ue(sps.max_num_ref_frames);
  br.u(1, sps.gaps_in_frame_num_value_allowed_flag);
  br.ue(sps.pic_width_in_mbs_minus1);
  br.ue(sps.pic_height_in_map_units_minus1);
  br.u(1, sps.frame_mbs_only_flag);
  if (!sps.frame_mbs_only_flag) {
    br.u(1, sps.mb_adaptive_frame_field_flag);
  }
  br.u(1, sps.direct_8x8_inference_flag);
  br.u(1, sps.frame_cropping_flag);
  if (sps.frame_cropping_flag) {
    br.ue(sps.frame_crop_left_offset);
    br.ue(sps.frame_crop_right_offset);
    br.ue(sps.frame_crop_top_offset);
    br.ue(sps.frame_crop_bottom_offset);
  }
  br.u(1, sps.vui_parameters_present_flag);
  if (sps.vui_parameters_present_flag) {
    if (!parseVuiSyntax(sps.vui_seq_parameters)) {
      return false;
    }
  }
  FillH264SpsContext(sps);
  h264Contex->spsSet[sps.seq_parameter_set_id] =
      std::make_shared<H264SpsSyntax>(sps);
  h264Contex->sps = h264Contex->spsSet[sps.seq_parameter_set_id];
  return true;
}

bool H264Parse::parseSliceHeaderSyntax(H264NalUnitPtr unit,
                                       H264SliceHeaderSyntax &slice) {
  // See aslo : ISO 14496/10(2020) - 7.3.3 Slice header syntax
  size_t begin = br.curBits();
  bool IdrPicFlag = unit->nal == H264NAL::NAL_IDR ? true : false;
  br.ue(slice.first_mb_in_slice);
  br.ue(slice.slice_type);
  slice.slice_type =
      slice.slice_type % 5; // See aslo : ISO 14496/10(2020) - Table 7-6 –
                            // Name association to slice_type
  MPP_H26X_SYNTAXT_STRICT_CHECK(
      !(IdrPicFlag && slice.slice_type != H264SliceType::MMP_H264_I_SLICE),
      "[slice] A non-intra slice in an IDR NAL unit->", return false);
  br.ue(slice.pic_parameter_set_id);
  MPP_H26X_SYNTAXT_STRICT_CHECK(
      h264Contex->ppsSet.count(slice.pic_parameter_set_id),
      "[slice] missing pps", return false);
  H264PpsPtr pps = h264Contex->ppsSet[slice.pic_parameter_set_id];
  MPP_H26X_SYNTAXT_STRICT_CHECK(
      h264Contex->spsSet.count(pps->seq_parameter_set_id),
      "[slice] missing sps", return false);
  H264SpsPtr sps = h264Contex->spsSet[pps->seq_parameter_set_id];
  if (sps->separate_colour_plane_flag == 1) {
    br.u(2, slice.colour_plane_id);
  }
  // Hint : frame_num is used as an identifier for pictures and shall be
  // represented by log2_max_frame_num_minus4 + 4 bits in the bitstream.
  br.u(sps->log2_max_frame_num_minus4 + 4, slice.frame_num);
  if (!sps->frame_mbs_only_flag) {
    br.u(1, slice.field_pic_flag);
    if (slice.field_pic_flag) {
      br.u(1, slice.bottom_field_flag);
    }
  }
  if (IdrPicFlag) {
    br.ue(slice.idr_pic_id);
    {
      // Hint :
      // idr_pic_id identifies an IDR picture. The values of idr_pic_id in all
      // the slices of an IDR picture shall remain unchanged. When two
      // consecutive access units in decoding order are both IDR access units,
      // the value of idr_pic_id in the slices of the first such IDR access
      // unit shall differ from the idr_pic_id in the second such IDR access
      // unit-> The value of idr_pic_id shall be in the range of 0 to 65535,
      // inclusive.
      MPP_H26X_SYNTAXT_STRICT_CHECK(
          /* slice.idr_pic_id >= 0 && */ slice.idr_pic_id <= 65535,
          "[slice] idr_pic_id out of range", return false);
    }
  }
  if (sps->pic_order_cnt_type == 0) {
    // Hint : The length of the pic_order_cnt_lsb syntax element is
    // log2_max_pic_order_cnt_lsb_minus4 + 4 bits.
    br.u(sps->log2_max_pic_order_cnt_lsb_minus4 + 4, slice.pic_order_cnt_lsb);
    if (pps->bottom_field_pic_order_in_frame_present_flag &&
        !slice.field_pic_flag) {
      br.se(slice.delta_pic_order_cnt_bottom);
    }
  }
  if (sps->pic_order_cnt_type == 1 && !sps->delta_pic_order_always_zero_flag) {
    br.se(slice.delta_pic_order_cnt[0]);
    if (pps->bottom_field_pic_order_in_frame_present_flag &&
        !slice.field_pic_flag) {
      br.se(slice.delta_pic_order_cnt[1]);
    }
  }
  if (pps->redundant_pic_cnt_present_flag) {
    br.se(slice.redundant_pic_cnt);
  }
  if (slice.slice_type == H264SliceType::MMP_H264_B_SLICE) {
    br.u(1, slice.direct_spatial_mv_pred_flag);
  }
  if (slice.slice_type == H264SliceType::MMP_H264_P_SLICE ||
      slice.slice_type == H264SliceType::MMP_H264_SP_SLICE ||
      slice.slice_type == H264SliceType::MMP_H264_B_SLICE) {
    br.u(1, slice.num_ref_idx_active_override_flag);
    if (slice.num_ref_idx_active_override_flag) {
      br.ue(slice.num_ref_idx_l0_active_minus1);
      if (slice.slice_type == H264SliceType::MMP_H264_B_SLICE) {
        br.ue(slice.num_ref_idx_l1_active_minus1);
      }
    } else /* if (slice.num_ref_idx_active_override_flag == 0) */
    {
      slice.num_ref_idx_l0_active_minus1 =
          pps->num_ref_idx_l0_default_active_minus1;
      if (slice.slice_type == H264SliceType::MMP_H264_B_SLICE) {
        slice.num_ref_idx_l1_active_minus1 =
            pps->num_ref_idx_l1_default_active_minus1;
      }
    }
  }
  if ((uint8_t)unit->nal == 20 || (uint8_t)unit->nal == 21) {
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        false, "[slice] not support ref_pic_list_mvc_modification() feature",
        return false);
  } else {
    if (!parseReferencePictureListModificationSyntax(slice, slice.rplm)) {
      return false;
    }
  }
  if ((pps->weighted_pred_flag &&
       (slice.slice_type == H264SliceType::MMP_H264_P_SLICE ||
        slice.slice_type == H264SliceType::MMP_H264_SP_SLICE)) ||
      (pps->weighted_bipred_idc == 1 &&
       slice.slice_type == H264SliceType::MMP_H264_B_SLICE)) {
    if (!parsePredictionWeightTableSyntax(*sps, slice, slice.pwt)) {
      return false;
    }
  }
  if (unit->RefIdc != 0) {
    if (!parseDecodedReferencePictureMarkingSyntax(unit, slice.drpm)) {
      return false;
    }
  }
  if (pps->entropy_coding_mode_flag &&
      slice.slice_type != H264SliceType::MMP_H264_I_SLICE &&
      slice.slice_type != H264SliceType::MMP_H264_SI_SLICE) {
    br.ue(slice.cabac_init_idc);
  }
  br.se(slice.slice_qp_delta);
  if (slice.slice_type == H264SliceType::MMP_H264_SP_SLICE ||
      slice.slice_type == H264SliceType::MMP_H264_SI_SLICE) {
    if (slice.slice_type == H264SliceType::MMP_H264_SP_SLICE) {
      br.u(1, slice.sp_for_switch_flag);
    }
    br.se(slice.slice_qs_delta);
  }
  if (pps->deblocking_filter_control_present_flag) {
    br.ue(slice.disable_deblocking_filter_idc);
    if (slice.disable_deblocking_filter_idc != 1) {
      br.se(slice.slice_alpha_c0_offset_div2);
      br.se(slice.slice_beta_offset_div2);
    }
  }
  if (pps->num_slice_groups_minus1 > 0 && pps->slice_group_map_type >= 3 &&
      pps->slice_group_map_type <= 5) {
    br.u(2, slice.slice_group_change_cycle);
  }
  slice.slice_data_bit_offset = (uint16_t)(br.curBits() - begin);
  return true;
}

bool H264Parse::parseDecodedReferencePictureMarkingSyntax(
    H264NalUnitPtr unit, H264DecodedReferencePictureMarkingSyntax &drpm) {
  // See also : ISO 14496/10(2020) - 7.3.3.3 Decoded reference picture marking
  // syntax

  bool IdrPicFlag = unit->nal == H264NAL::NAL_IDR ? true : false;
  if (IdrPicFlag) {
    br.u(1, drpm.no_output_of_prior_pics_flag);
    br.u(1, drpm.long_term_reference_flag);
  } else {
    // See also : ISO 14496/10(2020) - Table 7-9 – Memory management control
    // operation (memory_management_control_operation) values
    br.u(1, drpm.adaptive_ref_pic_marking_mode_flag);
    if (drpm.adaptive_ref_pic_marking_mode_flag) {
      uint32_t memory_management_control_operation = 0;
      do {
        br.ue(memory_management_control_operation);
        if (memory_management_control_operation == 1 ||
            memory_management_control_operation == 3) {
          H264DecodedReferencePictureMarkingSyntax::
              memory_management_control_operations_data data;
          br.ue(data.difference_of_pic_nums_minus1);
          drpm.memory_management_control_operations_datas.push_back(data);
        }
        if (memory_management_control_operation == 2) {
          H264DecodedReferencePictureMarkingSyntax::
              memory_management_control_operations_data data;
          br.ue(data.long_term_pic_num);
          drpm.memory_management_control_operations_datas.push_back(data);
        }
        if (memory_management_control_operation == 3 ||
            memory_management_control_operation == 6) {
          H264DecodedReferencePictureMarkingSyntax::
              memory_management_control_operations_data data;
          br.ue(data.long_term_frame_idx);
          drpm.memory_management_control_operations_datas.push_back(data);
        }
        if (memory_management_control_operation == 4) {
          H264DecodedReferencePictureMarkingSyntax::
              memory_management_control_operations_data data;
          br.ue(data.max_long_term_frame_idx_plus1);
          drpm.memory_management_control_operations_datas.push_back(data);
        }
        drpm.memory_management_control_operations.push_back(
            memory_management_control_operation);
      } while (memory_management_control_operation != 0);
    }
  }
  return true;
}

bool H264Parse::parseSubSpsSyntax(H264SpsSyntax &sps,
                                  H264SubSpsSyntax &subSps) {
  // See also : ISO 14496/10(2020) - 7.3.2.1.3 Subset sequence parameter set
  // RBSP syntax
  if (!parseSpsSyntax()) {
    return false;
  }
  if (sps.profile_idc == 83 || sps.profile_idc == 86) {
    // TODO
    // See aslo : ISO 14496/10(2020) - F.3.3.2.1.4 Sequence parameter set SVC
    // extension syntax
    assert(false);
    return false;
  } else if (sps.profile_idc == 118 || sps.profile_idc == 128 ||
             sps.profile_idc == 134) {
    br.u(1, subSps.bit_equal_to_one);

    if (!parseSpsMvcSyntax(subSps.mvc)) {
      return false;
    }
    br.u(1, subSps.mvc_vui_parameters_present_flag);
    if (subSps.mvc_vui_parameters_present_flag) {
      if (!parseMvcVuiSyntax(subSps.mvcVui)) {
        return false;
      }
    }
    assert(false);
    return false;
  } else if (sps.profile_idc == 138 || sps.profile_idc == 135) {
    // TODO
    // See aslo : ISO 14496/10(2020) - H.3.3.2.1.5 Sequence parameter set MVCD
    // extension syntax
    assert(false);
    return false;
  } else if (sps.profile_idc == 139) {
    // TODO
    // See aslo : ISO 14496/10(2020) - H.3.3.2.1.5 Sequence parameter set MVCD
    // extension syntax
    //                                 I.3.3.2.1.5 Sequence parameter set MVCD
    //                                 extension syntax
    assert(false);
    return false;
  }
  br.u(1, subSps.additional_extension2_flag);
  // TODO
  return true;
}

bool H264Parse::parseSpsMvcSyntax(H264SpsMvcSyntax &mvc) {
  // See also : ISO 14496/10(2020) - G.3.3.2.1.4 Sequence parameter set MVC
  // extension syntax

  br.ue(mvc.num_views_minus1);
  mvc.view_id.resize(mvc.num_views_minus1 + 1);
  for (uint32_t i = 0; i <= mvc.num_views_minus1; i++) {
    br.ue(mvc.view_id[i]);
  }
  mvc.num_anchor_refs_l0.resize(mvc.num_views_minus1 + 1);
  mvc.anchor_ref_l0.resize(mvc.num_views_minus1 + 1);
  mvc.num_anchor_refs_l1.resize(mvc.num_views_minus1 + 1);
  mvc.anchor_ref_l1.resize(mvc.num_views_minus1 + 1);
  for (uint32_t i = 1; i <= mvc.num_views_minus1; i++) {
    br.ue(mvc.num_anchor_refs_l0[i]);
    mvc.anchor_ref_l0[i].resize(mvc.num_anchor_refs_l0[i] + 1);
    for (uint32_t j = 0; j < mvc.num_anchor_refs_l0[i]; j++) {
      br.ue(mvc.anchor_ref_l0[i][j]);
    }
    br.ue(mvc.num_anchor_refs_l1[i]);
    mvc.anchor_ref_l1[i].resize(mvc.num_anchor_refs_l1[i] + 1);
    for (int j = 0; i < mvc.num_anchor_refs_l1[i]; j++) {
      br.ue(mvc.anchor_ref_l1[i][j]);
    }
  }
  mvc.num_non_anchor_refs_l0.resize(mvc.num_views_minus1 + 1);
  mvc.non_anchor_ref_l0.resize(mvc.num_views_minus1 + 1);
  mvc.num_non_anchor_refs_l1.resize(mvc.num_views_minus1 + 1);
  mvc.non_anchor_ref_l1.resize(mvc.num_views_minus1 + 1);
  for (uint32_t i = 0; i <= mvc.num_views_minus1; i++) {
    br.ue(mvc.num_non_anchor_refs_l0[i]);
    mvc.non_anchor_ref_l0[i].resize(mvc.num_non_anchor_refs_l0[i] + 1);
    for (uint32_t j = 0; j < mvc.num_non_anchor_refs_l0[i]; j++) {
      br.ue(mvc.non_anchor_ref_l0[i][j]);
    }
    br.ue(mvc.num_non_anchor_refs_l1[i]);
    mvc.non_anchor_ref_l1[i].resize(mvc.num_non_anchor_refs_l1[i] + 1);
    for (int j = 0; i < mvc.num_non_anchor_refs_l1[i]; j++) {
      br.ue(mvc.non_anchor_ref_l1[i][j]);
    }
  }
  br.ue(mvc.num_level_values_signalled_minus1);
  mvc.level_idc.resize(mvc.num_level_values_signalled_minus1 + 1);
  mvc.num_applicable_ops_minus1.resize(mvc.num_level_values_signalled_minus1 +
                                       1);
  mvc.applicable_op_temporal_id.resize(mvc.num_level_values_signalled_minus1 +
                                       1);
  mvc.applicable_op_num_target_views_minus1.resize(
      mvc.num_level_values_signalled_minus1 + 1);
  mvc.applicable_op_target_view_id.resize(
      mvc.num_level_values_signalled_minus1 + 1);
  mvc.applicable_op_num_views_minus1.resize(
      mvc.num_level_values_signalled_minus1 + 1);
  for (uint32_t i = 0; i <= mvc.num_level_values_signalled_minus1; i++) {
    br.u(8, mvc.level_idc[i]);
    br.ue(mvc.num_applicable_ops_minus1[i]);
    mvc.applicable_op_temporal_id[i].resize(mvc.num_applicable_ops_minus1[i] +
                                            1);
    mvc.applicable_op_num_target_views_minus1[i].resize(
        mvc.num_applicable_ops_minus1[i] + 1);
    mvc.applicable_op_target_view_id[i].resize(
        mvc.num_applicable_ops_minus1[i] + 1);
    mvc.applicable_op_num_views_minus1[i].resize(
        mvc.num_applicable_ops_minus1[i] + 1);
    for (uint32_t j = 0; j <= mvc.num_applicable_ops_minus1[i]; j++) {
      br.u(3, mvc.applicable_op_temporal_id[i][j]);
      br.ue(mvc.applicable_op_num_target_views_minus1[i][j]);
      mvc.applicable_op_target_view_id[i][j].resize(
          mvc.applicable_op_num_target_views_minus1[i][j] + 1);
      for (uint32_t k = 0; k <= mvc.applicable_op_num_target_views_minus1[i][j];
           k++) {
        br.ue(mvc.applicable_op_target_view_id[i][j][k]);
      }
      br.ue(mvc.applicable_op_num_views_minus1[i][j]);
    }
  }
  return true;
}

bool H264Parse::parseMvcVuiSyntax(H264MvcVuiSyntax &mvcVui) {
  // See also : ISO 14496/10(2020) - G.10.1 MVC VUI parameters extension syntax

  br.ue(mvcVui.vui_mvc_num_ops_minus1);
  mvcVui.vui_mvc_temporal_id.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_num_target_output_views_minus1.resize(
      mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_view_id.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_timing_info_present_flag.resize(mvcVui.vui_mvc_num_ops_minus1 +
                                                 1);
  mvcVui.vui_mvc_num_units_in_tick.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_time_scale.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_fixed_frame_rate_flag.resize(mvcVui.vui_mvc_num_ops_minus1 +
                                              1);
  mvcVui.vui_mvc_nal_hrd_parameters_present_flag.resize(
      mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.nalHrds.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_vcl_hrd_parameters_present_flag.resize(
      mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vclHrds.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_low_delay_hrd_flag.resize(mvcVui.vui_mvc_num_ops_minus1 + 1);
  mvcVui.vui_mvc_pic_struct_present_flag.resize(mvcVui.vui_mvc_num_ops_minus1 +
                                                1);
  for (uint32_t i = 0; i <= mvcVui.vui_mvc_num_ops_minus1; i++) {
    br.u(3, mvcVui.vui_mvc_temporal_id[i]);
    br.ue(mvcVui.vui_mvc_num_target_output_views_minus1[i]);
    mvcVui.vui_mvc_view_id[i].resize(
        mvcVui.vui_mvc_num_target_output_views_minus1[i] + 1);
    for (uint32_t j = 0; j <= mvcVui.vui_mvc_num_target_output_views_minus1[i];
         j++) {
      br.ue(mvcVui.vui_mvc_view_id[i][j]);
    }
    br.u(1, mvcVui.vui_mvc_timing_info_present_flag[i]);
    if (mvcVui.vui_mvc_timing_info_present_flag[i]) {
      br.u(32, mvcVui.vui_mvc_num_units_in_tick[i]);
      br.u(32, mvcVui.vui_mvc_time_scale[i]);
      br.u(1, mvcVui.vui_mvc_fixed_frame_rate_flag[i]);
    }
    br.u(1, mvcVui.vui_mvc_nal_hrd_parameters_present_flag[i]);
    if (mvcVui.vui_mvc_nal_hrd_parameters_present_flag[i]) {
      if (!parseHrdSyntax(mvcVui.nalHrds[i])) {
        return false;
      }
    }
    br.u(1, mvcVui.vui_mvc_vcl_hrd_parameters_present_flag[i]);
    if (mvcVui.vui_mvc_vcl_hrd_parameters_present_flag[i]) {
      if (!parseHrdSyntax(mvcVui.vclHrds[i])) {
        return false;
      }
    }
    if (mvcVui.vui_mvc_nal_hrd_parameters_present_flag[i] ||
        mvcVui.vui_mvc_vcl_hrd_parameters_present_flag[i]) {
      br.u(1, mvcVui.vui_mvc_low_delay_hrd_flag[i]);
    }
    br.u(1, mvcVui.vui_mvc_pic_struct_present_flag[i]);
  }
  return true;
}

bool H264Parse::parsePpsSyntax() {
  // See aslo : ISO 14496/10(2020) - 7.3.2.2 Picture parameter set RBSP syntax
  H264PpsSyntax pps = {};
  br.ue(pps.pic_parameter_set_id);
  MPP_H26X_SYNTAXT_STRICT_CHECK(
      pps.pic_parameter_set_id >= 0 && pps.pic_parameter_set_id <= 255,
      "[sps] pic_parameter_set_id out of range", return false);
  br.ue(pps.seq_parameter_set_id);
  MPP_H26X_SYNTAXT_STRICT_CHECK(
      pps.seq_parameter_set_id >= 0 && pps.seq_parameter_set_id <= 32,
      "[sps] seq_parameter_set_id out of range", return false);
  if (h264Contex->spsSet.count(pps.seq_parameter_set_id) == 0) {
    // assert(false);
    return false;
  }
  H264SpsPtr sps = h264Contex->spsSet[pps.seq_parameter_set_id];
  br.u(1, pps.entropy_coding_mode_flag);
  br.u(1, pps.bottom_field_pic_order_in_frame_present_flag);
  br.ue(pps.num_slice_groups_minus1);
  if (pps.num_slice_groups_minus1) {
    br.ue(pps.slice_group_map_type);
    // Reference : FFmpeg 6.x
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        pps.slice_group_map_type > 0,
        "[sps] not support slice_group_map_type, missing feature",
        return false);
  }
  br.ue(pps.num_ref_idx_l0_default_active_minus1);
  br.ue(pps.num_ref_idx_l1_default_active_minus1);
  {
    // Hint :
    // num_ref_idx_l0_default_active_minus1
    //   num_ref_idx_l0_default_active_minus1 specifies how
    //   num_ref_idx_l0_active_minus1 is inferred for P, SP, and B
    // slices with num_ref_idx_active_override_flag equal to 0. The value of
    // num_ref_idx_l0_default_active_minus1 shall be in the range of 0 to 31,
    // inclusive. num_ref_idx_l1_default_active_minus1
    //   num_ref_idx_l1_default_active_minus1 specifies how
    //   num_ref_idx_l1_active_minus1 is inferred for B slices with
    // num_ref_idx_active_override_flag equal to 0. The value of
    // num_ref_idx_l1_default_active_minus1 shall be in the range of 0 to 31,
    // inclusive.
    //
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        pps.num_ref_idx_l0_default_active_minus1 >= 0 &&
            pps.num_ref_idx_l0_default_active_minus1 <= 31,
        "[sps] num_ref_idx_l0_default_active_minus1 out of range",
        return false);
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        pps.num_ref_idx_l1_default_active_minus1 >= 0 &&
            pps.num_ref_idx_l1_default_active_minus1 <= 31,
        "[sps] num_ref_idx_active_override_flag out of range", return false);
  }
  br.u(1, pps.weighted_pred_flag);
  br.u(2, pps.weighted_bipred_idc);
  br.se(pps.pic_init_qp_minus26);
  br.se(pps.pic_init_qs_minus26);
  br.se(pps.chroma_qp_index_offset);
  {
    // Hint : chroma_qp_index_offset specifies the offset that shall be added
    // to QPY and QSY for addressing the table of QPC values for the Cb chroma
    // component. The value of chroma_qp_index_offset shall be in the range of
    // −12 to +12, inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        pps.chroma_qp_index_offset >= -12 && pps.chroma_qp_index_offset <= 12,
        "[sps] chroma_qp_index_offset out of range", return false);
  }
  br.u(1, pps.deblocking_filter_control_present_flag);
  br.u(1, pps.constrained_intra_pred_flag);
  br.u(1, pps.redundant_pic_cnt_present_flag);
  if (!br.end()) {
    br.u(1, pps.transform_8x8_mode_flag);
    br.u(1, pps.pic_scaling_matrix_present_flag);
    if (pps.pic_scaling_matrix_present_flag) {
      int32_t loopTime =
          ((sps->chroma_format_idc != H264ChromaFormat::MMP_H264_CHROMA_444)
               ? 2
               : 6) *
          pps.transform_8x8_mode_flag;
      pps.pic_scaling_list_present_flag.resize(loopTime);
      pps.ScalingList4x4.resize(6);
      pps.UseDefaultScalingMatrix4x4Flag.resize(6);
      pps.ScalingList8x8.resize(loopTime - 6);
      pps.UseDefaultScalingMatrix8x8Flag.resize(loopTime - 6);
      for (int32_t i = 0; i < loopTime; i++) {
        br.u(1, pps.pic_scaling_list_present_flag[i]);
        if (pps.pic_scaling_list_present_flag[i]) {
          if (i < 6) {
            if (!parseScalingListSyntax(
                    pps.ScalingList4x4[i], 16,
                    pps.UseDefaultScalingMatrix4x4Flag[i])) {
              return false;
            }
            if (pps.UseDefaultScalingMatrix4x4Flag[i] == 1) {
              // TODO
            }
          } else {
            if (!parseScalingListSyntax(
                    pps.ScalingList8x8[i - 6], 64,
                    pps.UseDefaultScalingMatrix8x8Flag[i])) {
              return false;
            }
            if (pps.UseDefaultScalingMatrix8x8Flag[i] == 1) {
              // TODO
            }
          }
        }
      }
    }
    br.se(pps.second_chroma_qp_index_offset);
    {
      // Hint : second_chroma_qp_index_offset specifies the offset that shall
      // be added to QPY and QSY for addressing the table of QPC values for
      // the Cr chroma component. The value of second_chroma_qp_index_offset
      // shall be in the range of −12 to +12, inclusive.
      // 
      //MPP_H26X_SYNTAXT_STRICT_CHECK(
      //    pps.second_chroma_qp_index_offset >= -12 &&
      //        pps.second_chroma_qp_index_offset <= 12,
      //    "[sps] second_chroma_qp_index_offset out of range", return false);
    }
  } else {
    // Hint : When second_chroma_qp_index_offset is not present, it shall be
    // inferred to be equal to chroma_qp_index_offset
    pps.second_chroma_qp_index_offset = pps.chroma_qp_index_offset;
  }
  if (!pps.pic_scaling_matrix_present_flag) {
    pps.ScalingList4x4 = sps->ScalingList4x4;
    pps.ScalingList8x8 = sps->ScalingList8x8;
  }
  h264Contex->ppsSet[pps.pic_parameter_set_id] =
      std::make_shared<H264PpsSyntax>(pps);
  h264Contex->pps = h264Contex->ppsSet[pps.pic_parameter_set_id];
  return true;
}

bool H264Parse::parseNalSvcSyntax(H264NalSvcSyntax &svc) {
  // See also : ISO 14496/10(2020) - F.3.3.1.1 NAL unit header SVC extension
  // syntax
  br.u(1, svc.idr_flag);
  br.u(6, svc.priority_id);
  br.u(1, svc.no_inter_layer_pred_flag);
  br.u(3, svc.dependency_id);
  br.u(4, svc.quality_id);
  br.u(3, svc.temporal_id);
  br.u(1, svc.use_ref_base_pic_flag);
  br.u(1, svc.discardable_flag);
  br.u(1, svc.output_flag);
  br.u(1, svc.reserved_three_2bits);
  return true;
}

bool H264Parse::parseNal3dAvcSyntax(H264Nal3dAvcSyntax &avc) {
  // See also : ISO 14496/10(2020) - I.3.3.1.1 NAL unit header 3D-AVC extension
  // syntax

  br.u(8, avc.view_idx);
  br.u(1, avc.depth_flag);
  br.u(1, avc.non_idr_flag);
  br.u(3, avc.temporal_id);
  br.u(1, avc.anchor_pic_flag);
  br.u(1, avc.inter_view_flag);
  return true;
}

bool H264Parse::parseNalMvcSyntax(H264NalMvcSyntax &mvc) {
  // See also : ISO 14496/10(2020) - I.3.3.1.1 NAL unit header 3D-AVC extension
  // syntax

  br.u(1, mvc.non_idr_flag);
  br.u(6, mvc.priority_id);
  br.u(10, mvc.view_id);
  br.u(1, mvc.temporal_id);
  br.u(1, mvc.anchor_pic_flag);
  br.u(1, mvc.inter_view_flag);
  br.u(1, mvc.reserved_one_bit);
  return true;
}

bool H264Parse::parseScalingListSyntax(std::vector<int32_t> &scalingList,
                                       int32_t sizeOfScalingList,
                                       int32_t &useDefaultScalingMatrixFlag) {
  // See aslo : ISO 14496/10(2020) - 7.3.2.1.1.1 Scaling list syntax

  int32_t lastScale = 8;
  int32_t nextScale = 8;
  int32_t delta_scale = 0;
  scalingList.resize(sizeOfScalingList);
  for (int32_t j = 0; j < sizeOfScalingList; j++) {
    if (nextScale != 0) {
      br.se(delta_scale);
      nextScale = (lastScale + delta_scale + 256) % 256;
      useDefaultScalingMatrixFlag = (j == 0 && nextScale == 0);
    }
    scalingList[j] = (nextScale == 0) ? lastScale : nextScale;
    lastScale = scalingList[j];
  }
  return true;
}

bool H264Parse::parseReferencePictureListModificationSyntax(
    H264SliceHeaderSyntax &slice,
    H264ReferencePictureListModificationSyntax &rplm) {
  // See also : ISO 14496/10(2020) - 7.3.3.1 Reference picture list modification
  // syntax

  if (slice.slice_type != 2 /* MMP_H264_I_SLICE */ &&
      slice.slice_type != 4 /* MMP_H264_SI_SLICE */) {
    br.u(1, rplm.ref_pic_list_modification_flag_l0);
    if (rplm.ref_pic_list_modification_flag_l0) {
      uint32_t modification_of_pic_nums_idc = 0;
      do {
        br.ue(modification_of_pic_nums_idc);
        if (modification_of_pic_nums_idc > 5) {
          return false;
        }
        if (rplm.modification_of_pic_nums_idcs.size() > AVOX_MAX_REFS) {
          break;
        }
        if (modification_of_pic_nums_idc == 0 ||
            modification_of_pic_nums_idc == 1) {
          H264ReferencePictureListModificationSyntax::
              modification_of_pic_nums_idcs_data
                  modification_of_pic_nums_idcs_data;
          br.ue(modification_of_pic_nums_idcs_data.abs_diff_pic_num_minus1);
          rplm.modification_of_pic_nums_idcs_datas.push_back(
              modification_of_pic_nums_idcs_data);
        } else if (modification_of_pic_nums_idc == 2) {
          H264ReferencePictureListModificationSyntax::
              modification_of_pic_nums_idcs_data
                  modification_of_pic_nums_idcs_data;
          br.ue(modification_of_pic_nums_idcs_data.long_term_pic_num);
          rplm.modification_of_pic_nums_idcs_datas.push_back(
              modification_of_pic_nums_idcs_data);
        }
        rplm.modification_of_pic_nums_idcs.push_back(
            modification_of_pic_nums_idc);
      } while (modification_of_pic_nums_idc != 3);
    }
  }
  if (slice.slice_type == 1 /* MMP_H264_B_SLICE */) {
    br.u(1, rplm.ref_pic_list_modification_flag_l1);
    if (rplm.ref_pic_list_modification_flag_l1) {
      uint32_t modification_of_pic_nums_idc = 0;
      do {
        br.ue(modification_of_pic_nums_idc);
        if (modification_of_pic_nums_idc == 0 ||
            modification_of_pic_nums_idc == 1) {
          H264ReferencePictureListModificationSyntax::
              modification_of_pic_nums_idcs_data
                  modification_of_pic_nums_idcs_data;
          br.ue(modification_of_pic_nums_idcs_data.abs_diff_pic_num_minus1);
          rplm.modification_of_pic_nums_idcs_datas.push_back(
              modification_of_pic_nums_idcs_data);
        } else if (modification_of_pic_nums_idc == 2) {
          H264ReferencePictureListModificationSyntax::
              modification_of_pic_nums_idcs_data
                  modification_of_pic_nums_idcs_data;
          br.ue(modification_of_pic_nums_idcs_data.long_term_pic_num);
          rplm.modification_of_pic_nums_idcs_datas.push_back(
              modification_of_pic_nums_idcs_data);
        }
        rplm.modification_of_pic_nums_idcs.push_back(
            modification_of_pic_nums_idc);
      } while (modification_of_pic_nums_idc != 3);
    }
  }
  return true;
}

bool H264Parse::parsePredictionWeightTableSyntax(
    const H264SpsSyntax &sps, H264SliceHeaderSyntax &slice,
    H264PredictionWeightTableSyntax &pwt) {
  // See also : ISO 14496/10(2020) - 7.3.3.2 Prediction weight table syntax

  uint32_t ChromaArrayType =
      sps.separate_colour_plane_flag == 1 ? 0 : sps.chroma_format_idc;
  br.ue(pwt.luma_log2_weight_denom);
  {
    // Hint : luma_log2_weight_denom is the base 2 logarithm of the
    // denominator for all luma weighting factors. The value of
    // luma_log2_weight_denom shall be in the range of 0 to 7, inclusive.
    MPP_H26X_SYNTAXT_STRICT_CHECK(
        pwt.luma_log2_weight_denom >= 0 && pwt.luma_log2_weight_denom <= 7,
        "[pwt] luma_log2_weight_denom out of range", return false);
  }
  if (ChromaArrayType != 0) {
    br.ue(pwt.chroma_log2_weight_denom);
    {
      // Hint : chroma_log2_weight_denom is the base 2 logarithm of the
      // denominator for all chroma weighting factors. The value of
      // chroma_log2_weight_denom shall be in the range of 0 to 7, inclusive.
      MPP_H26X_SYNTAXT_STRICT_CHECK(
          pwt.chroma_log2_weight_denom >= 0 &&
              pwt.chroma_log2_weight_denom <= 7,
          "[pwt] chroma_log2_weight_denom out of range", return false);
    }
  }
  pwt.luma_weight_l0_flag.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  pwt.luma_weight_l0.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  pwt.luma_offset_l0.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  pwt.chroma_weight_l0_flag.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  pwt.chroma_weight_l0.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  pwt.chroma_offset_l0.resize(slice.num_ref_idx_l0_active_minus1 + 1);
  for (uint32_t i = 0; i <= slice.num_ref_idx_l0_active_minus1; i++) {
    br.u(1, pwt.luma_weight_l0_flag[i]);
    if (pwt.luma_weight_l0_flag[i]) {
      br.se(pwt.luma_weight_l0[i]);
      br.se(pwt.luma_offset_l0[i]);
    } else {
      // Hint : When luma_weight_l0_flag is equal to 0, luma_weight_l0[ i ]
      // shall be inferred to be equal to 2^luma_log2_weight_denom for
      // RefPicList0[ i ].
      pwt.luma_weight_l0[i] = 1 << pwt.luma_log2_weight_denom;
    }
    if (ChromaArrayType != 0) {
      br.u(1, pwt.chroma_weight_l0_flag[i]);
      if (pwt.chroma_weight_l0_flag[i]) {
        pwt.chroma_weight_l0[i].resize(2);
        pwt.chroma_offset_l0[i].resize(2);
        for (size_t j = 0; j < 2; j++) {
          br.se(pwt.chroma_weight_l0[i][j]);
          br.se(pwt.chroma_offset_l0[i][j]);
        }
      }
    }
  }
  if (slice.slice_type == 1 /* MMP_H264_B_SLICE */) {
    pwt.luma_weight_l1_flag.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    pwt.luma_weight_l1.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    pwt.luma_offset_l1.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    pwt.chroma_weight_l1_flag.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    pwt.chroma_weight_l1.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    pwt.chroma_offset_l1.resize(slice.num_ref_idx_l1_active_minus1 + 1);
    for (uint32_t i = 0; i <= slice.num_ref_idx_l1_active_minus1; i++) {
      br.u(1, pwt.luma_weight_l1_flag[i]);
      if (pwt.luma_weight_l1_flag[i]) {
        br.se(pwt.luma_weight_l1[i]);
        br.se(pwt.luma_weight_l1[i]);
      } else {
        pwt.luma_weight_l1[i] = 1 << pwt.luma_log2_weight_denom;
      }
      if (ChromaArrayType != 0) {
        br.u(1, pwt.chroma_weight_l1_flag[i]);
        if (pwt.chroma_weight_l1_flag[i]) {
          pwt.chroma_weight_l1[i].resize(2);
          pwt.chroma_offset_l1[i].resize(2);
          for (size_t j = 0; j < 2; j++) {
            br.se(pwt.chroma_weight_l1[i][j]);
            br.se(pwt.chroma_offset_l1[i][j]);
          }
        } else {
          for (size_t j = 0; j < 2; j++) {
            pwt.chroma_weight_l1[i][j] = 1 << pwt.chroma_log2_weight_denom;
          }
        }
      }
    }
  }
  return true;
}

bool H264Parse::parseSeiBufferPeriodSyntax(H264SeiBufferPeriodSyntax &bp) {
  // See also : ISO 14496/10(2020) - D.1.2 Buffering period SEI message syntax

  br.ue(bp.seq_parameter_set_id);

  if (h264Contex->spsSet.count(bp.seq_parameter_set_id) == 0) {
    assert(false);
    return false;
  }
  H264SpsPtr sps = h264Contex->spsSet[bp.seq_parameter_set_id];

  // The variable NalHrdBpPresentFlag is derived as follows:
  // - If any of the following is true, the value of NalHrdBpPresentFlag shall
  // be set equal to 1:
  //  - nal_hrd_parameters_present_flag is present in the bitstream and is
  //  equal to 1,
  //  - the need for the presence of buffering period parameters for NAL HRD
  //  operation in the bitstream in buffering
  //    period SEI messages is determined by the application, by some means
  //    not specified in this Recommendation | International Standard.
  // - Otherwise, the value of NalHrdBpPresentFlag shall be set equal to 0
  //
  // The variable VclHrdBpPresentFlag is derived as follows:
  // - If any of the following is true, the value of VclHrdBpPresentFlag shall
  // be set equal to 1:
  //  - vcl_hrd_parameters_present_flag is present in the bitstream and is
  //  equal to 1,
  //  - the need for the presence of buffering period parameters for VCL HRD
  //  operation in the bitstream in buffering
  //    period SEI messages is determined by the application, by some means
  //    not specified in this Recommendation | International Standard.
  //

  int8_t NalHrdBpPresentFlag =
      sps->vui_parameters_present_flag &&
              sps->vui_seq_parameters.nal_hrd_parameters_present_flag
          ? 1
          : 0;
  int8_t VclHrdBpPresentFlag =
      sps->vui_parameters_present_flag &&
              sps->vui_seq_parameters.vcl_hrd_parameters_present_flag
          ? 1
          : 0;

  // Hint :  initial_cpb_removal_delay and initial_cpb_removal_delay_offset
  //         The syntax element has a length in bits given by
  //         initial_cpb_removal_delay_length_minus1 + 1.
  // See also : ISO 14496/10(2020) - D.2.2 Buffering period SEI message
  // semantics

  if (NalHrdBpPresentFlag) {
    int32_t cpb_cnt_minus1 =
        sps->vui_seq_parameters.nal_hrd_parameters.cpb_cnt_minus1;
    bp.initial_cpb_removal_delay.resize(cpb_cnt_minus1 + 1);
    bp.initial_cpb_removal_delay_offset.resize(cpb_cnt_minus1 + 1);
    for (int32_t SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1;
         SchedSelIdx++) {
      br.u(sps->vui_seq_parameters.nal_hrd_parameters
                   .initial_cpb_removal_delay_length_minus1 +
               1,
           bp.initial_cpb_removal_delay[SchedSelIdx]);
      br.u(sps->vui_seq_parameters.nal_hrd_parameters
                   .initial_cpb_removal_delay_length_minus1 +
               1,
           bp.initial_cpb_removal_delay_offset[SchedSelIdx]);
    }
  }

  if (VclHrdBpPresentFlag) {
    int32_t cpb_cnt_minus1 =
        sps->vui_seq_parameters.vcl_hrd_parameters.cpb_cnt_minus1;
    bp.initial_cpb_removal_delay.resize(cpb_cnt_minus1 + 1);
    bp.initial_cpb_removal_delay_offset.resize(cpb_cnt_minus1 + 1);
    for (int32_t SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1;
         SchedSelIdx++) {
      br.u(sps->vui_seq_parameters.vcl_hrd_parameters
                   .initial_cpb_removal_delay_length_minus1 +
               1,
           bp.initial_cpb_removal_delay[SchedSelIdx]);
      br.u(sps->vui_seq_parameters.vcl_hrd_parameters
                   .initial_cpb_removal_delay_length_minus1 +
               1,
           bp.initial_cpb_removal_delay_offset[SchedSelIdx]);
    }
  }
  return true;
}

bool H264Parse::parseSeiUserDataRegisteredSyntax(
    uint32_t payloadSize, H264SeiUserDataRegisteredSyntax &udr) {
  // See also : ISO 14496/10(2020) - D.1.6 User data registered by ITU-T Rec.
  // T.35 SEI message syntax

  uint32_t i = 0;
  br.u(8, udr.itu_t_t35_country_code);
  if (udr.itu_t_t35_country_code != 0xFF) {
    i = 1;
  } else {
    br.u(8, udr.itu_t_t35_country_code_extension_byte);
    i = 2;
  }
  udr.itu_t_t35_payload_byte.resize(payloadSize - i);
  size_t index = 0;
  do {
    br.u(8, udr.itu_t_t35_payload_byte[index]);
    index++;
    i++;
  } while (i < payloadSize);
  return true;
}

bool H264Parse::parseSeiUserDataUnregisteredSyntax(
    uint32_t payloadSize, H264SeiUserDataUnregisteredSyntax &udn) {
  // See also : ISO 14496/10(2020) - D.1.7 User data unregistered SEI message
  // syntax

  for (size_t i = 0; i < 16; i++) {
    br.u(8, udn.uuid_iso_iec_11578[i]);
  }
  udn.user_data_payload_byte.resize(payloadSize - 16);
  for (uint16_t i = 16; i < payloadSize; i++) {
    br.u(8, udn.user_data_payload_byte[i - 16]);
  }
  return true;
}

bool H264Parse::parseSeiPictureTimingSyntax(const H264VuiSyntax &vui,
                                            H264SeiPictureTimingSyntax &pt) {
  // See also : ISO 14496/10(2020) - D.1.2 Buffering period SEI message syntax

  // The variable CpbDpbDelaysPresentFlag is derived as follows:
  //     – If any of the following is true, the value of
  //     CpbDpbDelaysPresentFlag shall be set equal to 1: –
  //     nal_hrd_parameters_present_flag is present in the bitstream and is
  //     equal to 1, – vcl_hrd_parameters_present_flag is present in the
  //     bitstream and is equal to 1, – the need for the presence of CPB and
  //     DPB output delays in the bitstream in picture timing SEI messages is
  //     determined by the application, by some means not specified in this
  //     Recommendation | International Standard. – Otherwise, the value of
  //     CpbDpbDelaysPresentFlag shall be set equal to 0.
  //
  int8_t CpbDpbDelaysPresentFlag =
      vui.nal_hrd_parameters_present_flag || vui.vcl_hrd_parameters_present_flag
          ? 1
          : 0;
  if (CpbDpbDelaysPresentFlag) {
    // Hint : cpb_removal_delay
    // NOTE 2 The value of cpb_removal_delay_length_minus1 that determines the
    // length (in bits) of the syntax element cpb_removal_delay is the value
    // of cpb_removal_delay_length_minus1 coded in the sequence parameter set
    // that is active for the primary coded picture associated with the
    // picture timing SEI message, although cpb_removal_delay specifies a
    // number of clock ticks relative to the removal time of the preceding
    // access unit containing a buffering period SEI message, which could be
    // an access unit of a different coded video sequence.
    //
    // Hint : dpb_output_delay
    // The length of the syntax element dpb_output_delay is given in bits by
    // dpb_output_delay_length_minus1 + 1. When max_dec_frame_buffering is
    // equal to 0, dpb_output_delay shall be equal to 0.
    //
    if (vui.nal_hrd_parameters_present_flag) {
      br.u(vui.nal_hrd_parameters.cpb_removal_delay_length_minus1,
           pt.cpb_removal_delay);
      br.u(vui.nal_hrd_parameters.dpb_output_delay_length_minus1 + 1,
           pt.cpb_removal_delay);
    } else if (vui.vcl_hrd_parameters_present_flag) {
      br.u(vui.vcl_hrd_parameters.cpb_removal_delay_length_minus1,
           pt.cpb_removal_delay);
      br.u(vui.vcl_hrd_parameters.dpb_output_delay_length_minus1 + 1,
           pt.cpb_removal_delay);
    }
  }

  if (vui.pic_struct_present_flag) {
    // See also : ISO 14496/10(2020) - Table D-1 – Interpretation of
    // pic_struct Hint : NumClockTS is determined by pic_struct as specified
    // in Table D-1.
    int8_t NumClockTS[9] = {1, 1, 1, 2, 2, 3, 3, 2, 3};
    br.u(4, pt.pic_struct);
    if (pt.pic_struct > 9) {
      assert(false);
      return false;
    }
    pt.clock_timestamp_flag.resize(NumClockTS[pt.pic_struct]);
    pt.ct_type.resize(NumClockTS[pt.pic_struct]);
    pt.nuit_field_based_flag.resize(NumClockTS[pt.pic_struct]);
    pt.counting_type.resize(NumClockTS[pt.pic_struct]);
    pt.full_timestamp_flag.resize(NumClockTS[pt.pic_struct]);
    pt.discontinuity_flag.resize(NumClockTS[pt.pic_struct]);
    pt.cnt_dropped_flag.resize(NumClockTS[pt.pic_struct]);
    pt.n_frames.resize(NumClockTS[pt.pic_struct]);
    pt.seconds_value.resize(NumClockTS[pt.pic_struct]);
    pt.minutes_value.resize(NumClockTS[pt.pic_struct]);
    pt.hours_value.resize(NumClockTS[pt.pic_struct]);
    pt.seconds_flag.resize(NumClockTS[pt.pic_struct]);
    pt.minutes_flag.resize(NumClockTS[pt.pic_struct]);
    pt.hours_flag.resize(NumClockTS[pt.pic_struct]);
    pt.time_offset.resize(NumClockTS[pt.pic_struct]);
    for (int8_t i = 0; i < NumClockTS[pt.pic_struct]; i++) {
      br.u(1, pt.clock_timestamp_flag[i]);
      if (pt.clock_timestamp_flag[i]) {
        br.u(2, pt.ct_type[i]);
        br.u(1, pt.nuit_field_based_flag[i]);
        br.u(5, pt.counting_type[i]);
        br.u(1, pt.full_timestamp_flag[i]);
        br.u(1, pt.discontinuity_flag[i]);
        br.u(1, pt.cnt_dropped_flag[i]);
        br.u(8, pt.n_frames[i]);
        if (pt.full_timestamp_flag[i]) {
          br.u(6, pt.seconds_value[i]);
          br.u(6, pt.minutes_value[i]);
          br.u(5, pt.hours_value[i]);
        } else {
          br.u(1, pt.seconds_flag[i]);
          if (pt.seconds_flag[i]) {
            br.u(6, pt.seconds_value[i]);
            br.u(1, pt.minutes_flag[i]);
            if (pt.minutes_flag[i]) {
              br.u(6, pt.minutes_value[i]);
              br.u(1, pt.hours_flag[i]);
              if (pt.hours_flag[i]) {
                br.u(5, pt.hours_value[i]);
              }
            }
          }
        }
      }
      // Hint :
      // time_offset_length greater than 0 specifies the length in bits of the
      // time_offset syntax element. time_offset_length equal to 0 specifies
      // that the time_offset syntax element is not present. When the
      // time_offset_length syntax element is present in more than one
      // hrd_parameters( ) syntax structure within the VUI parameters syntax
      // structure, the value of the time_offset_length parameters shall be
      // equal in both hrd_parameters( ) syntax structures. When the
      // time_offset_length syntax element is not present, it shall be
      // inferred to be equal to 24.
      int32_t time_offset_length = 0;
      if (vui.nal_hrd_parameters_present_flag) {
        time_offset_length = vui.nal_hrd_parameters.time_offset_length;
      } else if (vui.vcl_hrd_parameters_present_flag) {
        time_offset_length = vui.vcl_hrd_parameters.time_offset_length;
      }
      if (time_offset_length > 0) {
        br.i(time_offset_length, pt.time_offset[i]);
      }
    }
  }

  return true;
}

bool H264Parse::parseSeiRecoveryPointSyntax(H264SeiRecoveryPointSyntax &pt) {
  // See also : ISO 14496/10(2020) - D.1.8 Recovery point SEI message syntax

  br.ue(pt.recovery_frame_cnt);
  br.u(1, pt.exact_match_flag);
  br.u(1, pt.broken_link_flag);
  br.u(2, pt.changing_slice_group_idc);
  return true;
}

bool H264Parse::parseSeiContentLigntLevelInfoSyntax(
    H264SeiContentLigntLevelInfoSyntax &clli) {
  // See also : ISO 14496/10(2020) - D.1.31 Content light level information SEI
  // message syntax

  br.u(16, clli.max_content_light_level);
  br.u(16, clli.max_pic_average_light_level);
  return true;
}

bool H264Parse::parseSeiDisplayOrientationSyntax(
    H264SeiDisplayOrientationSyntax &dot) {
  // See also : ISO 14496/10(2020) - D.1.27 Display orientation SEI message
  // syntax

  br.u(1, dot.display_orientation_cancel_flag);
  if (dot.display_orientation_cancel_flag) {
    br.u(1, dot.hor_flip);
    br.u(1, dot.ver_flip);
    br.u(16, dot.anticlockwise_rotation);
    br.ue(dot.display_orientation_repetition_period);
    br.u(1, dot.display_orientation_extension_flag);
  }
  return true;
}

bool H264Parse::parseSeiMasteringDisplayColourVolumeSyntax(
    H264MasteringDisplayColourVolumeSyntax &mdcv) {
  // See also : ISO 14496/10(2020) - D.1.29 Mastering display colour volume SEI
  // message syntax

  for (size_t c = 0; c < 3; c++) {
    br.u(16, mdcv.display_primaries_x[c]);
    br.u(16, mdcv.display_primaries_y[c]);
  }
  br.u(16, mdcv.white_point_x);
  br.u(16, mdcv.white_point_y);
  br.u(32, mdcv.max_display_mastering_luminance);
  br.u(32, mdcv.min_display_mastering_luminance);
  return true;
}

bool H264Parse::parseSeiFilmGrainSyntax(H264SeiFilmGrainSyntax &fg) {
  // See also : ISO 14496/10(2020) - D.1.21 Film grain characteristics SEI
  // message syntax

  br.u(1, fg.film_grain_characteristics_cancel_flag);
  if (!fg.film_grain_characteristics_cancel_flag) {
    br.u(2, fg.film_grain_model_id);
    br.u(1, fg.separate_colour_description_present_flag);
    if (!fg.separate_colour_description_present_flag) {
      br.u(3, fg.film_grain_bit_depth_luma_minus8);
      br.u(3, fg.film_grain_bit_depth_chroma_minus8);
      br.u(1, fg.film_grain_full_range_flag);
      br.u(8, fg.film_grain_colour_primaries);
      br.u(8, fg.film_grain_transfer_characteristics);
      br.u(8, fg.film_grain_matrix_coefficients);
    }
    br.u(2, fg.blending_mode_id);
    br.u(4, fg.log2_scale_factor);
    fg.intensity_interval_lower_bound.resize(3);
    fg.intensity_interval_upper_bound.resize(3);
    fg.comp_model_value.resize(3);
    for (size_t c = 0; c < 3; c++) {
      br.u(1, fg.comp_model_present_flag[c]);
    }
    for (size_t c = 0; c < 3; c++) {
      if (fg.comp_model_present_flag[c]) {
        br.u(8, fg.num_intensity_intervals_minus1[c]);
        br.u(3, fg.num_model_values_minus1[c]);
        fg.intensity_interval_lower_bound[c].resize(
            fg.num_intensity_intervals_minus1[c] + 1);
        fg.intensity_interval_upper_bound[c].resize(
            fg.num_intensity_intervals_minus1[c] + 1);
        fg.comp_model_value[c].resize(fg.num_intensity_intervals_minus1[c] + 1);
        for (uint32_t i = 0; i <= fg.num_intensity_intervals_minus1[c]; i++) {
          br.u(8, fg.intensity_interval_lower_bound[c][i]);
          br.u(8, fg.intensity_interval_upper_bound[c][i]);
          fg.comp_model_value[c][i].resize(fg.num_model_values_minus1[c] + 1);
          for (uint32_t j = 0; j <= fg.num_model_values_minus1[c]; j++) {
            br.se(fg.comp_model_value[c][i][j]);
          }
        }
      }
    }
    br.ue(fg.film_grain_characteristics_repetition_period);
  }
  return true;
}

bool H264Parse::parseSeiFramePackingArrangementSyntax(
    H264SeiFramePackingArrangementSyntax &fpa) {
  // See also : ISO 14496/10(2020) - D.1.27 Display orientation SEI message
  // syntax

  br.ue(fpa.frame_packing_arrangement_id);
  br.u(1, fpa.frame_packing_arrangement_cancel_flag);
  if (!fpa.frame_packing_arrangement_cancel_flag) {
    br.u(7, fpa.frame_packing_arrangement_type);
    br.u(1, fpa.quincunx_sampling_flag);
    br.u(6, fpa.content_interpretation_type);
    br.u(1, fpa.spatial_flipping_flag);
    br.u(1, fpa.frame0_flipped_flag);
    br.u(1, fpa.field_views_flag);
    br.u(1, fpa.current_frame_is_frame0_flag);
    br.u(1, fpa.frame0_self_contained_flag);
    br.u(1, fpa.frame1_self_contained_flag);
    if (!fpa.quincunx_sampling_flag &&
        fpa.frame_packing_arrangement_type != 5) {
      br.u(4, fpa.frame0_grid_position_x);
      br.u(4, fpa.frame0_grid_position_y);
      br.u(4, fpa.frame1_grid_position_x);
      br.u(4, fpa.frame1_grid_position_y);
    }
    br.u(8, fpa.frame_packing_arrangement_reserved_byte);
    br.ue(fpa.frame_packing_arrangement_repetition_period);
  }
  br.u(1, fpa.frame_packing_arrangement_extension_flag);
  return true;
}

bool H264Parse::parseSeiAlternativeTransferCharacteristicsSyntax(
    H264SeiAlternativeTransferCharacteristicsSyntax &atc) {
  // See also : D.1.32 Alternative transfer characteristics SEI message syntax

  br.u(8, atc.preferred_transfer_characteristics);
  return true;
}

bool H264Parse::parseAmbientViewingEnvironmentSyntax(
    H264AmbientViewingEnvironmentSyntax &awe) {
  // See also : ISO 14496/10(2020) - D.1.34 Ambient viewing environment SEI
  // message syntax

  br.u(32, awe.ambient_illuminance);
  br.u(16, awe.ambient_light_x);
  br.u(16, awe.ambient_light_y);
  return true;
}

}