#pragma once

#include <VideoToolbox/VideoToolbox.h>

#include "avox/video/VideoEncoder.hpp"

namespace avox {

class IOSVEncoder : public VideoEncoder {
 public:
  IOSVEncoder();
  virtual ~IOSVEncoder();

 private:
  VTCompressionSessionRef compressionSession = nullptr;
  bool bMetalRender = false;
  IOSurfaceRef preSurface = nullptr;
  CVPixelBufferRef pixelBuffer = nullptr;

 public:
  virtual DecodeResult onPreEncoder() override;
  virtual void flush() override;
  virtual void onClose() override;
  virtual DecodeResult encode(const GpuFrame& frame) override;
  virtual DecodeResult encode(const YUVFrame& frame) override;

 private:
  void sendConfig(CMFormatDescriptionRef formatDesc, int64_t pts);
  static void compressionOutputCallback(void* outputCallbackRefCon,
                                        void* sourceFrameRefCon,
                                        OSStatus status,
                                        VTEncodeInfoFlags infoFlags,
                                        CMSampleBufferRef sampleBuffer);
};

}
