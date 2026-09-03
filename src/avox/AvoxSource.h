#pragma once
// Device / stream sources push into the pipeline through these interfaces.
// A source never knows about players or renderers; it only produces frames.
#include <cstdint>

namespace avox {

struct VideoFormat {
  int width = 0;
  int height = 0;
  int fpsNum = 0;
  int fpsDen = 1;
};

struct AudioFormat {
  int sampleRate = 0;
  int channels = 0;
  int bitsPerSample = 32;
};

class IVideoSourceOb {
 public:
  virtual ~IVideoSourceOb() = default;
  virtual void onVideoFormat(const VideoFormat& fmt) = 0;
  // frame is a backend-neutral handle (native texture / hardware buffer), see gpu.h
  virtual void onVideoFrame(int64_t ptsUs, void* frame) = 0;
};

class IAudioSourceOb {
 public:
  virtual ~IAudioSourceOb() = default;
  virtual void onAudioFormat(const AudioFormat& fmt) = 0;
  virtual void onAudioFrame(int64_t ptsUs, const void* data, int bytes) = 0;
};

class IVideoSource {
 public:
  virtual ~IVideoSource() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual void setObserver(IVideoSourceOb* ob) = 0;
};

}  // namespace avox
