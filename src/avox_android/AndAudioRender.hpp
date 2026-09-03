#pragma once

#include "AndCommon.hpp"
#include "avox/audio/AudioOutput.hpp"
#include "avox/module/RunTask.hpp"

namespace avox {

class AndAudioRender : public AudioOutput {
public:
  AndAudioRender();
  virtual ~AndAudioRender();

private:
  jobject audioTrack = nullptr;
  int64_t baseTime = 0;
  int64_t baseOffset = 0;

protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData &frame) override;
  virtual void onClose() override;
  int64_t getPlayPosition();

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

}