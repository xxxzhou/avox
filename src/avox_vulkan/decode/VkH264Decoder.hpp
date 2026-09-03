#pragma once

#include "VkDecoder.hpp"
#include "avox/codec/H264Decoder.hpp"

namespace avox {

class VkH264Decoder : public VkDecoder, public H264Decoder {
 public:
  VkH264Decoder(/* args */);
  virtual ~VkH264Decoder();

 protected:
  VkVideoDecodeH264ProfileInfoKHR h264ProfileInfo = {};
  VkVideoSessionParametersKHR sessionParameters = nullptr;
  
  // VkDecoder
 protected:
  virtual bool onParsePacket(const AvoxPacket & packet) override;

  // H264Decoder
 public:
  virtual void onConfigChange(bool bInit) override;
};

}