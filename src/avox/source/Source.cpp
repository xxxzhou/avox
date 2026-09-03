#include <string>
#include <vector>

#include "../AvoxSource.h"
#include "../module/AvoxManager.hpp"
#include "DeviceSource.hpp"

namespace avox {

const char* getAVErrorStr(AVError error) {
  switch (error) {
#define XX(name, value, str) \
  case AVError::name:        \
    return str;
    AVOX_MAP_AV_ERROR(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getAVSoureceModeStr(AVSourceMode mode) {
  switch (mode) {
#define XX(name, value, str) \
  case AVSourceMode::name:   \
    return str;
    AVOX_MAP_AV_SOURCE_MODE(XX)
#undef XX
    default:
      return "unknow";
  }
}

const char* getRawSourceTypeStr(RawSourceType type) {
  switch (type) {
#define XX(name, value, str) \
  case RawSourceType::name:  \
    return str;
    AVOX_MAP_RAW_SOURCE_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

IVideoManager* getVideoManager(VDeviceSdk sdk) {
  return AvoxManager::Get().vDeviceMgr.getMgr(sdk);
}

// 当前平台的默认视频设备 SDK (每平台只实现一个)
VDeviceSdk getDefaltVideoSdk() {
#ifdef _WIN32
  return VDeviceSdk::win_capture;
#elif __ANDROID__
  return VDeviceSdk::and_ndkcamer2;
#elif __APPLE__
  return VDeviceSdk::ios_avf;
#endif
  return VDeviceSdk::none;
}

IAudioManager* getAudioManager(ADeviceSdk sdk) {
  return AvoxManager::Get().aDeviceMgr.getMgr(sdk);
}

}