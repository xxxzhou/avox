#include "RtcVideoEncoderFactory.hpp"

#include "RtcVideoEncoder.hpp"
#include "api/video_codecs/h264_profile_level_id.h"
#include "modules/video_coding/codecs/h264/include/h264.h"

namespace avox {

// M138 标准接口：获取支持的格式
std::vector<webrtc::SdpVideoFormat>
RtcVideoEncoderFactory::GetSupportedFormats() const {
  std::vector<webrtc::SdpVideoFormat> formats;
  webrtc::H264ProfileLevelId profile_level_id(
      webrtc::H264Profile::kProfileConstrainedBaseline,
      webrtc::H264Level::kLevel4);
  auto profileLevelIdStr = webrtc::H264ProfileLevelIdToString(profile_level_id);
  // log(LogLevel::info, "profileLevelIdStr:", profileLevelIdStr.value());
  webrtc::SdpVideoFormat h264_format("H264");
  h264_format.parameters["profile-level-id"] = *profileLevelIdStr;
  h264_format.parameters["level-asymmetry-allowed"] = "1";
  h264_format.parameters["packetization-mode"] = "1";
  formats.push_back(h264_format);
  // 添加一個 High Profile (640028) 作為備選
  webrtc::H264ProfileLevelId high_profile(webrtc::H264Profile::kProfileHigh,
                                          webrtc::H264Level::kLevel4);
  webrtc::SdpVideoFormat h264_high("H264");
  h264_high.parameters["profile-level-id"] =
      *webrtc::H264ProfileLevelIdToString(high_profile);
  h264_high.parameters["level-asymmetry-allowed"] = "1";
  h264_high.parameters["packetization-mode"] = "1";
  formats.push_back(h264_high);
  // 某些 WebRTC 版本可能需要 "H265" 或 "HEVC" 字符串
  webrtc::SdpVideoFormat h265_format("H265");
  h265_format.parameters = {
      {"profile-id", "1"},  // Main Profile
      {"tier-flag", "0"},   // Main Tier
      {"level-id", "123"}   // Level 4.1 (30 * 4.1 = 123)
  };
  formats.push_back(h265_format);
  return formats;
}

// M138 标准接口：查询支持情况
webrtc::VideoEncoderFactory::CodecSupport RtcVideoEncoderFactory::QueryCodecSupport(
    const webrtc::SdpVideoFormat& format,
    std::optional<std::string> scalability_mode) const {
  webrtc::VideoEncoderFactory::CodecSupport support;
  if (absl::EqualsIgnoreCase(format.name, "H264") ||
      absl::EqualsIgnoreCase(format.name, "H265")) {
    support.is_supported = true;
    support.is_power_efficient = true;
  }
  return support;
}

// M138 标准接口：创建编码器 (带 Environment)
std::unique_ptr<webrtc::VideoEncoder> RtcVideoEncoderFactory::Create(
    const webrtc::Environment& env, const webrtc::SdpVideoFormat& format) {
  // 可以在这里通过 env.field_trials() 获取实验性配置
  // 或者通过 format.name 区分创建 H264 还是 H265 实例
  log(LogLevel::info,
      "RtcVideoEncoderFactory::Create encoder for:", format.name);
  return std::make_unique<RtcVideoEncoder>();
}

}