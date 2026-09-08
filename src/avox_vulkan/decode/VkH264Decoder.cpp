#include "VkH264Decoder.hpp"

namespace avox {

// struct VkH264DecoderReg {
//   VkH264DecoderReg() {
//     VCodecDesc vulkanDesc = {};
//     // 初始化 vulkanDesc 的相关信息，例如名称、是否支持硬件加速等 ;
//     vulkanDesc.name = "vulkan h264 decoder";
//     vulkanDesc.bHardware = true;
//     AvoxManager::Get().vDecoders.regInitFunc(
//         VCodecId::h264, vulkanDesc,
//         []() -> VideoDecoder* { return new VkH264Decoder(); });
//     // std::cout << "vulkan h264 decoder init" << std::endl;
//   }
// };

// // 静态变量初始化在attach_process,main之前
// static VkH264DecoderReg vkH264DecoderReg;

#pragma region h264 to vk

StdVideoH264ProfileIdc ToStdVideoH264ProfileIdc(uint8_t profile_idc) {
  switch (profile_idc) {
    case 66:
      return StdVideoH264ProfileIdc::STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    case 77:
      return StdVideoH264ProfileIdc::STD_VIDEO_H264_PROFILE_IDC_MAIN;
    case 100:
      return StdVideoH264ProfileIdc::STD_VIDEO_H264_PROFILE_IDC_HIGH;
    case 244:
      return StdVideoH264ProfileIdc::
          STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE;
    default:
      assert(false);
      return STD_VIDEO_H264_PROFILE_IDC_INVALID;
  }
}

// 添加 level_idc 转换函数
StdVideoH264LevelIdc ToStdVideoH264LevelIdc(uint32_t level_idc) {
  switch (level_idc) {
    case 10:
      return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case 11:
      return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case 12:
      return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case 13:
      return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case 20:
      return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case 21:
      return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case 22:
      return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case 30:
      return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case 31:
      return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case 32:
      return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case 40:
      return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case 41:
      return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case 42:
      return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case 50:
      return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case 51:
      return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case 52:
      return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case 60:
      return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case 61:
      return STD_VIDEO_H264_LEVEL_IDC_6_1;
    default:
    case 62:
      return STD_VIDEO_H264_LEVEL_IDC_6_2;
  }
}

StdVideoH264ChromaFormatIdc ToStdVideoH264ChromaFormatIdc(
    uint8_t chroma_format_idc) {
  switch (chroma_format_idc) {
    case 0:
      return StdVideoH264ChromaFormatIdc::
          STD_VIDEO_H264_CHROMA_FORMAT_IDC_MONOCHROME;
    case 1:
      return StdVideoH264ChromaFormatIdc::STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
    case 2:
      return StdVideoH264ChromaFormatIdc::STD_VIDEO_H264_CHROMA_FORMAT_IDC_422;
    case 3:
      return StdVideoH264ChromaFormatIdc::STD_VIDEO_H264_CHROMA_FORMAT_IDC_444;
    default:
      assert(false);
      return StdVideoH264ChromaFormatIdc::
          STD_VIDEO_H264_CHROMA_FORMAT_IDC_MAX_ENUM;
  }
}

StdVideoH264PocType ToStdVideoH264PocType(uint8_t pic_order_cnt_type) {
  switch (pic_order_cnt_type) {
    case 0:
      return StdVideoH264PocType::STD_VIDEO_H264_POC_TYPE_0;
    case 1:
      return StdVideoH264PocType::STD_VIDEO_H264_POC_TYPE_1;
    case 2:
      return StdVideoH264PocType::STD_VIDEO_H264_POC_TYPE_2;
    default:
      assert(false);
      return StdVideoH264PocType::STD_VIDEO_H264_POC_TYPE_INVALID;
  }
}

StdVideoH264AspectRatioIdc ToStdVideoH264AspectRatioIdc(
    uint8_t aspect_ratio_idc) {
  switch (aspect_ratio_idc) {
    case 0:
      return StdVideoH264AspectRatioIdc::
          STD_VIDEO_H264_ASPECT_RATIO_IDC_UNSPECIFIED;
    case 1:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_SQUARE;
    case 2:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_12_11;
    case 3:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_10_11;
    case 4:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_16_11;
    case 5:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_40_33;
    case 6:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_24_11;
    case 7:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_20_11;
    case 8:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_32_11;
    case 9:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_80_33;
    case 10:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_18_11;
    case 11:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_15_11;
    case 12:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_64_33;
    case 13:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_160_99;
    case 14:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_4_3;
    case 15:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_3_2;
    case 16:
      return StdVideoH264AspectRatioIdc::STD_VIDEO_H264_ASPECT_RATIO_IDC_2_1;
    case 255:
      return StdVideoH264AspectRatioIdc::
          STD_VIDEO_H264_ASPECT_RATIO_IDC_EXTENDED_SAR;
    default:
      assert(false);
      return StdVideoH264AspectRatioIdc::
          STD_VIDEO_H264_ASPECT_RATIO_IDC_INVALID;
  }
}

StdVideoH264WeightedBipredIdc ToStdVideoH264WeightedBipredIdc(
    uint8_t weighted_bipred_idc) {
  switch (weighted_bipred_idc) {
    case 0:
      return StdVideoH264WeightedBipredIdc::
          STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT;
    case 1:
      return StdVideoH264WeightedBipredIdc::
          STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_EXPLICIT;
    case 2:
      return StdVideoH264WeightedBipredIdc::
          STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_IMPLICIT;
    default:
      assert(false);
      return StdVideoH264WeightedBipredIdc::
          STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_INVALID;
  }
}

void FillStdVideoH264ScalingLists(const H264SpsSyntax& sps,
                                  StdVideoH264ScalingLists& vkScalingLists) {
  vkScalingLists.scaling_list_present_mask =
      sps.seq_scaling_matrix_present_flag;
  vkScalingLists.use_default_scaling_matrix_mask = 0;

  for (size_t i = 0; (i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS) &&
                     i < sps.ScalingList4x4.size();
       i++) {
    for (size_t j = 0; (j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS) &&
                       j < sps.ScalingList4x4[i].size();
         j++) {
      vkScalingLists.ScalingList4x4[i][j] = sps.ScalingList4x4[i][j];
    }
  }

  for (size_t i = 0; (i < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS) &&
                     i < sps.ScalingList8x8.size();
       i++) {
    for (size_t j = 0; (j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS) &&
                       j < sps.ScalingList8x8[i].size();
         j++) {
      vkScalingLists.ScalingList8x8[i][j] = sps.ScalingList8x8[i][j];
    }
  }
}

void FillStdVideoH264HrdParameters(const H264SpsSyntax& sps,
                                   StdVideoH264HrdParameters& vkHrd) {
  if (sps.vui_parameters_present_flag) {
    const H264VuiSyntax& vui = sps.vui_seq_parameters;
    if (vui.nal_hrd_parameters_present_flag) {
      const H264HrdSyntax& hrd = vui.nal_hrd_parameters;
      vkHrd.cpb_cnt_minus1 = hrd.cpb_cnt_minus1;
      vkHrd.bit_rate_scale = hrd.bit_rate_scale;
      vkHrd.initial_cpb_removal_delay_length_minus1 =
          hrd.initial_cpb_removal_delay_length_minus1;
      vkHrd.cpb_removal_delay_length_minus1 =
          hrd.cpb_removal_delay_length_minus1;
      vkHrd.time_offset_length = hrd.time_offset_length;
      for (uint32_t i = 0; i <= hrd.cpb_cnt_minus1; i++) {
        vkHrd.bit_rate_value_minus1[i] = hrd.bit_rate_value_minus1[i];
        vkHrd.cpb_size_value_minus1[i] = hrd.cpb_size_value_minus1[i];
        vkHrd.cbr_flag[i] = hrd.cbr_flag[i];
      }
    } else if (vui.vcl_hrd_parameters_present_flag) {
      const H264HrdSyntax& hrd = vui.vcl_hrd_parameters;
      vkHrd.cpb_cnt_minus1 = hrd.cpb_cnt_minus1;
      vkHrd.bit_rate_scale = hrd.bit_rate_scale;
      vkHrd.initial_cpb_removal_delay_length_minus1 =
          hrd.initial_cpb_removal_delay_length_minus1;
      vkHrd.cpb_removal_delay_length_minus1 =
          hrd.cpb_removal_delay_length_minus1;
      vkHrd.time_offset_length = hrd.time_offset_length;
      for (uint32_t i = 0; i <= hrd.cpb_cnt_minus1; i++) {
        vkHrd.bit_rate_value_minus1[i] = hrd.bit_rate_value_minus1[i];
        vkHrd.cpb_size_value_minus1[i] = hrd.cpb_size_value_minus1[i];
        vkHrd.cbr_flag[i] = hrd.cbr_flag[i];
      }
    }
  }
}

void FillStdVideoH264SequenceParameterSetVui(
    const H264SpsSyntax& sps, StdVideoH264SequenceParameterSetVui& vkVui) {
  if (sps.vui_parameters_present_flag) {
    const H264VuiSyntax& vui = sps.vui_seq_parameters;
    vkVui.aspect_ratio_idc = ToStdVideoH264AspectRatioIdc(vui.aspect_ratio_idc);
    vkVui.sar_width = vui.sar_width;
    vkVui.sar_height = vui.sar_height;
    vkVui.video_format = vui.video_format;
    vkVui.colour_primaries = vui.colour_primaries;
    vkVui.transfer_characteristics = vui.transfer_characteristics;
    vkVui.matrix_coefficients = vui.matrix_coefficients;
    vkVui.num_units_in_tick = vui.num_units_in_tick;
    vkVui.time_scale = vui.time_scale;
    vkVui.max_num_reorder_frames = vui.num_reorder_frames;
    vkVui.max_dec_frame_buffering = vui.max_dec_frame_buffering;
    // StdVideoH264SpsVuiFlags
    {
      vkVui.flags.aspect_ratio_info_present_flag =
          vui.aspect_ratio_info_present_flag;
      vkVui.flags.overscan_info_present_flag = vui.overscan_info_present_flag;
      vkVui.flags.overscan_appropriate_flag = vui.overscan_appropriate_flag;
      vkVui.flags.video_signal_type_present_flag =
          vui.video_signal_type_present_flag;
      vkVui.flags.video_full_range_flag = vui.video_full_range_flag;
      vkVui.flags.color_description_present_flag =
          vui.colour_description_present_flag;
      vkVui.flags.chroma_loc_info_present_flag =
          vui.chroma_location_info_present_flag;
      vkVui.flags.timing_info_present_flag = vui.timing_info_present_flag;
      vkVui.flags.fixed_frame_rate_flag = vui.fixed_frame_rate_flag;
      vkVui.flags.bitstream_restriction_flag = vui.bitstream_restriction_flag;
      vkVui.flags.nal_hrd_parameters_present_flag =
          vui.nal_hrd_parameters_present_flag;
      vkVui.flags.vcl_hrd_parameters_present_flag =
          vui.vcl_hrd_parameters_present_flag;
    }
  }
}

void FillStdVideoH264SequenceParameterSet(
    const H264SpsSyntax& sps, StdVideoH264SequenceParameterSet& vkSps) {
  vkSps.profile_idc = ToStdVideoH264ProfileIdc(sps.profile_idc);
  vkSps.level_idc = ToStdVideoH264LevelIdc(sps.level_idc);
  vkSps.seq_parameter_set_id = sps.seq_parameter_set_id;
  vkSps.chroma_format_idc =
      ToStdVideoH264ChromaFormatIdc(sps.chroma_format_idc);
  vkSps.bit_depth_luma_minus8 = sps.bit_depth_luma_minus8;
  vkSps.bit_depth_chroma_minus8 = sps.bit_depth_chroma_minus8;
  vkSps.log2_max_frame_num_minus4 = sps.log2_max_frame_num_minus4;
  vkSps.pic_order_cnt_type = ToStdVideoH264PocType(sps.pic_order_cnt_type);
  vkSps.log2_max_pic_order_cnt_lsb_minus4 =
      sps.log2_max_pic_order_cnt_lsb_minus4;
  vkSps.offset_for_non_ref_pic = sps.offset_for_non_ref_pic;
  vkSps.offset_for_top_to_bottom_field = sps.offset_for_top_to_bottom_field;
  vkSps.num_ref_frames_in_pic_order_cnt_cycle =
      sps.num_ref_frames_in_pic_order_cnt_cycle;
  vkSps.max_num_ref_frames = sps.max_num_ref_frames;
  vkSps.pic_width_in_mbs_minus1 = sps.pic_width_in_mbs_minus1;
  vkSps.pic_height_in_map_units_minus1 = sps.pic_height_in_map_units_minus1;
  vkSps.frame_crop_left_offset = sps.frame_crop_left_offset;
  vkSps.frame_crop_right_offset = sps.frame_crop_right_offset;
  vkSps.frame_crop_top_offset = sps.frame_crop_top_offset;
  vkSps.frame_crop_bottom_offset = sps.frame_crop_bottom_offset;
  // StdVideoH264SpsFlags
  {
    vkSps.flags.constraint_set0_flag = sps.constraint_set0_flag;
    vkSps.flags.constraint_set1_flag = sps.constraint_set1_flag;
    vkSps.flags.constraint_set2_flag = sps.constraint_set2_flag;
    vkSps.flags.constraint_set3_flag = sps.constraint_set3_flag;
    vkSps.flags.constraint_set4_flag = sps.constraint_set4_flag;
    vkSps.flags.constraint_set5_flag = sps.constraint_set5_flag;
    vkSps.flags.direct_8x8_inference_flag = sps.direct_8x8_inference_flag;
    vkSps.flags.mb_adaptive_frame_field_flag = sps.mb_adaptive_frame_field_flag;
    vkSps.flags.frame_mbs_only_flag = sps.frame_mbs_only_flag;
    vkSps.flags.delta_pic_order_always_zero_flag =
        sps.delta_pic_order_always_zero_flag;
    vkSps.flags.separate_colour_plane_flag = sps.separate_colour_plane_flag;
    vkSps.flags.gaps_in_frame_num_value_allowed_flag =
        sps.gaps_in_frame_num_value_allowed_flag;
    vkSps.flags.qpprime_y_zero_transform_bypass_flag =
        sps.qpprime_y_zero_transform_bypass_flag;
    vkSps.flags.frame_cropping_flag = sps.frame_cropping_flag;
    vkSps.flags.seq_scaling_matrix_present_flag =
        sps.seq_scaling_matrix_present_flag;
    vkSps.flags.vui_parameters_present_flag = sps.vui_parameters_present_flag;
  }
}

void FillStdVideoH264ScalingLists(const H264PpsSyntax& pps,
                                  StdVideoH264ScalingLists& vkScalingLists) {
  vkScalingLists.scaling_list_present_mask =
      pps.pic_scaling_matrix_present_flag;
  vkScalingLists.use_default_scaling_matrix_mask = 0;

  for (size_t i = 0; (i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS) &&
                     i < pps.ScalingList4x4.size();
       i++) {
    for (size_t j = 0; (j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS) &&
                       j < pps.ScalingList4x4[i].size();
         j++) {
      vkScalingLists.ScalingList4x4[i][j] = pps.ScalingList4x4[i][j];
    }
  }

  for (size_t i = 0; (i < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_LISTS) &&
                     i < pps.ScalingList8x8.size();
       i++) {
    for (size_t j = 0; (j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS) &&
                       j < pps.ScalingList8x8[i].size();
         j++) {
      vkScalingLists.ScalingList8x8[i][j] = pps.ScalingList8x8[i][j];
    }
  }
}

void FillStdVideoH264PictureParameterSet(
    const H264PpsSyntax& pps, StdVideoH264PictureParameterSet& vkPps) {
  vkPps.seq_parameter_set_id = pps.seq_parameter_set_id;
  vkPps.pic_parameter_set_id = pps.pic_parameter_set_id;
  vkPps.num_ref_idx_l0_default_active_minus1 =
      pps.num_ref_idx_l0_default_active_minus1;
  vkPps.num_ref_idx_l1_default_active_minus1 =
      pps.num_ref_idx_l1_default_active_minus1;
  vkPps.weighted_bipred_idc =
      ToStdVideoH264WeightedBipredIdc(pps.weighted_bipred_idc);
  vkPps.pic_init_qp_minus26 = pps.pic_init_qp_minus26;
  vkPps.pic_init_qs_minus26 = pps.pic_init_qs_minus26;
  vkPps.chroma_qp_index_offset = pps.second_chroma_qp_index_offset;
  vkPps.second_chroma_qp_index_offset = pps.second_chroma_qp_index_offset;

  vkPps.flags.transform_8x8_mode_flag = pps.transform_8x8_mode_flag;
  vkPps.flags.redundant_pic_cnt_present_flag =
      pps.redundant_pic_cnt_present_flag;
  vkPps.flags.constrained_intra_pred_flag = pps.constrained_intra_pred_flag;
  vkPps.flags.deblocking_filter_control_present_flag =
      pps.deblocking_filter_control_present_flag;
  vkPps.flags.weighted_pred_flag = pps.weighted_pred_flag;
  vkPps.flags.bottom_field_pic_order_in_frame_present_flag =
      pps.bottom_field_pic_order_in_frame_present_flag;
  vkPps.flags.entropy_coding_mode_flag = pps.entropy_coding_mode_flag;
  vkPps.flags.pic_scaling_matrix_present_flag =
      pps.pic_scaling_matrix_present_flag;
}

void FillStdVideoDecodeH264ReferenceInfo(
    H264SliceHeaderPtr slice, H264PicturePtr picture,
    StdVideoDecodeH264ReferenceInfo& h264ReferenceInfo,
    VkVideoDecodeH264DpbSlotInfoKHR& h264DpbSlotInfo) {
  h264ReferenceInfo = {};
  h264ReferenceInfo.FrameNum =
      picture->referenceFlag & used_for_long_term_reference
          ? picture->long_term_frame_idx
          : picture->FrameNum;
  h264ReferenceInfo.PicOrderCnt[0] = picture->TopFieldOrderCnt;
  h264ReferenceInfo.PicOrderCnt[1] = picture->BottomFieldOrderCnt;
  h264ReferenceInfo.flags.top_field_flag = slice->field_pic_flag;
  h264ReferenceInfo.flags.bottom_field_flag = slice->bottom_field_flag;
  h264ReferenceInfo.flags.used_for_long_term_reference =
      (picture->referenceFlag & used_for_short_term_reference) ||
      (picture->referenceFlag & used_for_long_term_reference);
  h264ReferenceInfo.flags.is_non_existing =
      picture->referenceFlag & non_existing;

  h264DpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
  h264DpbSlotInfo.pNext = &h264ReferenceInfo;
}

// void FillPictureResourceInfo(VulaknVideoFrame::ptr frame, uint32_t slot,
//                              VkVideoPictureResourceInfoKHR&
//                              pictureResourceInfo,
//                              VkVideoReferenceSlotInfoKHR& referenceSlotInfo,
//                              VkVideoDecodeH264DpbSlotInfoKHR&
//                              h264DpbSlotInfo) {

//     pictureResourceInfo = {};
//     pictureResourceInfo.sType =
//         VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
//     {
//       pictureResourceInfo.codedOffset.x = 0;
//       pictureResourceInfo.codedOffset.y = 0;
//       pictureResourceInfo.codedExtent.width = frame->params.width;
//       pictureResourceInfo.codedExtent.height = frame->params.height;
//     }
//     pictureResourceInfo.baseArrayLayer =
//         frame->params.flags &
//                 VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR
//             ? 0
//             : slot;
//     pictureResourceInfo.imageViewBinding = frame->view;

//     referenceSlotInfo = {};
//     referenceSlotInfo.slotIndex =
//         VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
//     referenceSlotInfo.pNext = &h264DpbSlotInfo;
//     referenceSlotInfo.slotIndex = slot;
//     referenceSlotInfo.pPictureResource = &pictureResourceInfo;

// }

#pragma endregion

VkH264Decoder::VkH264Decoder() {
  // vulkan的硬解需要解析I/P/B帧
  parse->enableParseSLICE = true;
}

VkH264Decoder::~VkH264Decoder() {
  if (sessionParameters) {
    AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance, DestroyVideoSessionParametersKHR)
    DestroyVideoSessionParametersKHR(vkDevice, sessionParameters, nullptr);
    sessionParameters = nullptr;
  }
}

