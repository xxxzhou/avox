#pragma once

#include "FFVEncoder.hpp"

#if defined(__ONLY_LINUX__) && defined(AVOX_ENABLE_FFMPEG)

namespace avox {

// Linux VAAPI 硬编(h264_vaapi/hevc_vaapi): CPU NV12 上载 VAAPI surface 后直送编码;
// 构造时探测 VAAPI 设备, 无设备时 encode 回基类软编链路(open2 明确失败, 不静默)
class FFVaapiEncoder : public FFVEncoder {
public:
  FFVaapiEncoder();
  virtual ~FFVaapiEncoder();

protected:
  AVBufferRef* hwBuffer = nullptr;

public:
  virtual DecodeResult encode(const YUVFrame& frame) override;

protected:
  virtual void onAttachContext() override;
};

}

#endif
