#pragma once

#include <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>

#include <mutex>

#include "avox/audio/AudioOutput.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

class IOSAudioRender : public AudioOutput {
 public:
  IOSAudioRender();
  virtual ~IOSAudioRender();

 private:
  std::mutex mtx;
  AudioComponentInstance audioUnit = nullptr;
  AudioDesc renderDesc = {};
  int32_t lastQueueMs = 0;
  uint32_t totalFrames = 0;
  uint32_t playedFrames = 0;

  // 环形缓冲区
  std::vector<uint8_t> ringBuffer;
  uint32_t writePos = 0;
  uint32_t readPos = 0;
  uint32_t bufferCapacity = 0;

#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif

  static OSStatus renderCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber, UInt32 inNumberFrames,
                                 AudioBufferList* ioData);

 protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData& frame) override;
  virtual void onClose() override;

 public:
  virtual bool empty() override;
  virtual int32_t getQueueMS() override;
  virtual bool full() override;
  virtual void pause(bool pause) override;
  virtual void flush() override; 
  virtual void speed(double speed) override;
  virtual void setVolume(float volume) override;
  virtual float getVolume() override;
};

// 将 AudioFormat 转换为 AudioStreamBasicDescription
AudioStreamBasicDescription audioFormatToASBD(const AudioDesc& desc,
                                              double sampleRate = 0);

}