bool VkH264Decoder::onParsePacket(const AvoxPacket & packet) {
  bool bParse = parsePacket(pkt);
  if (!bParse) {
    log(LogLevel::warn, "nula parse error:", getNalName(unit->nal),
        " size:", pkt->size);
    return false;
  }
  if (!unit->decodeAble()) {
    return true;
  }
  // 当前图片的解码信息
  StdVideoDecodeH264ReferenceInfo h264ReferenceInfo = {};
  h264ReferenceInfo.FrameNum =
      curPicture->referenceFlag & used_for_long_term_reference
          ? curPicture->long_term_frame_idx
          : curPicture->FrameNum;
  h264ReferenceInfo.PicOrderCnt[0] = curPicture->TopFieldOrderCnt;
  h264ReferenceInfo.PicOrderCnt[1] = curPicture->BottomFieldOrderCnt;
  h264ReferenceInfo.flags.top_field_flag = slice->field_pic_flag;
  h264ReferenceInfo.flags.bottom_field_flag = slice->bottom_field_flag;
  h264ReferenceInfo.flags.used_for_long_term_reference =
      (curPicture->referenceFlag & used_for_short_term_reference) ||
      (curPicture->referenceFlag & used_for_long_term_reference);
  h264ReferenceInfo.flags.is_non_existing =
      curPicture->referenceFlag & non_existing;

  VkVideoDecodeH264DpbSlotInfoKHR h264DpbSlotInfo = {
      VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR};
  h264DpbSlotInfo.pNext = &h264ReferenceInfo;

  VkVideoPictureResourceInfoKHR pictureResourceInfo = {};
  pictureResourceInfo.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
  pictureResourceInfo.codedOffset.x = 0;
  pictureResourceInfo.codedOffset.y = 0;
  pictureResourceInfo.codedExtent.width = params.width;
  pictureResourceInfo.codedExtent.height = params.height;
  // DPB是否支持arrraylayer
  pictureResourceInfo.baseArrayLayer =
      (videoCapabilities.flags &
       VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)
          ? 0
          : curPicture->id & 0x7F;
  // pictureResourceInfo.imageViewBinding = frame->view;

  VkVideoReferenceSlotInfoKHR referenceSlotInfo = {};
  referenceSlotInfo.slotIndex = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
  referenceSlotInfo.pNext = &h264DpbSlotInfo;
  referenceSlotInfo.slotIndex = curPicture->id & 0x7F;
  referenceSlotInfo.pPictureResource = &pictureResourceInfo;
  return true;
}

