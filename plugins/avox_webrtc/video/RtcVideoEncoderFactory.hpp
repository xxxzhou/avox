#pragma once

#include "../RtcHelper.hpp"
#include "api/environment/environment.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace avox {

class RtcVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  RtcVideoEncoderFactory() = default;
  ~RtcVideoEncoderFactory() override = default;

 public:
  // M138 标准接口：获取支持的格式
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  // M138 标准接口：查询支持情况
  CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat& format,
      std::optional<std::string> scalability_mode) const override;

  // M138 标准接口：创建编码器 (带 Environment)
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;
};

}