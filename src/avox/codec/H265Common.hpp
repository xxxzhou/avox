#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../AvoxDef.h"
#include "H26XHelper.hpp"

namespace avox {

/**
 * @sa  ITU-T H.265 (2021) - Table 7-1 – NAL unit type codes and NAL unit type
 * classes
 */
enum H265NaluType {
  MMP_H265_NALU_TYPE_TRAIL_N = 0,
  MMP_H265_NALU_TYPE_TRAIL_R = 1,
  MMP_H265_NALU_TYPE_TSA_N = 2,
  MMP_H265_NALU_TYPE_TSA_R = 3,
  MMP_H265_NALU_TYPE_STSA_N = 4,
  MMP_H265_NALU_TYPE_STSA_R = 5,
  MMP_H265_NALU_TYPE_RADL_N = 6,
  MMP_H265_NALU_TYPE_RADL_R = 7,
  MMP_H265_NALU_TYPE_RASL_N = 8,
  MMP_H265_NALU_TYPE_RASL_R = 9,
  MMP_H265_NALU_TYPE_RSV_VCL_N10 = 10,
  MMP_H265_NALU_TYPE_RSV_VCL_N12 = 12,
  MMP_H265_NALU_TYPE_RSV_VCL_N14 = 14,
  MMP_H265_NALU_TYPE_RSV_VCL_R11 = 11,
  MMP_H265_NALU_TYPE_RSV_VCL_R13 = 13,
  MMP_H265_NALU_TYPE_RSV_VCL_R15 = 15,
  MMP_H265_NALU_TYPE_BLA_W_LP = 16,
  MMP_H265_NALU_TYPE_BLA_W_RADL = 17,
  MMP_H265_NALU_TYPE_BLA_N_LP = 18,
  MMP_H265_NALU_TYPE_IDR_W_RADL = 19,
  MMP_H265_NALU_TYPE_IDR_N_LP = 20,
  MMP_H265_NALU_TYPE_CRA_NUT = 21,
  MMP_H265_NALU_TYPE_RSV_IRAP_VCL22 = 22,
  MMP_H265_NALU_TYPE_RSV_IRAP_VCL23 = 23,
  MMP_H265_NALU_TYPE_VPS_NUT = 32,
  MMP_H265_NALU_TYPE_SPS_NUT = 33,
  MMP_H265_NALU_TYPE_PPS_NUT = 34,
  MMP_H265_NALU_TYPE_AUD_NUT = 35,
  MMP_H265_NALU_TYPE_EOS_NUT = 36,
  MMP_H265_NALU_TYPE_EOB_NUT = 37,
  MMP_H265_NALU_TYPE_FD_NUT = 38,
  MMP_H265_NALU_TYPE_PREFIX_SEI_NUT = 39,
  MMP_H265_NALU_TYPE_SUFFIX_SEI_NUT = 40
};

/**
 * @sa ITU-T H.265 (2021) - Table 7-7 – Name association to slice_type
 */
enum H265SliceType {
  MMP_H265_P_SLICE = 0,
  MMP_H265_B_SLICE = 1,
  MMP_H265_I_SLICE = 2
};

/**
 * @sa ITU-T H.265 (2021) - D.2.1 General SEI message syntax
 */
enum H265SeiPaylodType {
  MMP_H265_SEI_PIC_TIMING = 1,
  MMP_H265_SEI_RECOVERY_POINT = 6,
  MMP_H265_SEI_ACTIVE_PARAMETER_SETS = 129,
  MMP_H265_SEI_DECODED_PICTURE_HASH = 132,
  MMP_H265_SEI_TIME_CODE = 136,
  MMP_H265_SEI_MASTER_DISPLAY_COLOUR_VOLUME = 137,
  MMP_H265_SEI_CONTENT_LIGHT_LEVEL_INFORMATION = 144,
  MMP_H265_SEI_CONTENT_COLOUR_VOLUME = 149
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.7 Short-term reference picture set syntax
 */
struct H265StRefPicSetSyntax {
  uint8_t inter_ref_pic_set_prediction_flag;
  uint32_t delta_idx_minus1;
  uint8_t delta_rps_sign;
  uint32_t abs_delta_rps_minus1;
  std::vector<uint8_t> used_by_curr_pic_flag;
  std::vector<uint8_t> use_delta_flag;
  uint32_t num_negative_pics;
  uint32_t num_positive_pics;
  std::vector<uint32_t> delta_poc_s0_minus1;
  std::vector<uint8_t> used_by_curr_pic_s0_flag;
  std::vector<uint32_t> delta_poc_s1_minus1;
  std::vector<uint8_t> used_by_curr_pic_s1_flag;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.4 Scaling list data syntax
 */
struct H265ScalingListDataSyntax {
  std::vector<std::vector<uint8_t>> scaling_list_pred_mode_flag;
  std::vector<std::vector<uint32_t>> scaling_list_pred_matrix_id_delta;
  std::vector<std::vector<int32_t>> scaling_list_dc_coef_minus8;
  std::vector<std::vector<std::vector<uint8_t>>> ScalingList;
};

/**
 * @sa ITU-T H.265 (2021) - E.2.3 Sub-layer HRD parameters syntax
 */
struct H265SubLayerHrdSyntax {
  std::vector<uint32_t> bit_rate_value_minus1;
  std::vector<uint32_t> cpb_size_value_minus1;
  std::vector<uint32_t> cpb_size_du_value_minus1;
  std::vector<uint32_t> bit_rate_du_value_minus1;
  std::vector<uint8_t> cbr_flag;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.2.2 Sequence parameter set range extension
 * syntax
 */
struct H265SpsRangeSyntax {
  uint8_t transform_skip_rotation_enabled_flag;
  uint8_t transform_skip_context_enabled_flag;
  uint8_t implicit_rdpcm_enabled_flag;
  uint8_t explicit_rdpcm_enabled_flag;
  uint8_t extended_precision_processing_flag;
  uint8_t intra_smoothing_disabled_flag;
  uint8_t high_precision_offsets_enabled_flag;
  uint8_t persistent_rice_adaptation_enabled_flag;
  uint8_t cabac_bypass_alignment_enabled_flag;
};

/**
 * @sa ITU-T H.265 (2021) - F.7.3.2.3.5 General colour mapping table syntax
 */
struct H265ColourMappingTable {
  uint32_t num_cm_ref_layers_minus1;
  std::vector<uint8_t> cm_ref_layer_id;
  uint8_t cm_octant_depth;
  uint8_t cm_y_part_num_log2;
  uint32_t luma_bit_depth_cm_input_minus8;
  uint32_t chroma_bit_depth_cm_input_minus8;
  uint32_t luma_bit_depth_cm_output_minus8;
  uint32_t chroma_bit_depth_cm_output_minus8;
  uint8_t cm_res_quant_bits;
  uint8_t cm_delta_flc_bits_minus1;
  int32_t cm_adapt_threshold_u_delta;
  int32_t cm_adapt_threshold_v_delta;
};

/**
 * @sa ITU-T H.265 (2021) - F.7.3.2.3.4 Picture parameter set multilayer
 * extension syntax
 */
struct H265PpsMultilayerSyntax {
  uint8_t poc_reset_info_present_flag;
  uint8_t pps_infer_scaling_list_flag;
  uint8_t pps_scaling_list_ref_layer_id;
  uint32_t num_ref_loc_offsets;
  std::vector<uint8_t> ref_loc_offset_layer_id;
  std::vector<uint8_t> scaled_ref_layer_offset_present_flag;
  int32_t scaled_ref_layer_left_offset[1 << 6u];
  int32_t scaled_ref_layer_top_offset[1 << 6u];
  int32_t scaled_ref_layer_right_offset[1 << 6u];
  int32_t scaled_ref_layer_bottom_offset[1 << 6u];
  std::vector<uint8_t> ref_region_offset_present_flag;
  int32_t ref_region_left_offset[1 << 6u];
  int32_t ref_region_top_offset[1 << 6u];
  int32_t ref_region_right_offset[1 << 6u];
  int32_t ref_region_bottom_offset[1 << 6u];
  std::vector<uint8_t> resample_phase_set_present_flag;
  uint32_t phase_hor_luma[1 << 6u];
  uint32_t phase_ver_luma[1 << 6u];
  uint32_t phase_hor_chroma_plus8[1 << 6u];
  uint32_t phase_ver_chroma_plus8[1 << 6u];
  uint8_t colour_mapping_enabled_flag;
  H265ColourMappingTable cmt;
};

/**
 * @sa ITU-T H.265 (2021) - F.7.3.2.2.4 Sequence parameter set multilayer
 * extension syntax
 */
struct H265SpsMultilayerSyntax {
  uint8_t inter_view_mv_vert_constraint_flag;
};

/**
 * @sa ITU-T H.265 (2021) - I.7.3.2.2.5 Sequence parameter set 3D extension
 * syntax
 */
struct H265Sps3DSyntax {
  uint8_t iv_di_mc_enabled_flag[2];
  uint8_t iv_mv_scal_enabled_flag[2];
  uint32_t log2_ivmc_sub_pb_size_minus3;
  uint8_t iv_res_pred_enabled_flag;
  uint8_t depth_ref_enabled_flag;
  uint8_t vsp_mc_enabled_flag;
  uint8_t dbbp_enabled_flag;
  uint8_t tex_mc_enabled_flag;
  uint32_t log2_texmc_sub_pb_size_minus3;
  uint8_t intra_contour_enabled_flag;
  uint8_t intra_dc_only_wedge_enabled_flag;
  uint8_t cqt_cu_part_pred_enabled_flag;
  uint8_t inter_dc_only_enabled_flag;
  uint8_t skip_intra_enabled_flag;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.2.3 Sequence parameter set screen content
 * coding extension syntax
 */
struct H265SpsSccSyntax {
  uint8_t sps_curr_pic_ref_enabled_flag;
  uint8_t palette_mode_enabled_flag;
  uint32_t palette_max_size;
  uint32_t delta_palette_max_predictor_size;
  uint8_t sps_palette_predictor_initializers_present_flag;
  uint32_t sps_num_palette_predictor_initializers_minus1;
  std::vector<std::vector<uint32_t>> sps_palette_predictor_initializer;
  uint8_t motion_vector_resolution_control_idc;
  uint8_t intra_boundary_filtering_disabled_flag;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.3.2 Picture parameter set range extension
 * syntax
 */
struct H265PpsRangeSyntax {
  uint32_t log2_max_transform_skip_block_size_minus2;
  uint8_t cross_component_prediction_enabled_flag;
  uint8_t chroma_qp_offset_list_enabled_flag;
  uint32_t diff_cu_chroma_qp_offset_depth;
  uint32_t chroma_qp_offset_list_len_minus1;
  std::vector<int32_t> cb_qp_offset_list;
  std::vector<int32_t> cr_qp_offset_list;
  uint32_t log2_sao_offset_scale_luma;
  uint32_t log2_sao_offset_scale_chroma;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.3.3 Picture parameter set screen content
 * coding extension syntax
 */
struct H265PpsSccSyntax {
  uint8_t pps_curr_pic_ref_enabled_flag;
  uint8_t residual_adaptive_colour_transform_enabled_flag;
  uint8_t pps_slice_act_qp_offsets_present_flag;
  int32_t pps_act_y_qp_offset_plus5;
  int32_t pps_act_cb_qp_offset_plus5;
  int32_t pps_act_cr_qp_offset_plus3;
  uint8_t pps_palette_predictor_initializers_present_flag;
  uint32_t pps_num_palette_predictor_initializers;
  uint8_t monochrome_palette_flag;
  uint32_t luma_bit_depth_entry_minus8;
  uint32_t chroma_bit_depth_entry_minus8;
  std::vector<std::vector<uint32_t>> pps_palette_predictor_initializer;
};

/**
 * @sa ITU-T H.265 (2021) - I.7.3.2.3.8 Delta depth look-up table syntax
 */
struct H265DeltaDltSyntax {
  uint32_t num_val_delta_dlt;
  uint32_t max_diff;
  uint32_t min_diff_minus1;
  uint32_t delta_dlt_val0;
  std::vector<uint32_t> delta_val_diff_minus_min;
};

/**
 * @sa ITU-T H.265 (2021) - I.7.3.2.3.7 Picture parameter set 3D extension
 * syntax
 */
struct H265Pps3dSyntax {
  uint8_t dlts_present_flag;
  uint8_t pps_depth_layers_minus1;
  uint8_t pps_bit_depth_for_depth_layers_minus8;
  std::vector<uint8_t> dlt_flag;
  std::vector<uint8_t> dlt_pred_flag;
  std::vector<uint8_t> dlt_val_flags_present_flag;
  std::vector<std::vector<uint8_t>> dlt_value_flag;
  std::vector<H265DeltaDltSyntax> delta_dlt;
};

/**
 * @sa ITU-T H.265 (2021) - E.2.2 HRD parameters syntax
 */
struct H265HrdSyntax {
  uint8_t nal_hrd_parameters_present_flag;
  uint8_t vcl_hrd_parameters_present_flag;
  uint8_t sub_pic_hrd_params_present_flag;
  uint8_t tick_divisor_minus2;
  uint8_t du_cpb_removal_delay_increment_length_minus1;
  uint8_t sub_pic_cpb_params_in_pic_timing_sei_flag;
  uint8_t dpb_output_delay_du_length_minus1;
  uint8_t bit_rate_scale;
  uint8_t cpb_size_scale;
  uint8_t cpb_size_du_scale;
  uint8_t initial_cpb_removal_delay_length_minus1;
  uint8_t au_cpb_removal_delay_length_minus1;
  uint8_t dpb_output_delay_length_minus1;
  std::vector<uint8_t> fixed_pic_rate_general_flag;
  std::vector<uint8_t> fixed_pic_rate_within_cvs_flag;
  std::vector<uint32_t> elemental_duration_in_tc_minus1;
  std::vector<uint8_t> low_delay_hrd_flag;
  std::vector<uint32_t> cpb_cnt_minus1;
  std::vector<H265SubLayerHrdSyntax> nal_hrd_parameters;
  std::vector<H265SubLayerHrdSyntax> vcl_hrd_parameters;
};

/**
 * @sa ITU-T H.265 (2021) - E.2.1 VUI parameters syntax
 */
struct H265VuiSyntax {
  uint8_t aspect_ratio_info_present_flag;
  uint8_t aspect_ratio_idc;
  uint16_t sar_width;
  uint16_t sar_height;
  uint8_t overscan_info_present_flag;
  uint8_t overscan_appropriate_flag;
  uint8_t video_signal_type_present_flag;
  uint8_t video_format;
  uint8_t video_full_range_flag;
  uint8_t colour_description_present_flag;
  uint8_t colour_primaries;
  uint8_t
      transfer_characteristics; /* Table E.3 – Colour primaries interpretation
                                   using the colour_primaries syntax element */
  uint8_t matrix_coeffs; /* Table E.5 – Matrix coefficients interpretation using
                            the matrix_coeffs syntax element */
  uint8_t chroma_loc_info_present_flag;
  uint32_t chroma_sample_loc_type_top_field;
  uint32_t chroma_sample_loc_type_bottom_field;
  uint8_t neutral_chroma_indication_flag;
  uint8_t field_seq_flag;
  uint8_t frame_field_info_present_flag;
  uint8_t default_display_window_flag;
  uint32_t def_disp_win_left_offset;
  uint32_t def_disp_win_right_offset;
  uint32_t def_disp_win_top_offset;
  uint32_t def_disp_win_bottom_offset;
  uint8_t vui_timing_info_present_flag;
  uint32_t vui_num_units_in_tick;
  uint32_t vui_time_scale;
  uint8_t vui_poc_proportional_to_timing_flag;
  uint32_t vui_num_ticks_poc_diff_one_minus1;
  uint8_t vui_hrd_parameters_present_flag;
  H265HrdSyntax hrd_parameters;
  uint8_t bitstream_restriction_flag;
  uint8_t tiles_fixed_structure_flag;
  uint8_t motion_vectors_over_pic_boundaries_flag;
  uint8_t restricted_ref_pic_lists_flag;
  uint32_t min_spatial_segmentation_idc;
  uint32_t max_bytes_per_pic_denom;
  uint32_t max_bits_per_min_cu_denom;
  uint32_t log2_max_mv_length_horizontal;
  uint32_t log2_max_mv_length_vertical;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.3 Profile, tier and level syntax
 */
struct H265PTLSyntax {
  uint8_t general_profile_space;
  uint8_t general_tier_flag;
  uint8_t general_profile_idc;
  uint8_t general_profile_compatibility_flag[32];
  uint8_t general_progressive_source_flag;
  uint8_t general_interlaced_source_flag;
  uint8_t general_non_packed_constraint_flag;
  uint8_t general_frame_only_constraint_flag;
  uint8_t general_max_12bit_constraint_flag;
  uint8_t general_max_10bit_constraint_flag;
  uint8_t general_max_8bit_constraint_flag;
  uint8_t general_max_422chroma_constraint_flag;
  uint8_t general_max_420chroma_constraint_flag;
  uint8_t general_max_monochrome_constraint_flag;
  uint8_t general_intra_constraint_flag;
  uint8_t general_one_picture_only_constraint_flag;
  uint8_t general_lower_bit_rate_constraint_flag;
  uint8_t general_max_14bit_constraint_flag;
  uint64_t general_reserved_zero_33bits;
  uint64_t general_reserved_zero_34bits;
  uint8_t general_reserved_zero_7bits;
  uint64_t general_reserved_zero_35bits;
  uint64_t general_reserved_zero_43bits;
  uint8_t general_inbld_flag;
  uint8_t general_reserved_zero_bit;
  uint8_t general_level_idc;
  std::vector<uint8_t> sub_layer_profile_present_flag;
  std::vector<uint8_t> sub_layer_level_present_flag;
  std::vector<uint8_t> reserved_zero_2bits;
  std::vector<uint8_t> sub_layer_profile_space;
  std::vector<uint8_t> sub_layer_tier_flag;
  std::vector<uint8_t> sub_layer_profile_idc;
  std::vector<std::vector<uint8_t>> sub_layer_profile_compatibility_flag;
  std::vector<uint8_t> sub_layer_progressive_source_flag;
  std::vector<uint8_t> sub_layer_interlaced_source_flag;
  std::vector<uint8_t> sub_layer_non_packed_constraint_flag;
  std::vector<uint8_t> sub_layer_frame_only_constraint_flag;
  std::vector<uint8_t> sub_layer_max_12bit_constraint_flag;
  std::vector<uint8_t> sub_layer_max_10bit_constraint_flag;
  std::vector<uint8_t> sub_layer_max_8bit_constraint_flag;
  std::vector<uint8_t> sub_layer_max_422chroma_constraint_flag;
  std::vector<uint8_t> sub_layer_max_420chroma_constraint_flag;
  std::vector<uint8_t> sub_layer_max_monochrome_constraint_flag;
  std::vector<uint8_t> sub_layer_intra_constraint_flag;
  std::vector<uint8_t> sub_layer_one_picture_only_constraint_flag;
  std::vector<uint8_t> sub_layer_lower_bit_rate_constraint_flag;
  std::vector<uint8_t> sub_layer_max_14bit_constraint_flag;
  std::vector<uint64_t> sub_layer_reserved_zero_33bits;
  std::vector<uint64_t> sub_layer_reserved_zero_34bits;
  std::vector<uint8_t> sub_layer_reserved_zero_7bits;
  std::vector<uint64_t> sub_layer_reserved_zero_35bits;
  std::vector<uint64_t> sub_layer_reserved_zero_43bits;
  std::vector<uint8_t> sub_layer_inbld_flag;
  std::vector<uint8_t> sub_layer_reserved_zero_bit;
  std::vector<uint8_t> sub_layer_level_idc;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.1 Video parameter set RBSP syntax
 */
struct H265VPSSyntax {
  uint8_t vps_video_parameter_set_id;
  uint8_t vps_base_layer_internal_flag;
  uint8_t vps_base_layer_available_flag;
  uint8_t vps_max_layers_minus1;
  uint8_t vps_max_sub_layers_minus1;
  uint8_t vps_temporal_id_nesting_flag;
  uint16_t vps_reserved_0xffff_16bits;
  H265PTLSyntax ptl;
  uint8_t vps_sub_layer_ordering_info_present_flag;
  std::vector<uint32_t> vps_max_dec_pic_buffering_minus1;
  std::vector<uint32_t> vps_max_num_reorder_pics;
  std::vector<uint32_t> vps_max_latency_increase_plus1;
  uint8_t vps_max_layer_id;
  uint32_t vps_num_layer_sets_minus1;
  std::vector<std::vector<uint8_t>> layer_id_included_flag;
  uint8_t vps_timing_info_present_flag;
  uint32_t vps_num_units_in_tick;
  uint32_t vps_time_scale;
  uint8_t vps_poc_proportional_to_timing_flag;
  uint32_t vps_num_ticks_poc_diff_one_minus1;
  uint32_t vps_num_hrd_parameters;
  std::vector<uint32_t> hrd_layer_set_idx;
  std::vector<uint8_t> cprms_present_flag;
  std::vector<H265HrdSyntax> hrds;
  uint8_t vps_extension_flag;
  uint8_t vps_extension_data_flag;
};

/**
 * @sa ITU-T H.265 (2021) - 7.4.3.2 Sequence parameter set RBSP semantics
 */
struct H265SpsContext {
  uint32_t BitDepthY;                          // (7-4)
  uint32_t QpBdOffsetY;                        // (7-5)
  uint32_t BitDepthC;                          // (7-6)
  uint32_t QpBdOffsetC;                        // (7-7)
  uint32_t MaxPicOrderCntLsb;                  // (7-8)
  std::vector<int32_t> SpsMaxLatencyPictures;  // (7-9)
  uint32_t MinCbLog2SizeY;                     // (7-10)
  uint32_t CtbLog2SizeY;                       // (7-11)
  uint32_t MinCbSizeY;                         // (7-12)
  uint32_t CtbSizeY;                           // (7-13)
  uint32_t PicWidthInMinCbsY;                  // (7-14)
  uint32_t PicWidthInCtbsY;                    // (7-15)
  uint32_t PicHeightInMinCbsY;                 // (7-16)
  uint32_t PicHeightInCtbsY;                   // (7-17)
  uint32_t PicSizeInMinCbsY;                   // (7-18)
  uint32_t PicSizeInCtbsY;                     // (7-19)
  uint32_t PicSizeInSamplesY;                  // (7-20)
  uint32_t PicWidthInSamplesC;                 // (7-21)
  uint32_t PicHeightInSamplesC;                // (7-22)
  uint32_t CtbWidthC;                          // (7-23)
  uint32_t CtbHeightC;                         // (7-24)
  uint32_t PcmBitDepthY;                       // (7-25)
  uint32_t PcmBitDepthC;                       // (7-26)
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.2.1 General sequence parameter set RBSP syntax
 */
struct H265SpsSyntax {
  uint8_t sps_video_parameter_set_id;
  uint8_t sps_max_sub_layers_minus1;
  uint8_t sps_temporal_id_nesting_flag;
  H265PTLSyntax ptl;
  uint32_t sps_seq_parameter_set_id;
  uint32_t chroma_format_idc;
  uint8_t separate_colour_plane_flag;
  uint32_t pic_width_in_luma_samples;
  uint32_t pic_height_in_luma_samples;
  uint8_t conformance_window_flag;
  uint32_t conf_win_left_offset;
  uint32_t conf_win_right_offset;
  uint32_t conf_win_top_offset;
  uint32_t conf_win_bottom_offset;
  uint32_t bit_depth_luma_minus8;
  uint32_t bit_depth_chroma_minus8;
  uint32_t log2_max_pic_order_cnt_lsb_minus4;
  uint8_t sps_sub_layer_ordering_info_present_flag;
  std::vector<uint32_t> sps_max_dec_pic_buffering_minus1;
  std::vector<uint32_t> sps_max_num_reorder_pics;
  std::vector<uint32_t> sps_max_latency_increase_plus1;
  uint32_t log2_min_luma_coding_block_size_minus3;
  uint32_t log2_diff_max_min_luma_coding_block_size;
  uint32_t log2_min_luma_transform_block_size_minus2;
  uint32_t log2_diff_max_min_luma_transform_block_size;
  uint32_t max_transform_hierarchy_depth_inter;
  uint32_t max_transform_hierarchy_depth_intra;
  uint32_t scaling_list_enabled_flag;
  uint32_t sps_scaling_list_data_present_flag;
  H265ScalingListDataSyntax scaling_list_data;
  uint8_t amp_enabled_flag;
  uint8_t sample_adaptive_offset_enabled_flag;
  uint8_t pcm_enabled_flag;
  uint8_t pcm_sample_bit_depth_luma_minus1;
  uint8_t pcm_sample_bit_depth_chroma_minus1;
  uint32_t log2_min_pcm_luma_coding_block_size_minus3;
  uint32_t log2_diff_max_min_pcm_luma_coding_block_size;
  uint8_t pcm_loop_filter_disabled_flag;
  uint32_t num_short_term_ref_pic_sets;
  std::vector<H265StRefPicSetSyntax> stpss;
  uint8_t long_term_ref_pics_present_flag;
  uint32_t num_long_term_ref_pics_sps;
  std::vector<uint32_t> lt_ref_pic_poc_lsb_sps;
  std::vector<uint8_t> used_by_curr_pic_lt_sps_flag;
  uint8_t sps_temporal_mvp_enabled_flag;
  uint8_t strong_intra_smoothing_enabled_flag;
  uint8_t vui_parameters_present_flag;
  H265VuiSyntax vui;
  uint8_t sps_extension_present_flag;
  uint8_t sps_range_extension_flag;
  uint8_t sps_multilayer_extension_flag;
  uint8_t sps_3d_extension_flag;
  uint8_t sps_scc_extension_flag;
  uint8_t sps_extension_4bits;
  H265SpsRangeSyntax spsRange;
  H265Sps3DSyntax sps3d;
  H265SpsSccSyntax spsScc;
  uint8_t sps_extension_data_flag;
  H265SpsContext context;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.2.3.1 General picture parameter set RBSP syntax
 */
struct H265PpsSyntax {
  uint32_t pps_pic_parameter_set_id;
  uint32_t pps_seq_parameter_set_id;
  uint8_t dependent_slice_segments_enabled_flag;
  uint8_t output_flag_present_flag;
  uint8_t num_extra_slice_header_bits;
  uint8_t sign_data_hiding_enabled_flag;
  uint8_t cabac_init_present_flag;
  uint32_t num_ref_idx_l0_default_active_minus1;
  uint32_t num_ref_idx_l1_default_active_minus1;
  int32_t init_qp_minus26;
  uint8_t constrained_intra_pred_flag;
  uint8_t transform_skip_enabled_flag;
  uint8_t cu_qp_delta_enabled_flag;
  uint32_t diff_cu_qp_delta_depth;
  int32_t pps_cb_qp_offset;
  int32_t pps_cr_qp_offset;
  uint8_t pps_slice_chroma_qp_offsets_present_flag;
  uint8_t weighted_pred_flag;
  uint8_t weighted_bipred_flag;
  uint8_t transquant_bypass_enabled_flag;
  uint8_t tiles_enabled_flag;
  uint8_t entropy_coding_sync_enabled_flag;
  uint32_t num_tile_columns_minus1;
  uint32_t num_tile_rows_minus1;
  uint8_t uniform_spacing_flag;
  std::vector<uint32_t> column_width_minus1;
  std::vector<uint32_t> row_height_minus1;
  uint8_t loop_filter_across_tiles_enabled_flag;
  uint8_t pps_loop_filter_across_slices_enabled_flag;
  uint8_t deblocking_filter_control_present_flag;
  uint8_t deblocking_filter_override_enabled_flag;
  uint8_t pps_deblocking_filter_disabled_flag;
  int32_t pps_beta_offset_div2;
  int32_t pps_tc_offset_div2;
  uint8_t pps_scaling_list_data_present_flag;
  H265ScalingListDataSyntax scaling_list_data;
  uint8_t lists_modification_present_flag;
  uint32_t log2_parallel_merge_level_minus2;
  uint8_t slice_segment_header_extension_present_flag;
  uint8_t pps_extension_present_flag;
  uint8_t pps_range_extension_flag;
  uint8_t pps_multilayer_extension_flag;
  uint8_t pps_3d_extension_flag;
  uint8_t pps_scc_extension_flag;
  uint8_t pps_extension_4bits;
  H265PpsRangeSyntax ppsRange;
  H265PpsMultilayerSyntax ppsMultilayer;
  H265Pps3dSyntax pps3d;
  H265PpsSccSyntax ppsScc;
  uint8_t pps_extension_data_flag;
};

/**
 * @sa    ITU-T H.265 (2021) - F.7.3.2.3.4 Picture parameter set multilayer
 * extension syntax
 * @todo  使用 std::unordered_map<uint8_t, int32_t> 代替 int32_t[1<<6u] 是否可行
 */
struct H265PpsMultilayerExtensionSyntax {
  uint8_t poc_reset_info_present_flag;
  uint8_t pps_infer_scaling_list_flag;
  uint8_t pps_scaling_list_ref_layer_id;
  uint32_t num_ref_loc_offsets;
  std::vector<uint8_t> ref_loc_offset_layer_id;
  std::vector<uint8_t> scaled_ref_layer_offset_present_flag;
  int32_t scaled_ref_layer_left_offset[1 << 6u];
  int32_t scaled_ref_layer_top_offset[1 << 6u];
  int32_t scaled_ref_layer_right_offset[1 << 6u];
  int32_t scaled_ref_layer_bottom_offset[1 << 6u];
  std::vector<uint8_t> ref_region_offset_present_flag;
  int32_t ref_region_left_offset[1 << 6u];
  int32_t ref_region_top_offset[1 << 6u];
  int32_t ref_region_right_offset[1 << 6u];
  int32_t ref_region_bottom_offset[1 << 6u];
  std::vector<uint8_t> resample_phase_set_present_flag;
  uint32_t phase_hor_luma[1 << 6u];
  uint32_t phase_ver_luma[1 << 6u];
  uint32_t phase_hor_chroma_plus8[1 << 6u];
  uint32_t phase_ver_chroma_plus8[1 << 6u];
  uint8_t colour_mapping_enabled_flag;
  H265ColourMappingTable cmt;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.6.2 Reference picture list modification syntax
 */
struct H265RefPicListsModificationSyntax {
  uint8_t ref_pic_list_modification_flag_l0;
  std::vector<uint32_t> list_entry_l0;
  uint8_t ref_pic_list_modification_flag_l1;
  std::vector<uint32_t> list_entry_l1;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.6.3 Weighted prediction parameters syntax
 */
struct H265PredWeightTableSyntax {
  uint32_t luma_log2_weight_denom;
  int32_t delta_chroma_log2_weight_denom;
  std::vector<uint8_t> luma_weight_l0_flag;
  std::vector<uint8_t> chroma_weight_l0_flag;
  std::vector<int32_t> delta_luma_weight_l0;
  std::vector<int32_t> luma_offset_l0;
  std::vector<std::vector<int32_t>> delta_chroma_weight_l0;
  std::vector<std::vector<int32_t>> delta_chroma_offset_l0;
  std::vector<uint8_t> luma_weight_l1_flag;
  std::vector<uint8_t> chroma_weight_l1_flag;
  std::vector<int32_t> delta_luma_weight_l1;
  std::vector<int32_t> luma_offset_l1;
  std::vector<std::vector<int32_t>> delta_chroma_weight_l1;
  std::vector<std::vector<int32_t>> delta_chroma_offset_l1;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.6.1 General slice segment header syntax
 */
struct H265SliceHeaderSyntax {
  uint8_t first_slice_segment_in_pic_flag;
  uint8_t no_output_of_prior_pics_flag;
  uint32_t slice_pic_parameter_set_id;
  uint8_t dependent_slice_segment_flag;
  uint32_t slice_segment_address;
  std::vector<uint8_t> slice_reserved_flag;
  uint32_t slice_type; /* Table 7-7 – Name association to slice_type */
  uint8_t pic_output_flag;
  uint8_t colour_plane_id;
  uint32_t slice_pic_order_cnt_lsb;
  uint8_t short_term_ref_pic_set_sps_flag;
  H265StRefPicSetSyntax stps;
  uint32_t short_term_ref_pic_set_idx;
  uint32_t num_long_term_sps;
  uint32_t num_long_term_pics;
  std::vector<uint32_t> lt_idx_sps;
  std::vector<uint32_t> poc_lsb_lt;
  std::vector<uint8_t> used_by_curr_pic_lt_flag;
  std::vector<uint8_t> delta_poc_msb_present_flag;
  std::vector<uint32_t> delta_poc_msb_cycle_lt;
  uint8_t slice_temporal_mvp_enabled_flag;
  uint8_t slice_sao_luma_flag;
  uint8_t slice_sao_chroma_flag;
  uint8_t num_ref_idx_active_override_flag;
  uint32_t num_ref_idx_l0_active_minus1;
  uint32_t num_ref_idx_l1_active_minus1;
  H265RefPicListsModificationSyntax rplm;
  uint8_t mvd_l1_zero_flag;
  uint8_t cabac_init_flag;
  uint8_t collocated_from_l0_flag;
  uint32_t collocated_ref_idx;
  H265PredWeightTableSyntax pwt;
  uint32_t five_minus_max_num_merge_cand;
  uint8_t use_integer_mv_flag;
  int32_t slice_qp_delta;
  int32_t slice_cb_qp_offset;
  int32_t slice_cr_qp_offset;
  int32_t slice_act_y_qp_offset;
  int32_t slice_act_cb_qp_offset;
  int32_t slice_act_cr_qp_offset;
  uint8_t cu_chroma_qp_offset_enabled_flag;
  uint8_t deblocking_filter_override_flag;
  uint8_t slice_deblocking_filter_disabled_flag;
  int32_t slice_beta_offset_div2;
  int32_t slice_tc_offset_div2;
  uint8_t slice_loop_filter_across_slices_enabled_flag;
  uint32_t num_entry_point_offsets;
  uint32_t offset_len_minus1;
  std::vector<uint32_t> entry_point_offset_minus1;
  uint32_t slice_segment_header_extension_length;
  std::vector<uint8_t> slice_segment_header_extension_data_byte;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.3 Picture timing SEI message syntax
 */
struct H265SeiPicTimingSyntax {
  uint8_t pic_struct;
  uint8_t source_scan_type;
  uint8_t duplicate_flag;
  uint32_t au_cpb_removal_delay_minus1;
  uint32_t pic_dpb_output_delay;
  uint32_t pic_dpb_output_du_delay;
  uint32_t num_decoding_units_minus1;
  uint8_t du_common_cpb_removal_delay_flag;
  uint32_t du_common_cpb_removal_delay_increment_minus1;
  std::vector<uint32_t> num_nalus_in_du_minus1;
  std::vector<uint32_t> du_cpb_removal_delay_increment_minus1;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.8 Recovery point SEI message syntax
 */
struct H265SeiRecoveryPointSyntax {
  int32_t recovery_poc_cnt;
  uint8_t exact_match_flag;
  uint8_t broken_link_flag;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.20 Decoded picture hash SEI message syntax
 */
struct H265SeiDecodedPictureHashSyntax {
  uint8_t hash_type;
  std::vector<std::vector<uint8_t>> picture_md5;
  std::vector<uint16_t> picture_crc;
  std::vector<uint32_t> picture_checksum;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.21 Active parameter sets SEI message syntax
 */
struct H265SeiActiveParameterSetsSyntax {
  uint8_t active_video_parameter_set_id;
  uint8_t self_contained_cvs_flag;
  uint8_t no_parameter_set_update_flag;
  uint32_t num_sps_ids_minus1;
  std::vector<uint32_t> active_seq_parameter_set_id;
  std::vector<uint32_t> layer_sps_idx;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.27 Time code SEI message syntax
 */
struct H265SeiTimeCodeSyntax {
  uint8_t num_clock_ts;
  std::vector<uint8_t> clock_timestamp_flag;
  std::vector<uint8_t> units_field_based_flag;
  std::vector<uint8_t> counting_type;
  std::vector<uint8_t> full_timestamp_flag;
  std::vector<uint8_t> discontinuity_flag;
  std::vector<uint8_t> cnt_dropped_flag;
  std::vector<uint16_t> n_frames;
  std::vector<uint8_t> seconds_value;
  std::vector<uint8_t> minutes_value;
  std::vector<uint8_t> hours_value;
  std::vector<uint8_t> seconds_flag;
  std::vector<uint8_t> minutes_flag;
  std::vector<uint8_t> hours_flag;
  std::vector<uint8_t> time_offset_length;
  std::vector<int32_t> time_offset_value;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.28 Mastering display colour volume SEI
 * message syntax
 */
struct H265MasteringDisplayColourVolumeSyntax {
  uint16_t display_primaries_x[3];
  uint16_t display_primaries_y[3];
  uint16_t white_point_x;
  uint16_t white_point_y;
  uint32_t max_display_mastering_luminance;
  uint32_t min_display_mastering_luminance;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.35 Content light level information SEI
 * message syntax
 */
struct H265ContentLightLevelInformationSyntax {
  uint16_t max_content_light_level;
  uint16_t max_pic_average_light_level;
};

/**
 * @brief ITU-T H.265 (2021) - D.2.40 Content colour volume SEI message syntax
 */
struct H265ContentColourVolumeSyntax {
  uint8_t ccv_cancel_flag;
  uint8_t ccv_persistence_flag;
  uint8_t ccv_primaries_present_flag;
  uint8_t ccv_min_luminance_value_present_flag;
  uint8_t ccv_max_luminance_value_present_flag;
  uint8_t ccv_avg_luminance_value_present_flag;
  uint8_t ccv_reserved_zero_2bits;
  int32_t ccv_primaries_x[3];
  int32_t ccv_primaries_y[3];
  uint32_t ccv_min_luminance_value;
  uint32_t ccv_max_luminance_value;
  uint32_t ccv_avg_luminance_value;
};

/**
 * @brief ITU-T H.265 (2021) - 7.3.5 Supplemental enhancement information
 * message syntax
 */
struct H265SeiMessageSyntax {
  uint64_t payloadType;
  uint64_t payloadSize;
  H265SeiPicTimingSyntax pt;
  H265SeiRecoveryPointSyntax rp;
  H265SeiDecodedPictureHashSyntax dph;
  H265SeiActiveParameterSetsSyntax aps;
  H265SeiTimeCodeSyntax tc;
  H265MasteringDisplayColourVolumeSyntax mdcv;
  H265ContentLightLevelInformationSyntax clli;
  H265ContentColourVolumeSyntax ccv;
};

/**
 * @sa ITU-T H.265 (2021) - 7.3.1.2 NAL unit header syntax
 */
struct H265NalUnitHeaderSyntax {
  uint8_t forbidden_zero_bit;
  uint8_t nal_unit_type;
  uint8_t nuh_layer_id;
  uint8_t nuh_temporal_id_plus1;
};



}