void VkH264Decoder::onConfigChange(bool bInit) {
  if (bInit) {
    imageExtent = {(uint32_t)params.width, (uint32_t)params.height, 1};
    h264ProfileInfo = {VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
                       nullptr};
    h264ProfileInfo.stdProfileIdc = ToStdVideoH264ProfileIdc(sps->profile_idc);
    h264ProfileInfo.pictureLayout =
        VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR;

    profileInfo = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR, nullptr};
    profileInfo.videoCodecOperation =
        VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    profileInfo.chromaSubsampling = getVkYUVType(params.yuvType);
    profileInfo.chromaBitDepth = getVkBitDepth(sps->bit_depth_luma_minus8 + 8);
    profileInfo.lumaBitDepth = getVkBitDepth(sps->bit_depth_chroma_minus8 + 8);
    profileInfo.pNext = &h264ProfileInfo;

    bVulkanInit = initVkDecoder();
    if (bVulkanInit && !sessionParameters) {
      constexpr size_t kMaxSpsNum = 32;
      constexpr size_t kMaxPpsNum = 256;
      StdVideoH264ScalingLists spsScalingLists[kMaxSpsNum] = {};
      StdVideoH264HrdParameters hrdParameters[kMaxSpsNum] = {};
      StdVideoH264SequenceParameterSetVui sequenceParameterSetVui[kMaxSpsNum] =
          {};
      StdVideoH264SequenceParameterSet sequenceParameterSet[kMaxSpsNum] = {};
      StdVideoH264ScalingLists ppsScalingLists[kMaxPpsNum] = {};
      StdVideoH264PictureParameterSet pictureParameterSet[kMaxPpsNum] = {};

      VkVideoSessionParametersCreateInfoKHR sessionParametersCreateInfo = {};
      VkVideoDecodeH264SessionParametersCreateInfoKHR
          h264SessionParametersCreateInfo = {};
      VkVideoDecodeH264SessionParametersAddInfoKHR
          h264SessionParametersAddInfo = {};

      for (auto& val : parse->h264Contex->spsSet) {
        int32_t seq_parameter_set_id = val.first;
        H264SpsPtr sps = val.second;
        if (seq_parameter_set_id < 0 || seq_parameter_set_id > kMaxSpsNum) {
          assert(false);
          continue;
        }
        FillStdVideoH264ScalingLists(
            *sps, spsScalingLists[h264SessionParametersAddInfo.stdSPSCount]);
        if (sps->vui_parameters_present_flag) {
          FillStdVideoH264HrdParameters(
              *sps, hrdParameters[h264SessionParametersAddInfo.stdSPSCount]);
          FillStdVideoH264SequenceParameterSetVui(
              *sps, sequenceParameterSetVui[h264SessionParametersAddInfo
                                                .stdSPSCount]);
          sequenceParameterSetVui[h264SessionParametersAddInfo.stdSPSCount]
              .pHrdParameters =
              &hrdParameters[h264SessionParametersAddInfo.stdSPSCount];
        }
        FillStdVideoH264SequenceParameterSet(
            *sps,
            sequenceParameterSet[h264SessionParametersAddInfo.stdSPSCount]);
        sequenceParameterSet[h264SessionParametersAddInfo.stdSPSCount]
            .pSequenceParameterSetVui =
            &sequenceParameterSetVui[h264SessionParametersAddInfo.stdSPSCount];
        sequenceParameterSet[h264SessionParametersAddInfo.stdSPSCount]
            .pScalingLists =
            &spsScalingLists[h264SessionParametersAddInfo.stdSPSCount];
        h264SessionParametersAddInfo.stdSPSCount++;
      }
      // PPS
      for (auto& val : parse->h264Contex->ppsSet) {
        int32_t pic_parameter_set_id = val.first;
        H264PpsPtr pps = val.second;
        if (pic_parameter_set_id < 0 || pic_parameter_set_id > kMaxPpsNum) {
          assert(false);
          continue;
        }
        FillStdVideoH264ScalingLists(
            *pps, ppsScalingLists[h264SessionParametersAddInfo.stdPPSCount]);
        FillStdVideoH264PictureParameterSet(
            *pps,
            pictureParameterSet[h264SessionParametersAddInfo.stdPPSCount]);
        pictureParameterSet[h264SessionParametersAddInfo.stdPPSCount]
            .pScalingLists =
            &ppsScalingLists[h264SessionParametersAddInfo.stdPPSCount];
        h264SessionParametersAddInfo.stdPPSCount++;
      }
      // VkVideoSessionParametersCreateInfoKHR
      sessionParametersCreateInfo.sType =
          VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
      sessionParametersCreateInfo.videoSession = videoSession;
      sessionParametersCreateInfo.videoSessionParametersTemplate =
          VK_NULL_HANDLE;
      sessionParametersCreateInfo.pNext = &h264SessionParametersCreateInfo;
      // VkVideoDecodeH264SessionParametersCreateInfoKHR
      h264SessionParametersCreateInfo.sType =
          VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
      h264SessionParametersCreateInfo.maxStdSPSCount =
          h264SessionParametersAddInfo.stdSPSCount;
      h264SessionParametersCreateInfo.maxStdPPSCount =
          h264SessionParametersAddInfo.stdPPSCount;
      h264SessionParametersCreateInfo.pParametersAddInfo =
          &h264SessionParametersAddInfo;
      // VkVideoDecodeH264SessionParametersAddInfoKHR
      h264SessionParametersAddInfo.sType =
          VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
      h264SessionParametersAddInfo.pStdSPSs = sequenceParameterSet;
      h264SessionParametersAddInfo.pStdPPSs = pictureParameterSet;
      AVOX_AVOX_VK_INST_FUNC_IMPL_INIT(vkInstance,
                                     CreateVideoSessionParametersKHR)
      VkResult result = CreateVideoSessionParametersKHR(
          vkDevice, &sessionParametersCreateInfo, nullptr, &sessionParameters);
      AVOX_VULKAN_LOG(result, "video session parameters fail.");
    }
  }
}

}
