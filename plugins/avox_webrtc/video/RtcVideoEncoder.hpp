#pragma once

#include "../RtcHelper.hpp"
#include "api/video_codecs/video_encoder.h"
#include "avox/player/AVDecoder.hpp"
#include "avox/video/VideoEncoder.hpp"

namespace avox {

// 把avox实现的编码封装成webrtc的接口
// 包含windows/ios/android相应的h264/h265的硬编/软编实现
class RtcVideoEncoder : public webrtc::VideoEncoder, public avox::IEncoderOb {
 public:
  RtcVideoEncoder();
  virtual ~RtcVideoEncoder();

 protected:
  VCodecId codecId = VCodecId::none;
  std::unique_ptr<avox::VideoEncoder> encode = nullptr;
  webrtc::EncodedImageCallback* callback = nullptr;
  VCodecDesc codecDesc = {};
  webrtc::EncodedImage encodedImage;
  int64_t currentPts = 0;
  uint32_t currentRtpTimestamp = 0;
  bool bHard = true;
  YUVFormat yuvFormat = {};
  // 检查是否annexb/avcc
  bool bCheckAcc = false;
  // 是否是avcc/hvcc包
  bool bvcc = false;

 protected:
  void findEncoder(VCodecId codecId, bool bHard);
  void processPacket(AvoxPacket& packet);

 public:
  virtual void SetFecControllerOverride(
      webrtc::FecControllerOverride* fec_controller_override) override;
  virtual webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;
  virtual int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  virtual int32_t Release() override;
  virtual int32_t InitEncode(
      const webrtc::VideoCodec* codec_settings,
      const webrtc::VideoEncoder::Settings& settings) override;
  virtual int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;
  virtual void SetRates(
      const webrtc::VideoEncoder::RateControlParameters& parameters) override;
  // IEncoderOb
 public:
  virtual void onPacket(AvoxPacket& packet) override;
};

}
