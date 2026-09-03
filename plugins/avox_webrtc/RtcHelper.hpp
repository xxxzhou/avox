#pragma once

#include "RtcExport.h"

// 添加系统网络头文件，解决socklen_t和AF_INET6等未定义问题
// 解决socklen_t未定义问题
#ifdef __APPLE__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

// WebRTC 源码头文件
#include <future>

#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_receiver_interface.h"
#include "api/video/video_frame.h"
#include "avox/AvoxCodec.h"
#include "avox/AvoxPlayer.h"
#include "avox/module/LogHelper.hpp"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"

namespace avox {

AudioFormat rtcAudioFromat(int32_t sample_bit);
const char* getIceConnectionState(
    webrtc::PeerConnectionInterface::IceConnectionState state);
const char* getIceGatheringState(
    webrtc::PeerConnectionInterface::IceGatheringState state);
const char* getSignalingStateState(
    webrtc::PeerConnectionInterface::SignalingState state);

class CreateOfferObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  CreateOfferObserver(class RtcParse* parse);
  virtual ~CreateOfferObserver() = default;

 private:
  class RtcParse* parse = nullptr;

 public:
  virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  virtual void OnFailure(webrtc::RTCError error) override;
};

class SetRemoteOfferObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  virtual void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override;
};

class SetLocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  virtual void OnSetLocalDescriptionComplete(webrtc::RTCError error) override;
};

class CreateAnswerObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  CreateAnswerObserver(class RtcParse* parse);
  virtual ~CreateAnswerObserver() = default;

 private:
  class RtcParse* parse = nullptr;

 public:
  virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  virtual void OnFailure(webrtc::RTCError error) override;
};

// 添加 SetRemoteAnswerObserver（用于 Answer 方的 SetRemoteDescription 回调）
class SetRemoteAnswerObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  SetRemoteAnswerObserver(class RtcParse* parse) : parse(parse) {}
  virtual void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override;

 private:
  class RtcParse* parse = nullptr;
};

struct IceServer {
 public:
  std::string uri = "";
  std::string username = "";
  std::string password = "";

  webrtc::PeerConnectionInterface::IceServer getRtcIceServer();
};

std::string getSDP();
std::string getLocalSDP();

}