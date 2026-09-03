#pragma once

#include "AndCommon.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/AudioSource.hpp"

namespace avox {

class AndAudioSource : public AudioSource, public RunTask {
public:
  AndAudioSource();
  virtual ~AndAudioSource();

protected:
  jobject audioRecord = nullptr;
  int32_t minBufferSize = 0;
  int32_t bufferSize = 0;
  jbyteArray readBuf = nullptr;
  int32_t sessionId = 0;
  // audioFormat ENCODING_PCM_16BIT ENCODING_PCM_8BIT
  // channel CHANNEL_CONFIGURATION_MONO
protected:
  virtual void onRunTask() override;

public:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;
};

class AndAudioSourceMgr : public AudioManager<AndAudioSource> {
public:
  AndAudioSourceMgr();
  virtual ~AndAudioSourceMgr();

protected:
  virtual void onInitDevices() override;
};

}
