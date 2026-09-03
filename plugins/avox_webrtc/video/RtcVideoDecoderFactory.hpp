#pragma once

#include "RtcVideoDecoder.hpp"
#include "api/video_codecs/video_decoder_factory.h"

namespace avox {

class RtcVideoDecoderFactory : public webrtc::VideoDecoderFactory {
public:
  RtcVideoDecoderFactory() {}
  virtual ~RtcVideoDecoderFactory() {}

public:
  virtual std::vector<webrtc::SdpVideoFormat>
  GetSupportedFormats() const override;
  CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& format,
                                 bool reference_scaling) const override;
  virtual std::unique_ptr<webrtc::VideoDecoder>
  Create(const webrtc::Environment &env, const webrtc::SdpVideoFormat &format) override;
};

}