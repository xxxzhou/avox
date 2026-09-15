#pragma once

#include "avox/AvoxVideo.h"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

namespace avox {

#define AVOX_IOS_VIDEOTOOLBOX_TIMEOUT_US 2000

class IOSVDecoder : public VideoDecoder {
public:
  IOSVDecoder();
  virtual ~IOSVDecoder();

private:
  VTDecompressionSessionRef decompressionSession = nullptr;
  CMVideoFormatDescriptionRef videoFormatDescription = nullptr;
  bool bMetalRender = false;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;
  // 流位深(H265 SPS profile_idc==2 即 Main10), 决定 VT 输出 8bit NV12 或 10bit x420
  int32_t streamBitDepth = 8;

  H264NalUnit h264Unit = {};
  H265NalUnit h265Unit = {};

public:
  void updateYuvFormat();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;

  // VideoDecoder
public:
  virtual void onClose() override;

private:
  static void decompressionOutputCallback(
      void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
      VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
      CMTime presentationTimeStamp, CMTime presentationDuration);
};

}
