#include "MediaSourceInfo.hpp"

namespace avox {

const char* MediaSourceInfo::getVideoCodecName(int32_t index) {
  if (index < 0 || index >= (int32_t)videoCodecNames.size()) {
    return "";
  }
  return videoCodecNames[(size_t)index].c_str();
}

const char* MediaSourceInfo::getAudioCodecName(int32_t index) {
  if (index < 0 || index >= (int32_t)audioCodecNames.size()) {
    return "";
  }
  return audioCodecNames[(size_t)index].c_str();
}

void MediaSourceInfo::setContainerInfo(int64_t durationMsValue,
                                       int64_t bitRateValue,
                                       const std::string& formatNameValue,
                                       bool seekable) {
  containerDurationMs = durationMsValue;
  containerBitRate = bitRateValue;
  containerFormatName = formatNameValue;
  bSeekable = seekable;
}

void MediaSourceInfo::addVideoCodecName(const std::string& name) {
  videoCodecNames.push_back(name);
}

void MediaSourceInfo::addAudioCodecName(const std::string& name) {
  audioCodecNames.push_back(name);
}

}  // namespace avox
