#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../AvoxDef.h"

namespace avox {
/**
 * @sa 1 - ISO 14496/10(2020) - A.2 Profiles
 *     2 - ISO 14496/10(2020) - H.6.1 Profiles
 */
enum H264Profile {
  // A.2.11 CAVLC 4:4:4 Intra profile
  MMP_H264_PROFILE_FREXT_CAVLC444 = 44,
  // A.2.1 Baseline profile
  MMP_H264_PROFILE_BASELINE = 66,
  // A.2.2 Main profile
  MMP_H264_PROFILE_MAIN = 77,
  // A.2.3 Extended profile
  MMP_H264_PROFILE_EXTENDED = 88,
  // A.2.4 High profile
  MMP_H264_PROFILE_HIGH = 100,
  // A.2.5 High 10 profile
  MMP_H264_PROFILE_HIGH10 = 110,
  // A.2.6 High 4:2:2 profile and A.2.9 High 4:2:2 Intra profile
  MMP_H264_PROFILE_HIGH422 = 122,
  // A.2.7 High 4:4:4 Predictive profile and A.2.10 High 4:4:4 Intra profile
  MMP_H264_PROFILE_HIGH444 = 244,
  // H.6.1.2 MFC Depth High profile
  MMP_H264_PROFILE_MVC_HIGH = 118,
  MMP_H264_PROFILE_STEREO_HIGH = 128
};

/**
 * @sa ISO 14496/10(2020) - Table 6-1 – SubWidthC, and SubHeightC values derived
 * from chroma_format_idc and separate_colour_plane_flag
 */
enum H264ChromaFormat {
  MMP_H264_CHROMA_400 = 0,  //!< Monochrome
  MMP_H264_CHROMA_420 = 1,  //!< 4:2:0
  MMP_H264_CHROMA_422 = 2,  //!< 4:2:2
  MMP_H264_CHROMA_444 = 3   //!< 4:4:4
};

/**
 * @brief H.264 视频编码标准中的切片类型枚举
 * @sa ISO 14496/10(2020) - Table 7-6 – Name association to slice_type
 *
 * 该枚举定义了 H.264
 * 视频编码标准中不同的切片类型，每个类型都对应一个唯一的整数值。
 */
enum H264SliceType {
  // P 切片（预测切片）
  MMP_H264_P_SLICE = 0,
  // B 切片（双向预测切片）
  MMP_H264_B_SLICE = 1,
  // I 切片（帧内切片）
  MMP_H264_I_SLICE = 2,
  // SP 切片（切换预测切片）
  MMP_H264_SP_SLICE = 3,
  // SI 切片（切换帧内切片）
  MMP_H264_SI_SLICE = 4,
  // 切片类型的总数
  MMP_H264_NUM_SLICE_TYPES = 5
};

/**
 * @sa  ISO 14496/10(2020) - Table 7-1 – NAL unit type codes, syntax element
 * categories, and NAL unit type classes
 */
enum H264NaluType {
  MMP_H264_NALU_TYPE_NULL = 0,
  MMP_H264_NALU_TYPE_SLICE = 1,
  MMP_H264_NALU_TYPE_DPA = 2,
  MMP_H264_NALU_TYPE_DPB = 3,
  MMP_H264_NALU_TYPE_DPC = 4,
  MMP_H264_NALU_TYPE_IDR = 5,
  MMP_H264_NALU_TYPE_SEI = 6,
  MMP_H264_NALU_TYPE_SPS = 7,
  MMP_H264_NALU_TYPE_PPS = 8,
  MMP_H264_NALU_TYPE_AUD = 9,
  MMP_H264_NALU_TYPE_EOSEQ = 10,
  MMP_H264_NALU_TYPE_EOSTREAM = 11,
  MMP_H264_NALU_TYPE_FILL = 12,
  MMP_H264_NALU_TYPE_SPSEXT = 13,
  MMP_H264_NALU_TYPE_PREFIX = 14,
  MMP_H264_NALU_TYPE_SUB_SPS = 15,
  MMP_H264_NALU_TYPE_SLICE_AUX = 19,
  MMP_H264_NALU_TYPE_SLC_EXT = 20,
  MMP_H264_NALU_TYPE_VDRD = 24
};

/**
 * @sa  ISO 14496/10(2020) - Table 7-9 – Memory management control operation
 * (memory_management_control_operation) values
 */
enum H264MmcoType {
  MMP_H264_MMCO_0 = 0,  // End memory_management_control_operation
                        // syntax element loop
  MMP_H264_MMCO_1 = 1,  // Mark a short-term reference picture as
                        // "unused for reference"
  MMP_H264_MMCO_2 = 2,  // Mark a long-term reference picture as
                        // "unused for reference"
  MMP_H264_MMCO_3 = 3,  // Mark a short-term reference picture as
                        // "used for long-term reference" and assign a
                        // long-term frame index to it
  MMP_H264_MMCO_4 = 4,  // Specify the maximum long-term frame index
                        // and mark all long-term reference pictures
                        // having long-term frame indices greater than
                        // the maximum value as "unused for reference"
  MMP_H264_MMCO_5 = 5,  // Mark all reference pictures as
                        // "unused for reference" and set the
                        // MaxLongTermFrameIdx variable to
                        // "no long-term frame indices"
  MMP_H264_MMCO_6 = 6   // Mark the current picture as
                        // "used for long-term reference" and assign a
                        // long-term frame index to it
};

/**
 * @sa ISO 14496/10(2020) - D.1.1 General SEI message syntax
 */
enum H264SeiType {
  MMP_H264_SEI_BUFFERING_PERIOD = 0,
  MMP_H264_SEI_PIC_TIMING = 1,
  MMP_H264_SEI_PAN_SCAN_RECT = 2,
  MMP_H264_SEI_FILLER_PAYLOAD = 3,
  MMP_H264_SEI_USER_DATA_REGISTERED_ITU_T_T35 = 4,
  MMP_H264_SEI_USER_DATA_UNREGISTERED = 5,
  MMP_H264_SEI_RECOVERY_POINT = 6,
  MMP_H264_SEI_DEC_REF_PIC_MARKING_REPETITION = 7,
  MMP_H264_SEI_SPARE_PIC = 8,
  MMP_H264_SEI_SCENE_INFO = 9,
  MMP_H264_SEI_SUB_SEQ_INFO = 10,
  MMP_H264_SEI_SUB_SEQ_LAYER_CHARACTERISTICS = 11,
  MMP_H264_SEI_SUB_SEQ_CHARACTERISTICS = 12,
  MMP_H264_SEI_FULL_FRAME_FREEZE = 13,
  MMP_H264_SEI_FULL_FRAME_FREEZE_RELEASE = 14,
  MMP_H264_SEI_FULL_FRAME_SNAPSHOT = 15,
  MMP_H264_SEI_PROGRESSIVE_REFINEMENT_SEGMENT_START = 16,
  MMP_H264_SEI_PROGRESSIVE_REFINEMENT_SEGMENT_END = 17,
  MMP_H264_SEI_MOTION_CONSTRAINED_SLICE_GROUP_SET = 18,
  MMP_H264_SEI_FILM_GRAIN_CHARACTERISTICS = 19,
  MMP_H264_SEI_DEBLOCKING_FILTER_DISPLAY_PREFERENCE = 20,
  MMP_H264_SEI_STEREO_VIDEO_INFO = 21,
  MMP_H264_SEI_POST_FILTER_HINTS = 22,
  MMP_H264_SEI_TONE_MAPPING = 23,
  MMP_H264_SEI_SCALABILITY_INFO = 24,
  MMP_H264_SEI_SUB_PIC_SCALABLE_LAYER = 25,
  MMP_H264_SEI_NON_REQUIRED_LAYER_REP = 26,
  MMP_H264_SEI_PRIORITY_LAYER_INFO = 27,
  MMP_H264_SEI_LAYERS_NOT_PRESENT = 28,
  MMP_H264_SEI_LAYER_DEPENDENCY_CHANGE = 29,
  MMP_H264_SEI_SCALABLE_NESTING = 30,
  MMP_H264_SEI_BASE_LAYER_TEMPORAL_HRD = 31,
  MMP_H264_SEI_QUALITY_LAYER_INTEGRITY_CHECK = 32,
  MMP_H264_SEI_REDUNDANT_PIC_PROPERTY = 33,
  MMP_H264_SEI_TL0_DEP_REP_INDEX = 34,
  MMP_H264_SEI_TL_SWITCHING_POINT = 35,
  MMP_H264_SEI_PARALLEL_DECODING_INFO = 36,
  MMP_H264_SEI_MVC_SCALABLE_NESTING = 37,
  MMP_H264_SEI_VIEW_SCALABILITY_INFO = 38,
  MMP_H264_SEI_MULTIVIEW_SCENE_INFO = 39,
  MMP_H264_SEI_MULTIVIEW_ACQUISITION_INFO = 40,
  MMP_H264_SEI_NON_REQUIRED_VIEW_COMPONENT = 41,
  MMP_H264_SEI_VIEW_DEPENDENCY_CHANGE = 42,
  MMP_H264_SEI_OPERATION_POINTS_NOT_PRESENT = 43,
  MMP_H264_SEI_BASE_VIEW_TEMPORAL_HRD = 44,
  MMP_H264_SEI_FRAME_PACKING_ARRANGEMENT = 45,
  MMP_H264_SEI_MULTIVIEW_VIEW_POSITION_4 = 46,
  MMP_H264_SEI_DISPLAY_ORIENTATION = 47,
  MMP_H264_SEI_MVCD_SCALABLE_NESTING = 48,
  MMP_H264_SEI_MVCD_VIEW_SCALABILITY_INFO = 49,
  MMP_H264_SEI_DEPTH_REPRESENTATION_INFO_4 = 50,
  MMP_H264_SEI_THREE_DIMENSIONAL_REFERENCE_DISPLAYS_INFO_4 = 51,
  MMP_H264_SEI_DEPTH_TIMING = 52,
  MMP_H264_SEI_DEPTH_SAMPLING_INFO = 53,
  MMP_H264_SEI_CONSTRAINED_DEPTH_PARAMETER_SET_IDENTIFIER = 54,
  MMP_H264_SEI_GREEN_METADATA = 56,
  MMP_H264_SEI_MASTERING_DISPLAY_COLOUR_VOLUME = 137,
  MMP_H264_SEI_CONTENT_LIGHT_LEVEL_INFO = 144,
  MMP_H264_SEI_ALTERNATIVE_TRANSFER_CHARACTERISTICS = 147,
  MP_H264_SEI_AMBIENT_VIEWING_ENVIRONMENT = 148
};

/**
 * @sa ISO 14496/10(2020) - E.1.2 HRD parameters syntax
 */
struct H264HrdSyntax {
  uint32_t cpb_cnt_minus1 = 0;
  uint32_t bit_rate_scale = 0;
  uint32_t cpb_size_scale = 0;
  std::vector<uint32_t> bit_rate_value_minus1;
  std::vector<uint32_t> cpb_size_value_minus1;
  std::vector<uint32_t> cbr_flag;
  uint32_t initial_cpb_removal_delay_length_minus1 = 0;
  uint32_t cpb_removal_delay_length_minus1 = 0;
  uint32_t dpb_output_delay_length_minus1 = 0;
  uint32_t time_offset_length = 0;
};

/**
 * @sa ISO 14496/10(2020) - E.1.1 VUI parameters syntax
 */
struct H264VuiSyntax {
  uint8_t aspect_ratio_info_present_flag = 0;
  uint8_t aspect_ratio_idc = 0;
  uint16_t sar_width = 0;
  uint16_t sar_height = 0;
  uint8_t overscan_info_present_flag = 0;
  uint8_t overscan_appropriate_flag = 0;
  uint8_t video_signal_type_present_flag = 0;
  uint8_t video_format = 0;
  uint8_t video_full_range_flag = 0;
  uint8_t colour_description_present_flag = 0;
  uint32_t colour_primaries = 0;
  uint32_t transfer_characteristics = 0;
  uint32_t matrix_coefficients = 0;
  uint8_t chroma_location_info_present_flag = 0;
  uint32_t chroma_sample_loc_type_top_field = 0;
  uint32_t chroma_sample_loc_type_bottom_field = 0;
  uint8_t timing_info_present_flag = 0;
  uint32_t num_units_in_tick = 0;
  uint32_t time_scale = 0;
  uint8_t fixed_frame_rate_flag = 0;
  uint8_t nal_hrd_parameters_present_flag = 0;
  H264HrdSyntax nal_hrd_parameters;
  uint8_t vcl_hrd_parameters_present_flag = 0;
  H264HrdSyntax vcl_hrd_parameters;
  uint8_t low_delay_hrd_flag = 0;
  uint8_t pic_struct_present_flag = 0;
  uint8_t bitstream_restriction_flag = 0;
  uint8_t motion_vectors_over_pic_boundaries_flag = 0;
  uint32_t max_bytes_per_pic_denom = 0;
  uint32_t max_bits_per_mb_denom = 0;
  uint32_t log2_max_mv_length_vertical = 0;
  uint32_t log2_max_mv_length_horizontal = 0;
  uint32_t num_reorder_frames = 0;
  uint32_t max_dec_frame_buffering = 0;
};

/**
 * @sa ISO 14496/10(2020) - F.3.3.1.1 NAL unit header SVC extension syntax
 */
struct H264NalSvcSyntax {
  uint8_t idr_flag = 0;
  uint8_t priority_id = 0;
  uint8_t no_inter_layer_pred_flag = 0;
  uint8_t dependency_id = 0;
  uint8_t quality_id = 0;
  uint8_t temporal_id = 0;
  uint8_t use_ref_base_pic_flag = 0;
  uint8_t discardable_flag = 0;
  uint8_t output_flag = 0;
  uint8_t reserved_three_2bits = 0;
};

/**
 * @sa ISO 14496/10(2020) - I.3.3.1.1 NAL unit header 3D-AVC extension syntax
 */
struct H264Nal3dAvcSyntax {
  uint8_t view_idx;
  uint8_t depth_flag;
  uint8_t non_idr_flag;
  uint8_t temporal_id;
  uint8_t anchor_pic_flag;
  uint8_t inter_view_flag;
};

/**
 * @sa  ISO 14496/10(2020) - G.3.3.1.1 NAL unit header MVC extension syntax
 */
struct H264NalMvcSyntax {
  uint8_t non_idr_flag;
  uint8_t priority_id;
  uint16_t view_id;
  uint8_t temporal_id;
  uint8_t anchor_pic_flag;
  uint8_t inter_view_flag;
  uint8_t reserved_one_bit;
};

/**
 * @sa ISO 14496/10(2020) - D.1.2 Buffering period SEI message syntax
 */
struct H264SeiBufferPeriodSyntax {
  uint32_t seq_parameter_set_id;
  std::vector<uint32_t> initial_cpb_removal_delay;
  std::vector<uint32_t> initial_cpb_removal_delay_offset;
};

/**
 * @sa ISO 14496/10(2020) - D.1.3 Picture timing SEI message syntax
 */
struct H264SeiPictureTimingSyntax {
  uint32_t cpb_removal_delay;
  uint32_t dpb_output_delay;
  uint8_t pic_struct;
  std::vector<uint8_t> clock_timestamp_flag;
  std::vector<uint8_t> ct_type;
  std::vector<uint8_t> nuit_field_based_flag;
  std::vector<uint8_t> counting_type;
  std::vector<uint8_t> full_timestamp_flag;
  std::vector<uint8_t> discontinuity_flag;
  std::vector<uint8_t> cnt_dropped_flag;
  std::vector<uint8_t> n_frames;
  std::vector<uint8_t> seconds_value;
  std::vector<uint8_t> minutes_value;
  std::vector<uint8_t> hours_value;
  std::vector<uint8_t> seconds_flag;
  std::vector<uint8_t> minutes_flag;
  std::vector<uint8_t> hours_flag;
  std::vector<int32_t> time_offset;
};

/**
 * @sa ISO 14496/10(2020) - D.1.6 User data registered by ITU-T Rec. T.35 SEI
 * message syntax
 */
struct H264SeiUserDataRegisteredSyntax {
  uint8_t itu_t_t35_country_code;
  uint8_t itu_t_t35_country_code_extension_byte;
  std::vector<uint8_t> itu_t_t35_payload_byte;
};

/**
 * @sa ISO 14496/10(2020) - D.1.7 User data unregistered SEI message syntax
 */
struct H264SeiUserDataUnregisteredSyntax {
  uint8_t uuid_iso_iec_11578[16];
  std::vector<uint8_t> user_data_payload_byte;
};

/**
 * @sa ISO 14496/10(2020) - D.1.8 Recovery point SEI message syntax
 */
struct H264SeiRecoveryPointSyntax {
  uint32_t recovery_frame_cnt;
  uint8_t exact_match_flag;
  uint8_t broken_link_flag;
  uint8_t changing_slice_group_idc;
};

/**
 * @sa  ISO 14496/10(2020) - D.1.21 Film grain characteristics SEI message
 * syntax
 */
struct H264SeiFilmGrainSyntax {
  uint8_t film_grain_characteristics_cancel_flag;
  uint8_t film_grain_model_id;
  uint8_t separate_colour_description_present_flag;
  uint8_t film_grain_bit_depth_luma_minus8;
  uint8_t film_grain_bit_depth_chroma_minus8;
  uint8_t film_grain_full_range_flag;
  uint8_t film_grain_colour_primaries;
  uint8_t film_grain_transfer_characteristics;
  uint8_t film_grain_matrix_coefficients;
  uint8_t blending_mode_id;
  uint8_t log2_scale_factor;
  uint8_t comp_model_present_flag[3];
  uint8_t num_intensity_intervals_minus1[3];
  uint8_t num_model_values_minus1[3];
  std::vector<std::vector<uint8_t>> intensity_interval_lower_bound;
  std::vector<std::vector<uint8_t>> intensity_interval_upper_bound;
  std::vector<std::vector<std::vector<int32_t>>> comp_model_value;
  uint32_t film_grain_characteristics_repetition_period;
};

/**
 * @sa  ISO 14496/10(2020) - D.1.26 Frame packing arrangement SEI message syntax
 */
struct H264SeiFramePackingArrangementSyntax {
  uint32_t frame_packing_arrangement_id;
  uint8_t frame_packing_arrangement_cancel_flag;
  uint8_t frame_packing_arrangement_type;
  uint8_t quincunx_sampling_flag;
  uint8_t content_interpretation_type;
  uint8_t spatial_flipping_flag;
  uint8_t frame0_flipped_flag;
  uint8_t field_views_flag;
  uint8_t current_frame_is_frame0_flag;
  uint8_t frame0_self_contained_flag;
  uint8_t frame1_self_contained_flag;
  uint8_t frame0_grid_position_x;
  uint8_t frame0_grid_position_y;
  uint8_t frame1_grid_position_x;
  uint8_t frame1_grid_position_y;
  uint8_t frame_packing_arrangement_reserved_byte;
  uint32_t frame_packing_arrangement_repetition_period;
  uint8_t frame_packing_arrangement_extension_flag;
};

/**
 * @sa ISO 14496/10(2020) - D.1.27 Display orientation SEI message syntax
 */
struct H264SeiDisplayOrientationSyntax {
  uint8_t display_orientation_cancel_flag;
  uint8_t hor_flip;
  uint8_t ver_flip;
  uint16_t anticlockwise_rotation;
  uint32_t display_orientation_repetition_period;
  uint8_t display_orientation_extension_flag;
};

/**
 * @sa ISO 14496/10(2020) - D.1.29 Mastering display colour volume SEI message
 * syntax
 */
struct H264MasteringDisplayColourVolumeSyntax {
  uint16_t display_primaries_x[3];
  uint16_t display_primaries_y[3];
  uint16_t white_point_x;
  uint16_t white_point_y;
  uint32_t max_display_mastering_luminance;
  uint32_t min_display_mastering_luminance;
};

/**
 * @sa  ISO 14496/10(2020) - D.1.31 Content light level information SEI message
 * syntax
 */
struct H264SeiContentLigntLevelInfoSyntax {
  uint16_t max_content_light_level;
  uint16_t max_pic_average_light_level;
};

/**
 * @sa ISO 14496/10(2020) - D.1.32 Alternative transfer characteristics SEI
 * message syntax
 */
struct H264SeiAlternativeTransferCharacteristicsSyntax {
  uint8_t preferred_transfer_characteristics;
};

/**
 * @sa  ISO 14496/10(2020) - D.1.34 Ambient viewing environment SEI message
 * syntax
 */
struct H264AmbientViewingEnvironmentSyntax {
  uint32_t ambient_illuminance;
  uint16_t ambient_light_x;
  uint16_t ambient_light_y;
};

/**
 * @sa ISO 14496/10(2020) - 7.3.2.3.1 Supplemental enhancement information
 * message syntax
 */
struct H264SeiSyntax {
  uint64_t payloadType;
  uint64_t payloadSize;
  H264SeiBufferPeriodSyntax bp;
  H264SeiPictureTimingSyntax pt;
  H264SeiRecoveryPointSyntax rp;
  H264SeiContentLigntLevelInfoSyntax clli;
  H264SeiDisplayOrientationSyntax dot;
  H264SeiFilmGrainSyntax fg;
  H264SeiFramePackingArrangementSyntax fpa;
  H264MasteringDisplayColourVolumeSyntax mpvc;
  H264SeiUserDataRegisteredSyntax udr;
  H264SeiUserDataUnregisteredSyntax udn;
  H264SeiAlternativeTransferCharacteristicsSyntax atc;
  H264AmbientViewingEnvironmentSyntax awe;
};

/**
 * @brief H.264可伸缩视频编码（SVC）序列参数集（SPS）扩展语法结构
 *
 * 该结构定义了H.264 SVC SPS的扩展语法元素，用于描述可伸缩视频编码的参数。
 *
 * @sa ISO 14496/10(2020) - F.3.3.2.1.4 Sequence parameter set SVC extension
 * syntax
 */
struct H264SpsSvcSynctax {
  // 层间去块滤波器控制存在标志
  uint8_t inter_layer_deblocking_filter_control_present_flag;
  // 扩展空间可伸缩性标识符
  uint8_t extended_spatial_scalability_idc;
  // 色度相位X加1标志
  uint8_t chroma_phase_x_plus1_flag;
  // 色度相位Y加1
  uint8_t chroma_phase_y_plus1;
  // 参考层色度相位X加1标志
  uint8_t seq_ref_layer_chroma_phase_x_plus1_flag;
  // 参考层色度相位Y加1
  uint8_t seq_ref_layer_chroma_phase_y_plus1;
  // 参考层缩放后的左偏移量
  int32_t seq_scaled_ref_layer_left_offse;
  // 参考层缩放后的上偏移量
  int32_t seq_scaled_ref_layer_top_offset;
  // 参考层缩放后的右偏移量
  int32_t seq_scaled_ref_layer_right_offse;
  // 参考层缩放后的下偏移量
  int32_t seq_scaled_ref_layer_bottom_offset;
  // 序列系数级别预测标志
  uint8_t seq_tcoeff_level_prediction_flag;
  // 自适应系数级别预测标志
  uint8_t adaptive_tcoeff_level_prediction_flag;
  // 切片头限制标志
  uint8_t slice_header_restriction_flag;
};

/**
 * @brief H.264多视图编码（MVC）序列参数集（SPS）语法结构
 *
 * 该结构定义了H.264 MVC SPS的语法元素，用于描述多视图视频编码的参数。
 *
 * @sa ISO 14496/10(2020) - G.3.3.2.1.4 Sequence parameter set MVC extension
 * syntax
 */
struct H264SpsMvcSyntax {
  // 视图数量减1
  uint32_t num_views_minus1;
  // 视图ID列表
  std::vector<uint32_t> view_id;
  // L0参考帧的数量列表
  std::vector<uint32_t> num_anchor_refs_l0;
  // L0参考帧的ID列表
  std::vector<std::vector<uint32_t>> anchor_ref_l0;
  // L1参考帧的数量列表
  std::vector<uint32_t> num_anchor_refs_l1;
  // L1参考帧的ID列表
  std::vector<std::vector<uint32_t>> anchor_ref_l1;
  // 非锚点参考帧的数量列表
  std::vector<uint32_t> num_non_anchor_refs_l0;
  // 非锚点参考帧的ID列表
  std::vector<std::vector<uint32_t>> non_anchor_ref_l0;
  // 非锚点参考帧的数量列表
  std::vector<uint32_t> num_non_anchor_refs_l1;
  // 非锚点参考帧的ID列表
  std::vector<std::vector<uint32_t>> non_anchor_ref_l1;
  // 级别值数量减1
  uint32_t num_level_values_signalled_minus1;
  // 级别ID列表
  std::vector<uint8_t> level_idc;
  // 适用操作数量减1
  std::vector<uint32_t> num_applicable_ops_minus1;
  // 适用操作的时间ID列表
  std::vector<std::vector<uint8_t>> applicable_op_temporal_id;
  // 适用操作的目标视图数量减1
  std::vector<std::vector<uint32_t>> applicable_op_num_target_views_minus1;
  // 适用操作的目标视图ID列表
  std::vector<std::vector<std::vector<uint32_t>>> applicable_op_target_view_id;
  // 适用操作的视图数量减1
  std::vector<std::vector<uint32_t>> applicable_op_num_views_minus1;
};

/**
 * @brief H.264多视图编码（MVC）视频用户接口（VUI）参数扩展语法结构
 *
 * 该结构定义了H.264 MVC VUI的扩展语法元素，用于描述多视图视频编码的VUI参数。
 *
 * @sa ISO 14496/10(2020) - G.10.1 MVC VUI parameters extension syntax
 */
struct H264MvcVuiSyntax {
  // MVC操作数量减1
  uint32_t vui_mvc_num_ops_minus1;
  // MVC时间ID列表
  std::vector<uint8_t> vui_mvc_temporal_id;
  // MVC目标输出视图数量减1
  std::vector<uint32_t> vui_mvc_num_target_output_views_minus1;
  // MVC视图ID列表
  std::vector<std::vector<uint32_t>> vui_mvc_view_id;
  // MVC定时信息存在标志
  std::vector<uint8_t> vui_mvc_timing_info_present_flag;
  // MVC时钟周期内的单位数量
  std::vector<uint32_t> vui_mvc_num_units_in_tick;
  // MVC时间刻度
  std::vector<uint32_t> vui_mvc_time_scale;
  // MVC固定帧率标志
  std::vector<uint8_t> vui_mvc_fixed_frame_rate_flag;
  // MVC NAL层速率控制参数存在标志
  std::vector<uint8_t> vui_mvc_nal_hrd_parameters_present_flag;
  // MVC NAL层速率控制参数列表
  std::vector<H264HrdSyntax> nalHrds;
  // MVC VCL层速率控制参数存在标志
  std::vector<uint8_t> vui_mvc_vcl_hrd_parameters_present_flag;
  // MVC VCL层速率控制参数列表
  std::vector<H264HrdSyntax> vclHrds;
  // MVC低延迟速率控制标志
  std::vector<uint8_t> vui_mvc_low_delay_hrd_flag;
  // MVC图像结构存在标志
  std::vector<uint8_t> vui_mvc_pic_struct_present_flag;
};

/**
 * @sa  ISO 14496/10(2020) - 7.3.3.1 Reference picture list modification syntax
 */
struct H264ReferencePictureListModificationSyntax {
  uint8_t ref_pic_list_modification_flag_l0;
  uint8_t ref_pic_list_modification_flag_l1;
  std::vector<uint32_t> modification_of_pic_nums_idcs;
  union modification_of_pic_nums_idcs_data {
    uint32_t abs_diff_pic_num_minus1;
    uint32_t long_term_pic_num;
  };
  std::vector<modification_of_pic_nums_idcs_data>
      modification_of_pic_nums_idcs_datas;
};

/**
 * @sa  ISO 14496/10(2020) - 7.3.3.2 Prediction weight table syntax
 */
struct H264PredictionWeightTableSyntax {
  uint32_t luma_log2_weight_denom;
  uint32_t chroma_log2_weight_denom;
  std::vector<uint8_t> luma_weight_l0_flag;
  std::vector<int32_t> luma_weight_l0;
  std::vector<int32_t> luma_offset_l0;
  std::vector<uint8_t> chroma_weight_l0_flag;
  std::vector<std::vector<int32_t>> chroma_weight_l0;
  std::vector<std::vector<int32_t>> chroma_offset_l0;
  std::vector<uint8_t> luma_weight_l1_flag;
  std::vector<int32_t> luma_weight_l1;
  std::vector<int32_t> luma_offset_l1;
  std::vector<uint8_t> chroma_weight_l1_flag;
  std::vector<std::vector<int32_t>> chroma_weight_l1;
  std::vector<std::vector<int32_t>> chroma_offset_l1;
};

/**
 * @sa ISO 14496/10(2020) - 7.3.3.3 Decoded reference picture marking syntax
 */
struct H264DecodedReferencePictureMarkingSyntax {
  uint8_t no_output_of_prior_pics_flag;
  uint8_t long_term_reference_flag;
  uint8_t adaptive_ref_pic_marking_mode_flag;
  std::vector<uint32_t> memory_management_control_operations;
  union memory_management_control_operations_data {
    uint32_t difference_of_pic_nums_minus1;
    uint32_t long_term_pic_num;
    uint32_t long_term_frame_idx;
    uint32_t max_long_term_frame_idx_plus1;
  };
  std::vector<memory_management_control_operations_data>
      memory_management_control_operations_datas;
};

/**
 * @sa ISO 14496/10(2020) - 7.3.2.1.3 Subset sequence parameter set RBSP syntax
 */
struct H264SubSpsSyntax {
  uint8_t svc_vui_parameters_present_flag = 0;
  uint8_t bit_equal_to_one = 0;
  uint8_t mvc_vui_parameters_present_flag = 0;
  H264MvcVuiSyntax mvcVui = {};
  uint8_t additional_extension2_flag = 0;
  uint8_t additional_extension2_data_flag = 0;
  H264SpsMvcSyntax mvc = {};
};

/**
 * @brief H.264视频编码标准的序列参数集上下文结构
 *
 * 该结构定义了H.264视频编码标准中序列参数集的上下文信息。
 *
 * @sa ISO 14496/10(2020) - 7.3.2.1 Sequence parameter set RBSP syntax
 */
struct H264SpsContext {
  // 亮度位深度
  uint32_t BitDepthY = 0;
  // 亮度量化参数偏移
  uint32_t QpBdOffsetY = 0;
  // 色度位深度
  uint32_t BitDepthC = 0;
  // 色度量化参数偏移
  uint32_t QpBdOffsetC = 0;
  // 原始宏块比特数
  uint32_t RawMbBits = 0;
  // 最大帧号(GOP)
  uint32_t MaxFrameNum = 0;
  // 最大图片顺序计数LSB
  uint32_t MaxPicOrderCntLsb = 0;
  // 每个图片顺序计数周期的期望差值
  int64_t ExpectedDeltaPerPicOrderCntCycle = 0;
  // 图片宽度（以宏块为单位）
  uint32_t PicWidthInMbs = 0;
  // 图片宽度（以亮度样本为单位）
  uint32_t PicWidthInSamplesL = 0;
  // 图片宽度（以色度样本为单位）
  uint32_t PicWidthInSamplesC = 0;
  // 图片高度（以映射单元为单位）
  uint32_t PicHeightInMapUnits = 0;
  // 图片大小（以映射单元为单位）
  uint32_t PicSizeInMapUnits = 0;
  // 帧高度（以宏块为单位）
  uint32_t FrameHeightInMbs = 0;
  // 水平裁剪单位
  int32_t CropUnitX = 0;
  // 垂直裁剪单位
  int32_t CropUnitY = 0;
};

/**
 * @brief H.264视频编码标准的序列参数集语法结构
 *
 * 该结构定义了H.264视频编码标准中序列参数集的语法元素。
 *
 * @sa ISO 14496/10(2020) - 7.3.2.1 Sequence parameter set RBSP syntax
 */
struct H264SpsSyntax {  
  // 配置文件ID
  uint8_t profile_idc;
  // 约束集0标志
  uint8_t constraint_set0_flag;
  // 约束集1标志
  uint8_t constraint_set1_flag;
  // 约束集2标志
  uint8_t constraint_set2_flag;
  // 约束集3标志
  uint8_t constraint_set3_flag;
  // 约束集4标志
  uint8_t constraint_set4_flag;
  // 约束集5标志
  uint8_t constraint_set5_flag;
  // 级别ID
  uint8_t level_idc;
  // 序列参数集ID
  uint32_t seq_parameter_set_id;
  // 色度格式ID
  uint32_t chroma_format_idc;
  // 分离颜色平面标志
  uint8_t separate_colour_plane_flag;
  // 亮度位深度减8
  uint32_t bit_depth_luma_minus8;
  // 色度位深度减8
  uint32_t bit_depth_chroma_minus8;
  // QP prime Y零变换旁路标志
  uint8_t qpprime_y_zero_transform_bypass_flag;
  // 序列缩放矩阵存在标志
  uint8_t seq_scaling_matrix_present_flag;
  // 序列缩放列表存在标志
  std::vector<uint8_t> seq_scaling_list_present_flag;
  // 4x4缩放列表
  std::vector<std::vector<int32_t>> ScalingList4x4;
  // 使用默认4x4缩放矩阵标志
  std::vector<int32_t> UseDefaultScalingMatrix4x4Flag;
  // 8x8缩放列表
  std::vector<std::vector<int32_t>> ScalingList8x8;
  // 使用默认8x8缩放矩阵标志
  std::vector<int32_t> UseDefaultScalingMatrix8x8Flag;
  // 最大帧号对数减4
  uint32_t log2_max_frame_num_minus4;
  // 图片顺序计数类型(0:简单帧间预测,1/2:双向预测)
  uint32_t pic_order_cnt_type;
  // 最大图片顺序计数LSB对数减4
  uint32_t log2_max_pic_order_cnt_lsb_minus4;
  // 图片顺序差值始终为零标志
  uint8_t delta_pic_order_always_zero_flag;
  // 非参考图片偏移
  int32_t offset_for_non_ref_pic;
  // 顶部到底部场偏移
  int32_t offset_for_top_to_bottom_field;
  // 图片顺序计数周期中参考帧数量
  uint32_t num_ref_frames_in_pic_order_cnt_cycle;
  // 参考帧偏移
  std::vector<int32_t> offset_for_ref_frame;
  // 最大参考帧数量
  uint32_t max_num_ref_frames;
  // 帧号值允许间隙标志
  uint8_t gaps_in_frame_num_value_allowed_flag;
  // 图片宽度（以宏块为单位）减1
  uint32_t pic_width_in_mbs_minus1;
  // 图片高度（以映射单元为单位）减1
  uint32_t pic_height_in_map_units_minus1;
  // 仅帧宏块标志
  uint8_t frame_mbs_only_flag;
  // 宏块自适应帧场标志
  uint8_t mb_adaptive_frame_field_flag;
  // 直接8x8推断标志
  uint8_t direct_8x8_inference_flag;
  // 帧裁剪标志
  uint8_t frame_cropping_flag;
  // 帧裁剪左偏移
  uint32_t frame_crop_left_offset;
  // 帧裁剪右偏移
  uint32_t frame_crop_right_offset;
  // 帧裁剪上偏移
  uint32_t frame_crop_top_offset;
  // 帧裁剪下偏移
  uint32_t frame_crop_bottom_offset;
  // VUI参数存在标志
  uint8_t vui_parameters_present_flag;
  // VUI序列参数
  H264VuiSyntax vui_seq_parameters;
  // 序列参数集上下文
  H264SpsContext context;
};

/**
 * @brief H.264视频编码标准的图片参数集语法结构
 *
 * 该结构定义了H.264视频编码标准中图片参数集的语法元素。
 *
 * @sa ISO 14496/10(2020) - 7.3.2.2 Picture parameter set RBSP syntax
 */
struct H264PpsSyntax {  
  // 图片参数集ID
  uint32_t pic_parameter_set_id;
  // 序列参数集ID
  uint32_t seq_parameter_set_id;
  // 熵编码模式标志
  uint8_t entropy_coding_mode_flag;
  // 帧中底部场图片顺序存在标志
  uint8_t bottom_field_pic_order_in_frame_present_flag;
  // 切片组数量减1
  uint32_t num_slice_groups_minus1;
  // 切片组映射类型
  uint32_t slice_group_map_type;
  // 参考索引L0默认激活减1
  uint32_t num_ref_idx_l0_default_active_minus1;
  // 参考索引L1默认激活减1
  uint32_t num_ref_idx_l1_default_active_minus1;
  // 加权预测标志
  uint32_t weighted_pred_flag;
  // 加权双向预测ID
  uint32_t weighted_bipred_idc;
  // 图片初始量化参数减26
  int32_t pic_init_qp_minus26;
  // 图片初始量化参数减26
  int32_t pic_init_qs_minus26;
  // 色度量化参数索引偏移
  int32_t chroma_qp_index_offset;
  // 去块滤波器控制存在标志
  uint8_t deblocking_filter_control_present_flag;
  // 约束帧内预测标志
  uint8_t constrained_intra_pred_flag;
  // 冗余图片计数存在标志
  uint8_t redundant_pic_cnt_present_flag;
  // 8x8变换模式标志
  uint8_t transform_8x8_mode_flag;
  // 图片缩放矩阵存在标志
  uint8_t pic_scaling_matrix_present_flag;
  // 图片缩放列表存在标志
  std::vector<uint8_t> pic_scaling_list_present_flag;
  // 4x4缩放列表
  std::vector<std::vector<int32_t>> ScalingList4x4;
  // 使用默认4x4缩放矩阵标志
  std::vector<int32_t> UseDefaultScalingMatrix4x4Flag;
  // 8x8缩放列表
  std::vector<std::vector<int32_t>> ScalingList8x8;
  // 使用默认8x8缩放矩阵标志
  std::vector<int32_t> UseDefaultScalingMatrix8x8Flag;
  // 第二个色度量化参数索引偏移
  int32_t second_chroma_qp_index_offset;
};

/**
 * @brief H.264视频编码标准的切片头语法结构
 *
 * 该结构定义了H.264视频编码标准中切片头的语法元素。
 *
 * @sa ISO 14496/10(2020) - 7.3.3 Slice header syntax
 */
struct H264SliceHeaderSyntax {
  // 切片中第一个宏块的索引
  uint32_t first_mb_in_slice;
  // 切片类型
  uint32_t slice_type;
  // 图片参数集ID
  uint32_t pic_parameter_set_id;
  // 颜色平面ID
  uint8_t colour_plane_id;
  // 帧号
  uint64_t frame_num;
  // 场图片标志
  uint8_t field_pic_flag;
  // 底部场标志
  uint8_t bottom_field_flag;
  // IDR图片ID
  uint32_t idr_pic_id;
  // 图片顺序计数LSB
  uint32_t pic_order_cnt_lsb;
  // 底部场的图片顺序计数差值
  int32_t delta_pic_order_cnt_bottom;
  // 图片顺序计数差值数组
  int32_t delta_pic_order_cnt[2];
  // 冗余图片计数
  int32_t redundant_pic_cnt;
  // 直接空间运动矢量预测标志
  uint8_t direct_spatial_mv_pred_flag;
  // 参考索引激活覆盖标志
  uint8_t num_ref_idx_active_override_flag;
  // 参考索引L0激活减1
  uint32_t num_ref_idx_l0_active_minus1;
  // 参考索引L1激活减1
  uint32_t num_ref_idx_l1_active_minus1;
  // CABAC初始化ID
  uint32_t cabac_init_idc;
  // 切片QP差值
  int32_t slice_qp_delta;
  // 用于切换的SP标志
  uint8_t sp_for_switch_flag;
  // 切片QS差值
  int32_t slice_qs_delta;
  // 禁用去块滤波器ID
  uint32_t disable_deblocking_filter_idc;
  // 切片alpha C0偏移除以2
  int32_t slice_alpha_c0_offset_div2;
  // 切片beta偏移除以2
  int32_t slice_beta_offset_div2;
  // 切片组变化周期
  uint32_t slice_group_change_cycle;
  // 参考图片列表修改语法
  H264ReferencePictureListModificationSyntax rplm;
  // 预测权重表语法
  H264PredictionWeightTableSyntax pwt;
  // 解码参考图片标记语法
  H264DecodedReferencePictureMarkingSyntax drpm;
  // 切片数据位偏移
  uint16_t slice_data_bit_offset;
};

struct H264RplcContext {
  uint64_t FrameNum;
  uint64_t LongTermFrameIdx;
  uint64_t PicNum;
  uint64_t LongTermPicNum;
};

/**
 * @sa ISO 14496/10(2020) - 8.2.5 Decoded reference picture marking process
 */
struct H264DrpmContext {
  bool unused_for_reference;
  bool used_for_short_term_reference;
  bool used_for_long_term_reference;
  int8_t MaxLongTermFrameIdx;
  int8_t LongTermFrameIdx;
};

}