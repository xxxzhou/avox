#include "RtcVideoDecoderFactory.hpp"

#include "api/video_codecs/h264_profile_level_id.h"
#include "modules/video_coding/codecs/h264/include/h264.h"

namespace avox {

using namespace webrtc;

std::vector<SdpVideoFormat> RtcVideoDecoderFactory::GetSupportedFormats()
    const {
  std::vector<SdpVideoFormat> formats;
  // kRTCLevel31ConstrainedBaseline "42e028"
  webrtc::H264ProfileLevelId profile_level_id(
      webrtc::H264Profile::kProfileConstrainedBaseline,
      webrtc::H264Level::kLevel4);
  auto profileLevelIdStr = webrtc::H264ProfileLevelIdToString(profile_level_id);
  // log(LogLevel::info, "profileLevelIdStr:", profileLevelIdStr.value());
  SdpVideoFormat h264_format("H264");
  h264_format.parameters["profile-level-id"] = *profileLevelIdStr;
  h264_format.parameters["level-asymmetry-allowed"] = "1";
  h264_format.parameters["packetization-mode"] = "1";
  formats.push_back(h264_format);
  // 添加一個 High Profile (640028) 作為備選
  webrtc::H264ProfileLevelId high_profile(webrtc::H264Profile::kProfileHigh,
                                          webrtc::H264Level::kLevel4);
  SdpVideoFormat h264_high("H264");
  h264_high.parameters["profile-level-id"] =
      *webrtc::H264ProfileLevelIdToString(high_profile);
  h264_high.parameters["level-asymmetry-allowed"] = "1";
  h264_high.parameters["packetization-mode"] = "1";
  formats.push_back(h264_high);

  webrtc::SdpVideoFormat h265_format("H265");
  h265_format.parameters = {
      {"profile-id", "1"},  // Main Profile
      {"tier-flag", "0"},   // Main Tier
      {"level-id", "123"}   // Level 4.1 (30 * 4.1 = 123)
  };
  formats.push_back(h265_format);
  for (const SdpVideoFormat& vf : formats) {
    log(LogLevel::info,
        "RtcVideoDecoderFactory::GetSupportedFormats format:", vf.name);
  }
  return formats;
}

VideoDecoderFactory::CodecSupport RtcVideoDecoderFactory::QueryCodecSupport(
    const webrtc::SdpVideoFormat& format, bool reference_scaling) const {
  CodecSupport codec_support;
  codec_support.is_supported = true;
  return codec_support;
}

std::unique_ptr<webrtc::VideoDecoder> RtcVideoDecoderFactory::Create(
    const webrtc::Environment& env, const webrtc::SdpVideoFormat& format) {
  log(LogLevel::info, "RtcVideoDecoderFactory::Create format:", format.name);
  return std::make_unique<RtcVideoDecoder>();
}

}
