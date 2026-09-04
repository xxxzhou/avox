#include "AndAudioRender.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"

namespace avox {

void regAndATRender() {
  RegFunc andATRenderReg = {
      "android audio track render init", []() {
        ARenderDesc renderDesc = {};
        renderDesc.name = "Android Audio Track Render";
        AvoxManager::Get().aRender.regInitFunc(
            ARenderType::androidAT, renderDesc,
            []() -> AudioRender* { return new AndAudioRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(andATRenderReg);
}

AndAudioRender::AndAudioRender() {}

AndAudioRender::~AndAudioRender() { close(); }

void AndAudioRender::onInit() {
  if (!desc.bValid()) {
    LOGFLF(LogLevel::warn, "desc is not valid");
    return;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  if (!env) {
    LOGFLF(LogLevel::warn, "not get jnienv");
    return;
  }
  if (!jmAudioTrack.audioTrackClass) {
    LOGFLF(LogLevel::warn, "not find audio track class");
    return;
  }
  jobject jAudioTrack =
      env->NewObject(jmAudioTrack.audioTrackClass, jmAudioTrack.init);
  if (!jAudioTrack) {
    LOGFLF(LogLevel::warn, "not create audio track");
    return;
  }
  int32_t bits = audioFormatSize(desc.format) * 8;
  int32_t status = env->CallIntMethod(jAudioTrack, jmAudioTrack.create,
                                      desc.sampleRate, desc.channels, bits);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
  if (status != 0) {
    // create失败(如声道数/位深不支持)时Java侧已Destroy, buffer为空, 不能再持有
    LOGFLF(LogLevel::warn, "audio track create fail, status:", status);
    env->DeleteLocalRef(jAudioTrack);
    return;
  }
  // 保持一个全局引用
  audioTrack = env->NewGlobalRef(jAudioTrack);
  env->DeleteLocalRef(jAudioTrack);
  // 开始播放
  env->CallVoidMethod(audioTrack, jmAudioTrack.start);
  //
  frameSize = env->CallIntMethod(audioTrack, jmAudioTrack.getFrameSize);
  if (frameSize <= 0) {
    frameSize = getAudioFrameSize(desc, 40);
    LOGFLF(LogLevel::warn,
           "get frame size fail, use default frame size:", frameSize);
  }
  log(LogLevel::info, "android audio track start,frameSize:", frameSize);
}

void AndAudioRender::onRender(const AvoxData& frame) {
  if (!audioTrack) {
    LOGFLF(LogLevel::warn, "audio track is null");
    return;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  // 获取要写入的音频数据区域
  jbyteArray jaudioBuffer =
      (jbyteArray)env->CallObjectMethod(audioTrack, jmAudioTrack.getDataBuffer);
  if (!jaudioBuffer) {
    LOGFLF(LogLevel::warn, "audio data buffer is null");
    return;
  }
  uint8_t* pAudioOutBuff = (uint8_t*)env->GetByteArrayElements(jaudioBuffer, 0);
  // 拷贝数据到缓冲区
  memcpy(pAudioOutBuff, frame.data, frame.size);
  // 将缓冲区内容复制回 Java 数组
  env->ReleaseByteArrayElements(jaudioBuffer, (jbyte*)pAudioOutBuff, 0);
  // 写入音频数据, AudioTrack.write是阻塞的
  int32_t lWriteLen =
      env->CallIntMethod(audioTrack, jmAudioTrack.write, frame.size);
  env->DeleteLocalRef(jaudioBuffer);
  int32_t msPre = desc.sampleRate / 1000;
  // getPosition返回是秒,去掉msPre,表示毫秒
  int64_t playTime = getPlayPosition();
  if (baseTime == 0) {
    baseTime = timeStampMS();
    baseOffset = baseTime - playTime;
  }
  int64_t now = timeStampMS();
  // offset应该差不多是0
  int32_t offset = playTime + baseOffset - now;
  // 差异比较大，重新调整一下基准
  if (std::abs(offset) > 200) {
    baseTime = timeStampMS();
    baseOffset = baseTime - playTime;
  }
}

int64_t AndAudioRender::getPlayPosition() {
  if (!audioTrack) {
    return 0;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  int32_t msPre = desc.sampleRate / 1000;
  // getPosition返回是秒,去掉msPre,表示毫秒
  return env->CallIntMethod(audioTrack, jmAudioTrack.getPosition) / msPre;
}

bool AndAudioRender::empty() {
  if (!audioTrack) {
    return true;
  }
  int64_t playTime = getPlayPosition();
  // 这个值应该和playTime差不多
  int64_t offset = timeStampMS() - baseOffset;
  // 相差太大，说明没有数据了
  if (offset - playTime > 200) {
    return true;
  }
  return false;
}

int32_t AndAudioRender::getQueueMS() { return 80; }

bool AndAudioRender::full() {
  if (!audioTrack) {
    return true;
  }
  // AudioTrack.write本身是阻塞的
  return false;
}

void AndAudioRender::pause(bool pause) {
  if (!audioTrack) {
    return;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  if (pause) {
    env->CallVoidMethod(audioTrack, jmAudioTrack.pause);
  } else {
    /* 恢复播放设备 */
    env->CallVoidMethod(audioTrack, jmAudioTrack.start);
  }
}

void AndAudioRender::flush() {
  if (!audioTrack) {
    return;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  env->CallVoidMethod(audioTrack, jmAudioTrack.flush);
}

void AndAudioRender::onClose() {
  if (audioTrack) {
    JNIEnv* env = AvoxManager::Get().getEnv();
    env->CallVoidMethod(audioTrack, jmAudioTrack.destroy);
    env->DeleteGlobalRef(audioTrack);
    audioTrack = nullptr;
  }
}

void AndAudioRender::speed(double speed) {}

void AndAudioRender::setVolume(float cvolume) {
  if (!audioTrack) {
    return;
  }
  JNIEnv* env = AvoxManager::Get().getEnv();
  // 确保音量在0.0到1.0范围内
  volume = std::max(0.0f, std::min(1.0f, cvolume));
  jint result = env->CallIntMethod(audioTrack, jmAudioTrack.setVolume, volume, volume);
  LOGFLF(LogLevel::info, "volume:", volume, " result:", result);
}

float AndAudioRender::getVolume() {
  if (!audioTrack) {
    return 1.0f;
  }
  return volume;
}

}
