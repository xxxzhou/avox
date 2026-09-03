#pragma once

#include "avox/AvoxCodec.h"
#include "ZlmExport.h"
#include <string>

namespace avox {

// ONVIF Backchannel 信息
struct BackchannelInfo {
  bool isOnvifBackchannel = false;
  std::string audioEncoding;  // "PCMA", "PCMU", "MPEG4-GENERIC", "G726-32"
  int32_t clockRate = 8000;
  int32_t payloadType = 0;
};

// ONVIF Backchannel 检测器
class OnvifBC {
 public:
  // 同步检测 RTSP URL 是否支持 ONVIF Backchannel
  static BackchannelInfo detect(const std::string& url,
                                 const std::string& user = "",
                                 const std::string& pwd = "",
                                 int32_t timeoutMs = 3000);
  // 从 SDP 中提取音频编码信息
  static void parseSdpAudio(const std::string& sdp, BackchannelInfo& info);

 private:
  static void parseUrl(const std::string& url, std::string& host, int32_t& port, std::string& path);
};

}

