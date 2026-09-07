#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
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
// 轨道方向语义: 发送=设置了对应源且方向允许发; 接收=方向允许收(实际以协商结果为准)
class RtcParse : public RawSource,
                 public webrtc::PeerConnectionObserver,
                 public webrtc::RtpReceiverObserverInterface,
                 public webrtc::VideoSinkInterface<webrtc::VideoFrame>,
                 public webrtc::AudioTrackSinkInterface,
                 public webrtc::DataChannelObserver {
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
  // localSdp信令线程写/任意线程读
  std::mutex sdpMtx;
  std::string localSdp = "";
  // 本地SDP/ICE事件(信令线程回调; RtcPlayer设置, 转发agent+派发观察者)
  std::function<void(const char* localSdp)> localSdpCb = nullptr;
  std::function<void(const char* candidate, const char* mid, int mlineIndex)> iceCb = nullptr;
  RtcRollType rollType = RtcRollType::offer;
  std::vector<IceServer> iceServers;
  // 轨道方向与推流参数(open前配置)
  RtpDirection videoDirection = RtpDirection::sendRecv;
  RtpDirection audioDirection = RtpDirection::sendRecv;
  // open时算出的实际发送标记
  bool bSendVideo = false;
  bool bSendAudio = false;
  int32_t maxVideoBitrateKbps = 0;
  int32_t maxVideoFps = 0;
  std::string preferredVideoCodec = "";
  // DataChannel
  bool bEnableDataChannel = false;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel = nullptr;
  // 收到DataChannel消息(信令线程回调)
  std::function<void(const char* data, int32_t size)> dataChannelMsgCb = nullptr;
  // RTCP统计(RTCStatsCollectorCallback写, 任意线程读)
  std::atomic<int32_t> rttMs = -1;
  std::atomic<float> lossRate = 0.0f;
  std::atomic<int64_t> lastStatsTime = 0;

  // 语音源
  webrtc::scoped_refptr<RtcAudioSource> audioSource;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> localAudioTrack;
  // 视频源
  webrtc::scoped_refptr<RtcVideoSource> videoSource;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> localVideoTrack;
  // 发送端(设置码率/帧率用)
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> videoSender = nullptr;
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> audioSender = nullptr;
  // 使用 scoped_refptr 自动管理远端轨道生命周期
  std::vector<webrtc::scoped_refptr<webrtc::VideoTrackInterface>>
      remoteVideoTracks;
  std::vector<webrtc::scoped_refptr<webrtc::AudioTrackInterface>>
      remoteAudioTracks;
  // 连接状态(PeerConnectionObserver写, 播放线程轮询读)
  std::atomic<RtcConnState> connState = RtcConnState::init;

 public:
  // 设置Roll类型,不同类型流程不同
  void setRollType(RtcRollType type);
  void addIceServer(const char* uri, const char* username,
                    const char* password);
  // 设置本地SDP/ICE事件回调(信令线程回调)
  void setSdpEventCb(std::function<void(const char* localSdp)> localSdpCb_,
                     std::function<void(const char* candidate, const char* mid,
                                        int mlineIndex)> iceCb_) {
    localSdpCb = localSdpCb_;
    iceCb = iceCb_;
  }
  void setVideoDirection(RtpDirection direction);
  void setAudioDirection(RtpDirection direction);
  void setSendVideoBitrate(int32_t maxKbps);
  void setSendVideoFps(int32_t maxFps);
  void setPreferredVideoCodec(const char* codec);
  void setEnableDataChannel(bool bEnable);
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
  // DataChannel
  bool sendDataChannel(const char* data, int32_t size);
  // 设置DataChannel消息回调(信令线程回调)
  void setDataChannelMsgCb(
      std::function<void(const char* data, int32_t size)> cb) {
    dataChannelMsgCb = cb;
  }
  // 周期采集RTT/丢包(播放线程调用, 内部2秒节流)
  void pollStats();
  int32_t getRttMs() { return rttMs.load(std::memory_order_acquire); }
  float getLossRate() { return lossRate.load(std::memory_order_acquire); }
  RtcConnState getConnState() {
    return connState.load(std::memory_order_acquire);
  }
  // 本地推流源描述(onVideoDesc/onAudioDesc后有效)
  VideoDesc getLocalVideoDesc();
  AudioDesc getLocalAudioDesc();

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
  virtual void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
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

  // DataChannelObserver
 public:
  virtual void OnStateChange() override;
  virtual void OnMessage(const webrtc::DataBuffer& buffer) override;

 private:
  // 从远端SDP判断对端是否发送该媒体(recvonly/inactive为不发送)
  bool remoteSendsMedia(const std::string& sdp, const char* media);
  // 按远端SDP修正期望媒体, 修正对端只发单媒体时等待双通道就绪的问题
  void applyRemoteMediaExpectation(const std::string& sdp);
  // 置顶偏好的视频codec(SetCodecPreferences)
  void applyCodecPreference();
  // 应用推流码率/帧率(RtpSender参数)
  void applySendParams();
};

}
