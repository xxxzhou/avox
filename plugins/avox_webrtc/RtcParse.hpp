#pragma once

#include <future>
#include <iostream>
#include <vector>

#include "RtcHelper.hpp"
#include "audio/RtcAudioSource.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/JsonOption.hpp"
#include "avox/source/AVSource.hpp"
#include "avox/source/RawSource.hpp"
#include "video/RtcVideoSource.hpp"

namespace avox {

// 有长连接信令通道 → 客户端适合作为 Answer 方（被动等待）
// 没有长连接信令通道 → 客户端适合作为 Offer 方（主动请求）
// Answer/Offer,其SetLocalDescription/SetRemoteDescription顺序不同
class RtcParse : public RawSource,
                 public webrtc::PeerConnectionObserver,
                 public webrtc::RtpReceiverObserverInterface,
                 public webrtc::VideoSinkInterface<webrtc::VideoFrame>,
                 public webrtc::AudioTrackSinkInterface {
 public:
  RtcParse();
  virtual ~RtcParse();

 private:
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory =
      nullptr;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = nullptr;
  int64_t videoPts = 0;
  int64_t videoTime = 0;
  AudioDesc adesc = {};
  //
  std::string localSdp = "";
  ISdpAgentOb* sdpOb = nullptr;
  RtcRollType rollType = RtcRollType::offer;
  std::vector<IceServer> iceServers;

  // 语音源
  webrtc::scoped_refptr<RtcAudioSource> audioSource;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> localAudioTrack;
  // 视频源
  webrtc::scoped_refptr<RtcVideoSource> videoSource;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> localVideoTrack;
  // 使用 scoped_refptr 自动管理远端轨道生命周期
  std::vector<webrtc::scoped_refptr<webrtc::VideoTrackInterface>>
      remoteVideoTracks;
  std::vector<webrtc::scoped_refptr<webrtc::AudioTrackInterface>>
      remoteAudioTracks;

 public:
  // 设置Roll类型,不同类型流程不同
  void setRollType(RtcRollType type);
  void addIceServer(const char* uri, const char* username,
                            const char* password);
  void setSdpAgentOb(ISdpAgentOb* ob);
  // 在打开PC前,如果设置了音频源,则会创建推音频源
  void setAudioSource(IAudioSource* source);
  void setVideoSource(IVideoSource* source);
  WindowRender* getLocalSurfaceRender();
  IAudioRender* getLocalAudioRender();
  // 得到本地SDP
  const char* getLocalSdp();
  // 设置远程SDP
  void setRemoteSdp(const std::string& sdp);
  void addIceCandidate(const char* candidate, const char* mid, int mlineIndex);

 public:
  void setTransceiverDirection();
  void createOffer();
  void createAnswer();
  void onSetLocalSdp(webrtc::SessionDescriptionInterface* desc);

  // IRawSource
 public:
  // 初始化，打开文件/网络流
  virtual bool open() override;
  // 关闭
  virtual void close() override;
  virtual bool bOpening() override;

  // RTCPeerConnectionObserver 接口实现
 public:
  virtual void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override;
  virtual void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>
                                 data_channel) override;
  virtual void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  virtual void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
  virtual void OnIceCandidate(
      const webrtc::IceCandidateInterface* candidate) override;
  virtual void OnIceConnectionReceivingChange(bool receiving) override;
  virtual void OnAddStream(
      webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  virtual void OnRemoveStream(
      webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  virtual void OnRenegotiationNeeded() override;
  virtual void OnAddTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override;
  virtual void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

  // RtpReceiverObserverInterface
 public:
  virtual void OnFirstPacketReceived(webrtc::MediaType media_type) override;

  // VideoSinkInterface
 public:
  virtual void OnFrame(const webrtc::VideoFrame& frame) override;

  // AudioTrackSinkInterface
 public:
  virtual void OnData(const void* data, int bits_per_sample, int sample_rate,
                      size_t number_of_channels,
                      size_t number_of_frames) override;
};

}
