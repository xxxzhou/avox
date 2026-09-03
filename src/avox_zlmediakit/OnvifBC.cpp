#include "OnvifBC.hpp"

#include <thread>

#include "ZlmExport.h"
#include "avox/AvoxCodec.h"
#include "avox/AvoxLog.h"

extern "C" {
#include "mk_tcp.h"
}

namespace avox {

// 检测上下文，用于异步回调同步等待
class BackchannelContext {
 public:
  BackchannelInfo result;
  std::string response;
  std::string request;
  bool completed = false;
  mk_tcp_client client = nullptr;
  std::string host;
  int32_t port = 554;
};

// 静态回调函数
static void sOnConnect(mk_tcp_client client, int32_t code, const char* msg) {
  auto* ctx =
      static_cast<BackchannelContext*>(mk_tcp_client_get_user_data(client));
  if (code == 0) {
    mk_tcp_client_send(client, ctx->request.c_str(), (int32_t)ctx->request.size());
  } else {
    ctx->completed = true;
  }
}

static void sOnData(mk_tcp_client client, mk_buffer buffer) {
  auto* ctx =
      static_cast<BackchannelContext*>(mk_tcp_client_get_user_data(client));
  const char* data = mk_buffer_get_data(buffer);
  size_t size = mk_buffer_get_size(buffer);
  ctx->response.append(data, size);
  if (ctx->response.find("\r\n\r\n") != std::string::npos) {
    if (ctx->response.find("RTSP/1.0 200 OK") != std::string::npos &&
        ctx->response.find("a=sendonly") != std::string::npos) {
      ctx->result.isOnvifBackchannel = true;
      OnvifBC::parseSdpAudio(ctx->response, ctx->result);
    }
    ctx->completed = true;
  }
}

static void sOnDisconnect(mk_tcp_client client, int code, const char* msg) {
  auto* ctx =
      static_cast<BackchannelContext*>(mk_tcp_client_get_user_data(client));
  ctx->completed = true;
}

static void sOnManager(mk_tcp_client client) {
  // 超时管理由外层控制
}

void OnvifBC::parseUrl(const std::string& url, std::string& host, int32_t& port,
                       std::string& path) {
  port = 554;
  path = "/";
  size_t pos = url.find("rtsp://");
  if (pos == std::string::npos) {
    host = url;
    return;
  }
  std::string tmp = url.substr(7);
  size_t atPos = tmp.find('@');
  if (atPos != std::string::npos) {
    tmp = tmp.substr(atPos + 1);
  }
  size_t slashPos = tmp.find('/');
  size_t colonPos = tmp.find(':');
  if (colonPos != std::string::npos &&
      (slashPos == std::string::npos || colonPos < slashPos)) {
    host = tmp.substr(0, colonPos);
    std::string portStr;
    if (slashPos != std::string::npos) {
      portStr = tmp.substr(colonPos + 1, slashPos - colonPos - 1);
      path = tmp.substr(slashPos);
    } else {
      portStr = tmp.substr(colonPos + 1);
    }
    port = std::stoi(portStr);
  } else {
    if (slashPos != std::string::npos) {
      host = tmp.substr(0, slashPos);
      path = tmp.substr(slashPos);
    } else {
      host = tmp;
    }
  }
}

void OnvifBC::parseSdpAudio(const std::string& sdp, BackchannelInfo& result) {
  size_t sendonlyPos = sdp.find("a=sendonly");
  if (sendonlyPos == std::string::npos) {
    return;
  }
  size_t rtpmapPos = sdp.rfind("a=rtpmap:", sendonlyPos);
  if (rtpmapPos == std::string::npos) {
    rtpmapPos = sdp.find("a=rtpmap:", sendonlyPos);
  }
  if (rtpmapPos != std::string::npos) {
    size_t lineEnd = sdp.find("\r\n", rtpmapPos);
    if (lineEnd == std::string::npos) {
      lineEnd = sdp.find('\n', rtpmapPos);
    }
    std::string line = sdp.substr(rtpmapPos, lineEnd - rtpmapPos);
    size_t colonPos = line.find(':');
    size_t spacePos = line.find(' ');
    if (colonPos != std::string::npos && spacePos != std::string::npos) {
      result.payloadType =
          std::stoi(line.substr(colonPos + 1, spacePos - colonPos - 1));
      size_t slashPos = line.find('/');
      if (slashPos != std::string::npos) {
        result.audioEncoding =
            line.substr(spacePos + 1, slashPos - spacePos - 1);
        size_t rateStart = slashPos + 1;
        size_t rateEnd = line.find('/', rateStart);
        if (rateEnd == std::string::npos) {
          rateEnd = line.size();
        }
        result.clockRate =
            std::stoi(line.substr(rateStart, rateEnd - rateStart));
      }
    }
  }
  if (result.audioEncoding.empty()) {
    if (sdp.find("PCMA") != std::string::npos) {
      result.audioEncoding = "PCMA";
    } else if (sdp.find("PCMU") != std::string::npos) {
      result.audioEncoding = "PCMU";
    } else if (sdp.find("MPEG4-GENERIC") != std::string::npos) {
      result.audioEncoding = "MPEG4-GENERIC";
      result.clockRate = 16000;
    } else if (sdp.find("G726") != std::string::npos) {
      result.audioEncoding = "G726-32";
    }
  }
}

BackchannelInfo OnvifBC::detect(const std::string& url, const std::string& user,
                                const std::string& pwd, int timeoutMs) {
  BackchannelContext ctx;
  std::string path;
  parseUrl(url, ctx.host, ctx.port, path);
  ctx.request = "DESCRIBE " + url +
                " RTSP/1.0\r\n"
                "CSeq: 1\r\n"
                "Require: www.onvif.org/ver20/backchannel\r\n"
                "User-Agent: avplay\r\n"
                "Accept: application/sdp\r\n"
                "\r\n";
  mk_tcp_client_events events = {0};
  events.on_mk_tcp_client_connect = sOnConnect;
  events.on_mk_tcp_client_data = sOnData;
  events.on_mk_tcp_client_disconnect = sOnDisconnect;
  events.on_mk_tcp_client_manager = sOnManager;
  ctx.client = mk_tcp_client_create(&events, mk_type_tcp);
  mk_tcp_client_set_user_data(ctx.client, &ctx);
  float timeoutSec = timeoutMs / 1000.0f;
  mk_tcp_client_connect(ctx.client, ctx.host.c_str(), ctx.port, timeoutSec);
  int elapsed = 0;
  int sleepMs = 10;
  while (!ctx.completed && elapsed < timeoutMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    elapsed += sleepMs;
  }
  if (ctx.client) {
    mk_tcp_client_release(ctx.client);
  }
  return ctx.result;
}

bool checkOnvif(const char* url, ATrackDesc* trackDesc) {
  if (!url) {
    return false;
  }
  auto info = OnvifBC::detect(url);
  if (!info.isOnvifBackchannel) {
    return false;
  }
  if (trackDesc) {
    // 解析音频编码
    if (info.audioEncoding == "PCMA") {
      trackDesc->codecId = ACodecId::g711a;
    } else if (info.audioEncoding == "PCMU") {
      trackDesc->codecId = ACodecId::g711u;
    } else if (info.audioEncoding == "MPEG4-GENERIC") {
      trackDesc->codecId = ACodecId::aac;
    } else {
      trackDesc->codecId = ACodecId::g711a;  // 默认
    }
    trackDesc->desc.sampleRate = info.clockRate;
    trackDesc->desc.channels = 1;
    trackDesc->desc.format = AudioFormat::AVOX_AUDIO_S16;
  }
  return true;
}

}
