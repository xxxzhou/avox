#include "AndAudioSource.hpp"

#include "avox/module/AvoxManager.hpp"

namespace avox {

void regAndAudioDevice() {
  RegFunc regFunc = {"android audio device init", []() {
                       AvoxManager::Get().aDeviceMgr.regMgrObj(
                           ADeviceSdk::android, []() -> IAudioManager* {
                             return new AndAudioSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

AndAudioSource::AndAudioSource() {
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = 16000;
  desc.channels = 2;
  deviceName = "android audio source";
  deviceKind = ADeviceKind::mic;
}

AndAudioSource::~AndAudioSource() { close(); }

bool AndAudioSource::onOpen() {
  sessionId = 0;
  JNIEnv* env = AvoxManager::Get().getEnv();
  if (!env) {
    LOGFLF(LogLevel::warn, "not get jnienv");
    return false;
  }
  if (!jmAudioRecord.audioRecordClass) {
    LOGFLF(LogLevel::warn, "not find audio record class");
    return false;
  }
  // audioFormat ENCODING_PCM_8BIT 1 ENCODING_PCM_16BIT 2
  // channel CHANNEL_CONFIGURATION_MONO 16 CHANNEL_IN_STEREO 12
  int32_t channelConfig = desc.channels == 1 ? 16 : 12;
  minBufferSize = env->CallStaticIntMethod(jmAudioRecord.audioRecordClass,
                                           jmAudioRecord.getMinBufferSize,
                                           desc.sampleRate, channelConfig, 2);
  bufferSize = minBufferSize * 2;
  if (bufferSize <= 0) {
    bufferSize = getAudioFrameSize(desc, 80);
  }
  //   public AudioRecord(int audioSource, int sampleRateInHz, int
  //   channelConfig, int audioFormat, int bufferSizeInBytes)
  jobject jAudioRecord =
      env->NewObject(jmAudioRecord.audioRecordClass, jmAudioRecord.init, 0,
                     desc.sampleRate, channelConfig, 2, bufferSize);
  if (!jAudioRecord) {
    LOGFLF(LogLevel::warn, "not create audio record");
    return false;
  }
  // 申请java的byte数组
  jbyteArray temp_buff = env->NewByteArray(bufferSize);
  readBuf = (jbyteArray)env->NewGlobalRef(temp_buff);
  env->DeleteLocalRef(temp_buff);
  // 开始录音
  audioRecord = env->NewGlobalRef(jAudioRecord);
  env->DeleteLocalRef(jAudioRecord);
  env->CallVoidMethod(audioRecord, jmAudioRecord.startrecording);
  // 获取音频会话ID用于后续处理
  sessionId = env->CallIntMethod(audioRecord, jmAudioRecord.getAudioSessionId);
  LOGFLF(LogLevel::info, "start seeionid:", sessionId);
  startTask();
  return sessionId > 0;
}

void AndAudioSource::onRunTask() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  if (!env) {
    LOGFLF(LogLevel::warn, "not get jnienv");
    return;
  }
  if (!audioRecord) {
    LOGFLF(LogLevel::warn, "audio record is null");
    return;
  }
  while (running()) {
    jbyteArray localReadBuf = readBuf;   
    // 阻塞的,不用sleep
    int32_t readSize = env->CallIntMethod(audioRecord, jmAudioRecord.read,
                                          localReadBuf, 0, bufferSize);
    if (readSize > 0) {          
      // uint8_t* pByte = (uint8_t*)env->GetByteArrayElements(readBuf, 0);
      std::vector<uint8_t> tempBuffer(readSize);
      env->GetByteArrayRegion(localReadBuf, 0, readSize, (jbyte*)tempBuffer.data());
      AvoxAFrame frame = {};
      frame.buffer.data = tempBuffer.data();
      frame.buffer.size = readSize;
      frame.pts = timeStampMS();
      onFrame(frame);
      // env->ReleaseByteArrayElements(readBuf, (jbyte*)pByte, 0);
    } else if (readSize == 0) {
      sleepTask(true, 5);
    } else {
      // readSize < 0 代表錯誤 (如 ERROR_INVALID_OPERATION)
      LOGFLF(LogLevel::warn, "error:", readSize);
      dispatch(&IAudioSourceOb::onAudioError, AVError::deviceError,
               "read error");
      break;
    }
    // sleepTask(false, 10);
  }
}

void AndAudioSource::onClose() {
  sessionId = 0;
  stopTask();
  JNIEnv* env = AvoxManager::Get().getEnv();
  if (!env) {
    LOGFLF(LogLevel::warn, "not get jnienv");
    return;
  }
  if (readBuf) {
    env->DeleteGlobalRef(readBuf);
    readBuf = nullptr;
  }
  if (audioRecord) {
    env->CallVoidMethod(audioRecord, jmAudioRecord.stop);
    env->CallVoidMethod(audioRecord, jmAudioRecord.release);
    env->DeleteGlobalRef(audioRecord);
    audioRecord = nullptr;
  }
}

bool AndAudioSource::bOpening() { return sessionId > 0; }

AndAudioSourceMgr::AndAudioSourceMgr() { onInitDevices(); }

AndAudioSourceMgr::~AndAudioSourceMgr() {}

void AndAudioSourceMgr::onInitDevices() {
  devices.clear();
  std::shared_ptr<AndAudioSource> source = std::make_shared<AndAudioSource>();
  devices.push_back(source);
}

}