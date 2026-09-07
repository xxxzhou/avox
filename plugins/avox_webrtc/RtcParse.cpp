#include "RtcParse.hpp"

#include "RtcEngine.hpp"
#include "avox/module/AvoxManager.hpp"
#ifdef __APPLE__
#include "avox_apple/IOSHelper.h"
#endif
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace avox {

using namespace webrtc;

namespace {

// codec名大小写不敏感比较
bool bSameCodecName(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
      return false;
    }
  }
  return true;
}

// 轨道方向转webrtc方向: 发送还需实际有源(bSend)
RtpTransceiverDirection toRtcDirection(RtpDirection direction, bool bSend) {
  bool bRecv = direction == RtpDirection::recvOnly ||
               direction == RtpDirection::sendRecv;
  bool bCanSend = bSend && (direction == RtpDirection::sendOnly ||
                            direction == RtpDirection::sendRecv);
  if (bCanSend && bRecv) {
    return RtpTransceiverDirection::kSendRecv;
  }
  if (bCanSend) {
    return RtpTransceiverDirection::kSendOnly;
  }
  if (bRecv) {
    return RtpTransceiverDirection::kRecvOnly;
  }
  return RtpTransceiverDirection::kInactive;
}

// GetStats回调: 提取RTT与丢包率
class RtcStatsCallback : public RTCStatsCollectorCallback {
 public:
  RtcStatsCallback(std::atomic<int32_t>* rttMs_, std::atomic<float>* lossRate_)
      : rttMs(rttMs_), lossRate(lossRate_) {}
  virtual ~RtcStatsCallback() = default;

 private:
  std::atomic<int32_t>* rttMs = nullptr;
  std::atomic<float>* lossRate = nullptr;

 public:
  virtual void OnStatsDelivered(const scoped_refptr<const RTCStatsReport>&
                                    report) override {
    for (const auto* pair :
         report->GetStatsOfType<RTCIceCandidatePairStats>()) {
      if (pair->current_round_trip_time.has_value()) {
        rttMs->store((int32_t)(*pair->current_round_trip_time * 1000),
                     std::memory_order_release);
        break;
      }
    }
    int64_t lost = 0;
    int64_t recv = 0;
    for (const auto* stat :
         report->GetStatsOfType<RTCInboundRtpStreamStats>()) {
      if (stat->packets_lost.has_value()) {
        lost += *stat->packets_lost;
      }
      if (stat->packets_received.has_value()) {
        recv += *stat->packets_received;
      }
    }
    if (recv + lost > 0) {
      lossRate->store((float)((double)lost / (double)(recv + lost)),
                      std::memory_order_release);
    }
  }
};

}  // namespace

// regWebRtcRawSource 已移至 WebrtcModule::loadModule

RtcParse::RtcParse() {
  audioSource = webrtc::make_ref_counted<RtcAudioSource>();
  videoSource = webrtc::make_ref_counted<RtcVideoSource>();
}

RtcParse::~RtcParse() { close(); }

void RtcParse::setRollType(RtcRollType type) {
  if (pc != nullptr) {
    // 需要open之前,close之后才能设置
    LOGFLF(LogLevel::warn, "must close before setRollType");
    return;
  }
  rollType = type;
  LOGFLF(LogLevel::info,
         "roll type:", rollType == RtcRollType::offer ? "offer" : "answer");
}

void RtcParse::addIceServer(const char* uri, const char* username,
                            const char* password) {
  if (!uri) {
    return;
  }
  if (pc != nullptr) {
    LOGFLF(LogLevel::warn, "must close before addIceServer");
    return;
  }
  // 同uri去重,允许重复配置只保留一份
  for (auto& server : iceServers) {
    if (server.uri == uri) {
      return;
    }
  }
  IceServer server;
  server.uri = uri;
  server.username = username ? username : "";
  server.password = password ? password : "";
  iceServers.push_back(server);
}

void RtcParse::setVideoDirection(RtpDirection direction) {
  if (pc != nullptr) {
    LOGFLF(LogLevel::warn, "must close before setVideoDirection");
    return;
  }
  videoDirection = direction;
}

void RtcParse::setAudioDirection(RtpDirection direction) {
  if (pc != nullptr) {
    LOGFLF(LogLevel::warn, "must close before setAudioDirection");
    return;
  }
  audioDirection = direction;
}

void RtcParse::setSendVideoBitrate(int32_t maxKbps) {
  maxVideoBitrateKbps = maxKbps;
  // 已连接时立即生效
  applySendParams();
}

void RtcParse::setSendVideoFps(int32_t maxFps) {
  maxVideoFps = maxFps;
  applySendParams();
}

void RtcParse::setPreferredVideoCodec(const char* codec) {
  preferredVideoCodec = codec ? codec : "";
  if (pc != nullptr) {
    applyCodecPreference();
  }
}

void RtcParse::setEnableDataChannel(bool bEnable) {
  if (pc != nullptr) {
    LOGFLF(LogLevel::warn, "must close before setEnableDataChannel");
    return;
  }
  bEnableDataChannel = bEnable;
}

bool RtcParse::open() {
  if (!RtcEngine::Get().ensureInitialized()) {
    return false;
  }
  factory = RtcEngine::Get().getFactory();
  if (!factory) {
    LOGFLF(LogLevel::warn, "rtc peerconnection factory is null");
    return false;
  }
  LOGFLF(LogLevel::info, "start open webrtc source,type:",
         rollType == RtcRollType::offer ? "offer" : "answer");
  // 重置轨道/就绪状态与统计
  RawSource::open();
  connState.store(RtcConnState::init, std::memory_order_release);
  rttMs.store(-1, std::memory_order_release);
  lossRate.store(0.0f, std::memory_order_release);
  lastStatsTime.store(0, std::memory_order_release);
  PeerConnectionInterface::RTCConfiguration config = {};
  // 使用全部网络接口
  config.type = webrtc::PeerConnectionInterface::kAll;
  config.continual_gathering_policy =
      webrtc::PeerConnectionInterface::GATHER_ONCE;
  // 优先低成本网络类型(物理网卡)
  config.candidate_network_policy = webrtc::PeerConnectionInterface::
      CandidateNetworkPolicy::kCandidateNetworkPolicyLowCost;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  // ICE服务器: 用户配置优先, 未配置仅默认STUN(局域网/同网段可直连)
  if (iceServers.empty()) {
    PeerConnectionInterface::IceServer ice_server = {};
    ice_server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(ice_server);
  } else {
    for (auto& server : iceServers) {
      config.servers.push_back(server.getRtcIceServer());
    }
  }
  config.set_cpu_adaptation(false);
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  webrtc::PeerConnectionDependencies deps(this);
  auto result = factory->CreatePeerConnectionOrError(config, std::move(deps));
  if (!result.ok()) {
    LOGFLF(LogLevel::warn, "failed:", result.error().message());
    return false;
  }
  pc = result.MoveValue();
  // 发送=设置了对应源且方向允许发; 接收=方向允许收
  bSendVideo = videoSource && videoSource->hasSource() &&
               (videoDirection == RtpDirection::sendOnly ||
                videoDirection == RtpDirection::sendRecv);
  bSendAudio = audioSource && audioSource->hasSource() &&
               (audioDirection == RtpDirection::sendOnly ||
                audioDirection == RtpDirection::sendRecv);
  if (bSendVideo) {
    localVideoTrack =
        factory->CreateVideoTrack("video_label", videoSource.get());
    auto addResult = pc->AddTrack(localVideoTrack, {"stream_id"});
    if (!addResult.ok()) {
      LOGFLF(LogLevel::warn, "add video track failed: ",
             addResult.error().message());
    } else {
      videoSender = addResult.value();
    }
  } else if (videoDirection == RtpDirection::recvOnly ||
             videoDirection == RtpDirection::sendRecv) {
    // 只收不推,加recvonly收轨道
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
    pc->AddTransceiver(webrtc::MediaType::MEDIA_TYPE_VIDEO, init);
  }
  if (bSendAudio) {
    localAudioTrack =
        factory->CreateAudioTrack("audio_label", audioSource.get());
    auto addResult = pc->AddTrack(localAudioTrack, {"stream_id"});
    if (!addResult.ok()) {
      LOGFLF(LogLevel::warn, "add audio track failed: ",
             addResult.error().message());
    } else {
      audioSender = addResult.value();
    }
  } else if (audioDirection == RtpDirection::recvOnly ||
             audioDirection == RtpDirection::sendRecv) {
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
    pc->AddTransceiver(webrtc::MediaType::MEDIA_TYPE_AUDIO, init);
  }
  // offer方主动建DataChannel, answer方等OnDataChannel
  if (bEnableDataChannel && rollType == RtcRollType::offer) {
    webrtc::DataChannelInit init;
    dataChannel = pc->CreateDataChannel("avox", &init);
    if (dataChannel) {
      dataChannel->RegisterObserver(this);
    }
  }
  // 期望媒体按方向初始化,setRemoteSdp后按协商结果修正
  updateExpectVideo(videoDirection == RtpDirection::recvOnly ||
                    videoDirection == RtpDirection::sendRecv);
  updateExpectAudio(audioDirection == RtpDirection::recvOnly ||
                    audioDirection == RtpDirection::sendRecv);
  applyCodecPreference();
  applySendParams();
  // 如果是offer,先得到本地offer,再提交给服务器
  if (rollType == RtcRollType::offer) {
    createOffer();
  }
  LOGFLF(LogLevel::info, "open webrtc source,type:",
         rollType == RtcRollType::offer ? "offer" : "answer");
  return true;
}

void RtcParse::setSdpAgentOb(ISdpAgentOb* ob) {
  sdpOb = ob;
  if (sdpOb && !localSdp.empty()) {
    sdpOb->onLocalSdp(localSdp.c_str());
  }
}

void RtcParse::setAudioSource(IAudioSource* source) {
  if (!audioSource) {
    LOGFLF(LogLevel::warn, "audio source not create");
    return;
  }
  audioSource->setSource(source);
}

void RtcParse::setVideoSource(IVideoSource* source) {
  if (!videoSource) {
    LOGFLF(LogLevel::warn, "video source not create");
    return;
  }
  videoSource->setSource(source);
}

WindowRender* RtcParse::getLocalSurfaceRender() {
  if (videoSource) {
    return videoSource->getSurfaceRender();
  }
  return nullptr;
}

IAudioRender* RtcParse::getLocalAudioRender() { return nullptr; }

VideoDesc RtcParse::getLocalVideoDesc() {
  if (videoSource) {
    return videoSource->getVideoDesc();
  }
  return {};
}

AudioDesc RtcParse::getLocalAudioDesc() {
  if (audioSource) {
    return audioSource->getAudioDesc();
  }
  return {};
}

void RtcParse::close() {
  {
    std::lock_guard<std::mutex> lock(sdpMtx);
    localSdp = "";
  }
  if (dataChannel) {
    dataChannel->UnregisterObserver();
    dataChannel = nullptr;
  }
  videoSender = nullptr;
  audioSender = nullptr;
  connState.store(RtcConnState::closed, std::memory_order_release);
  if (videoSource) {
    videoSource->close();
  }
  if (audioSource) {
    audioSource->close();
  }
  for (auto& videoTrack : remoteVideoTracks) {
    if (videoTrack) {
      videoTrack->RemoveSink(this);
    }
  }
  remoteVideoTracks.clear();
  for (auto& audioTrack : remoteAudioTracks) {
    if (audioTrack) {
      audioTrack->RemoveSink(this);
    }
  }
  remoteAudioTracks.clear();
  if (pc != nullptr) {
    pc->Close();
    pc = nullptr;
  }
  localAudioTrack = nullptr;
  localVideoTrack = nullptr;
  factory = nullptr;
  RawSource::close();
}

bool RtcParse::bOpening() { return pc != nullptr; }

const char* RtcParse::getLocalSdp() {
  std::lock_guard<std::mutex> lock(sdpMtx);
  // 返回内部缓冲,调用方需立即拷贝
  return localSdp.c_str();
}

bool RtcParse::sendDataChannel(const char* data, int32_t size) {
  if (!dataChannel || !data || size <= 0) {
    return false;
  }
  return dataChannel->Send(
      webrtc::DataBuffer(webrtc::CopyOnWriteBuffer(data, size), true));
}

void RtcParse::pollStats() {
  if (!pc) {
    return;
  }
  // 2秒节流,避免并发GetStats
  int64_t now = timeStampMS();
  int64_t last = lastStatsTime.load(std::memory_order_relaxed);
  if (now - last < 2000) {
    return;
  }
  if (!lastStatsTime.compare_exchange_strong(last, now)) {
    return;
  }
  pc->GetStats(webrtc::make_ref_counted<RtcStatsCallback>(&rttMs, &lossRate)
                   .get());
}

bool RtcParse::remoteSendsMedia(const std::string& sdp, const char* media) {
  // 找m=<media>段, 段内recvonly/inactive为对端不发送, 默认sendrecv视为发送
  std::string tag = std::string("m=") + media + " ";
  size_t pos = sdp.find(tag);
  if (pos == std::string::npos) {
    return false;
  }
  size_t end = sdp.find("\nm=", pos + 1);
  if (end == std::string::npos) {
    end = sdp.size();
  }
  std::string section = sdp.substr(pos, end - pos);
  return section.find("a=recvonly") == std::string::npos &&
         section.find("a=inactive") == std::string::npos;
}

void RtcParse::applyRemoteMediaExpectation(const std::string& sdp) {
  updateExpectVideo(remoteSendsMedia(sdp, "video"));
  updateExpectAudio(remoteSendsMedia(sdp, "audio"));
}

void RtcParse::applyCodecPreference() {
  if (preferredVideoCodec.empty() || !pc || !factory) {
    return;
  }
  auto capabilities =
      factory->GetRtpSenderCapabilities(webrtc::MediaType::MEDIA_TYPE_VIDEO);
  // 偏好的codec置顶,其余保持原相对顺序
  std::vector<webrtc::RtpCodecCapability> ordered;
  for (auto& codec : capabilities.codecs) {
    if (bSameCodecName(codec.name, preferredVideoCodec)) {
      ordered.push_back(codec);
    }
  }
  if (ordered.empty()) {
    LOGFLF(LogLevel::warn, "preferred codec not support:", preferredVideoCodec);
    return;
  }
  for (auto& codec : capabilities.codecs) {
    if (!bSameCodecName(codec.name, preferredVideoCodec)) {
      ordered.push_back(codec);
    }
  }
  for (auto transceiver : pc->GetTransceivers()) {
    if (transceiver->media_type() == webrtc::MediaType::MEDIA_TYPE_VIDEO) {
      auto error = transceiver->SetCodecPreferences(ordered);
      if (!error.ok()) {
        LOGFLF(LogLevel::warn, "set codec preference failed: ",
               error.message());
      }
    }
  }
}

void RtcParse::applySendParams() {
  if (!videoSender) {
    return;
  }
  webrtc::RtpParameters params = videoSender->GetParameters();
  if (params.encodings.empty()) {
    return;
  }
  if (maxVideoBitrateKbps > 0) {
    params.encodings[0].max_bitrate_bps = maxVideoBitrateKbps * 1000;
  }
  if (maxVideoFps > 0) {
    params.encodings[0].max_framerate = maxVideoFps;
  }
  auto error = videoSender->SetParameters(params);
  if (!error.ok()) {
    LOGFLF(LogLevel::warn, "set send params failed: ", error.message());
  }
}

void RtcParse::setTransceiverDirection() {
  for (auto transceiver : pc->GetTransceivers()) {
    auto media_type = transceiver->media_type();
    if (media_type == webrtc::MediaType::AUDIO) {
      transceiver->SetDirection(toRtcDirection(audioDirection, bSendAudio));
    } else if (media_type == webrtc::MediaType::VIDEO) {
      transceiver->SetDirection(toRtcDirection(videoDirection, bSendVideo));
    }
  }
}

void RtcParse::createOffer() {
  LOGFLF(LogLevel::info,
         "signal state:", getSignalingStateState(pc->signaling_state()));
  setTransceiverDirection();
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  pc->CreateOffer(webrtc::make_ref_counted<CreateOfferObserver>(this).get(),
                  options);
}

void RtcParse::createAnswer() {
  LOGFLF(LogLevel::info,
         "signal state:", getSignalingStateState(pc->signaling_state()));
  setTransceiverDirection();
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  pc->CreateAnswer(webrtc::make_ref_counted<CreateAnswerObserver>(this).get(),
                   options);
}

void RtcParse::onSetLocalSdp(webrtc::SessionDescriptionInterface* desc) {
  if (!pc) {
    LOGFLF(LogLevel::warn, "pc not create");
    return;
  }
  // 设置本地描述
  pc->SetLocalDescription(
      absl::WrapUnique(desc),
      webrtc::make_ref_counted<SetLocalDescriptionObserver>());
  std::string sdpStr;
  desc->ToString(&sdpStr);
  {
    std::lock_guard<std::mutex> lock(sdpMtx);
    localSdp = sdpStr;
  }
  LOGFLF(LogLevel::info, "set local sdp:", sdpStr);
  if (sdpOb && !sdpStr.empty()) {
    sdpOb->onLocalSdp(sdpStr.c_str());
  }
}

void RtcParse::setRemoteSdp(const std::string& sdp) {
  if (!pc) {
    LOGFLF(LogLevel::warn, "pc not create");
    return;
  }
  LOGFLF(LogLevel::info, "set remote sdp:", sdp);
  // 按远端SDP修正期望媒体,对端只发单媒体时不再等双通道就绪
  applyRemoteMediaExpectation(sdp);
  webrtc::PeerConnectionInterface::SignalingState nowState =
      pc->signaling_state();
  if (rollType == RtcRollType::answer) {
    if (nowState != webrtc::PeerConnectionInterface::kStable) {
      LOGFLF(LogLevel::warn,
             "pc expected signal state is stale,but now signal state:",
             getSignalingStateState(nowState));
    }
    // Answer 方：先设置远程 Offer，然后创建 Answer
    std::unique_ptr<webrtc::SessionDescriptionInterface> offer(
        webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp));
    pc->SetRemoteDescription(
        std::move(offer),
        webrtc::make_ref_counted<SetRemoteAnswerObserver>(this));
  } else {
    if (nowState != webrtc::PeerConnectionInterface::kHaveLocalOffer) {
      LOGFLF(
          LogLevel::warn,
          "pc expected signal state is have local offer,but now signal state:",
          getSignalingStateState(nowState));
    }
    // Offer 方：接收远程 Answer
    std::unique_ptr<webrtc::SessionDescriptionInterface> answer(
        webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp));
    pc->SetRemoteDescription(
        std::move(answer), webrtc::make_ref_counted<SetRemoteOfferObserver>());
  }
}

void RtcParse::addIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) {
  if (!pc || !candidate) {
    return;
  }
  webrtc::SdpParseError error;
  auto iceCandidate = std::unique_ptr<webrtc::IceCandidateInterface>(
      webrtc::CreateIceCandidate(mid, mlineIndex, candidate, &error));
  if (!iceCandidate) {
    LOGFLF(LogLevel::warn,
           "failed to parse ice candidate: ", error.description);
    return;
  }
  pc->AddIceCandidate(iceCandidate.get());
  LOGFLF(LogLevel::info, "added ice candidate: ", candidate);
}

void RtcParse::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState new_state) {
  LOGFLF(LogLevel::info, "new state:", getSignalingStateState(new_state));
}

void RtcParse::OnDataChannel(
    webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  LOGFLF(LogLevel::info, "webrtc onDataChannel:",
         data_channel->label().c_str());
  // answer方采用远端创建的通道
  dataChannel = data_channel;
  dataChannel->RegisterObserver(this);
}

void RtcParse::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  const char* stateStr = getIceConnectionState(new_state);
  LOGFLF(LogLevel::info, stateStr);
  switch (new_state) {
    case webrtc::PeerConnectionInterface::IceConnectionState::
        kIceConnectionDisconnected: {
      dispatch(&IRawSourceOb::onError, AVError::devcieLost, stateStr);
      break;
    }
    case webrtc::PeerConnectionInterface::IceConnectionState::
        kIceConnectionFailed: {
      dispatch(&IRawSourceOb::onError, AVError::deviceError, stateStr);
      break;
    }
    case webrtc::PeerConnectionInterface::IceConnectionState::
        kIceConnectionCompleted: {
      break;
    }
    case webrtc::PeerConnectionInterface::IceConnectionState::
        kIceConnectionClosed: {
      dispatch(&IRawSourceOb::onError, AVError::deviceClose, stateStr);
      break;
    }
    default:
      break;
  }
}

void RtcParse::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
  RtcConnState state = RtcConnState::init;
  switch (new_state) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
      state = RtcConnState::init;
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      state = RtcConnState::connecting;
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      state = RtcConnState::connected;
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
      state = RtcConnState::disconnected;
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      state = RtcConnState::failed;
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      state = RtcConnState::closed;
      break;
    default:
      break;
  }
  connState.store(state, std::memory_order_release);
  LOGFLF(LogLevel::info, "rtc connection state:", getRtcConnStateStr(state));
}

void RtcParse::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState new_state) {
  log(LogLevel::info,
      "webrtc OnIceGatheringChange:", getIceGatheringState(new_state));
}

void RtcParse::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  if (candidate) {
    std::string candidate_str;
    std::string sdp_mid;
    int sdp_mline_index = 0;
    candidate->ToString(&candidate_str);
    sdp_mid = candidate->sdp_mid();
    sdp_mline_index = candidate->sdp_mline_index();
    if (sdpOb) {
      sdpOb->onIceCandidate(candidate_str.c_str(), sdp_mid.c_str(),
                            sdp_mline_index);
    }
    avox::log(avox::LogLevel::info, "get ice candidate: ", candidate_str);
  } else {
    avox::log(avox::LogLevel::warn, "get null ice candidate");
    // 空表示收集完毕
    if (sdpOb) {
      sdpOb->onIceCandidate(nullptr, nullptr, 0);
    }
  }
}

void RtcParse::OnIceConnectionReceivingChange(bool receiving) {}

void RtcParse::OnAddStream(
    webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  log(LogLevel::info, "webrtc onAddStream");
}

void RtcParse::OnRemoveStream(
    webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  log(LogLevel::info, "webrtc onRemoveStream");
}

void RtcParse::OnRenegotiationNeeded() {}

void RtcParse::OnAddTrack(
    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&
        streams) {
  auto track = receiver->track();
  if (!track) {
    return;
  }
  LOGFLF(LogLevel::info, "track kind: ", track->kind().c_str());
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind &&
      !bDisableVideo) {
    webrtc::VideoTrackInterface* videoTrack =
        static_cast<webrtc::VideoTrackInterface*>(track.get());
    videoTrack->AddOrUpdateSink(this, webrtc::VideoSinkWants());
    remoteVideoTracks.push_back(
        webrtc::scoped_refptr<webrtc::VideoTrackInterface>(videoTrack));
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind &&
             !bDisableAudio) {
    webrtc::AudioTrackInterface* audioTrack =
        static_cast<webrtc::AudioTrackInterface*>(track.get());
    // 不调用这个,下面的RtcParse::OnData音频数据不会回调
    audioTrack->AddSink(this);
    remoteAudioTracks.push_back(
        webrtc::scoped_refptr<webrtc::AudioTrackInterface>(audioTrack));
  }
}

void RtcParse::OnRemoveTrack(
    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  auto track = receiver->track();
  if (!track) {
    return;
  }
  std::string kind = track->kind();
  LOGFLF(LogLevel::info, "webrtc onRemoveTrack: ", kind.c_str());
  if (kind == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto videoTrack = static_cast<webrtc::VideoTrackInterface*>(track.get());
    videoTrack->RemoveSink(this);
    auto it = std::find(remoteVideoTracks.begin(), remoteVideoTracks.end(),
                        videoTrack);
    if (it != remoteVideoTracks.end()) {
      remoteVideoTracks.erase(it);
    }
  } else if (kind == webrtc::MediaStreamTrackInterface::kAudioKind) {
    auto audioTrack = static_cast<webrtc::AudioTrackInterface*>(track.get());
    audioTrack->RemoveSink(this);
    auto it = std::find(remoteAudioTracks.begin(), remoteAudioTracks.end(),
                        audioTrack);
    if (it != remoteAudioTracks.end()) {
      remoteAudioTracks.erase(it);
    }
  }
}

void RtcParse::OnFirstPacketReceived(webrtc::MediaType media_type) {
  Timespan nowTime = {timeStampMS() * 10000};
  if (media_type == webrtc::MediaType::VIDEO) {
    log(LogLevel::info, "webrtc OnFirstPacketReceived video:", nowTime);
  } else if (media_type == webrtc::MediaType::AUDIO) {
    log(LogLevel::info, "webrtc OnFirstPacketReceived audio:", nowTime);
  }
}

void RtcParse::OnFrame(const webrtc::VideoFrame& frame) {
  if (videoTracks.size() == 0) {
    VideoDesc vdesc = {};
    vdesc.width = frame.width();
    vdesc.height = frame.height();
    setVideoDesc(vdesc);
  }
  if (!bReady) {
    return;
  }
  int64_t ms = frame.render_time_ms();
  videoPts = ms;
  videoTime = timeStampMS();
  auto frameBuffer = frame.video_frame_buffer();
  auto rtcBuffer = static_cast<RtcVideoBuffer*>(frameBuffer.get());
  if (!rtcBuffer) {
    return;
  }
  // 硬解
  if (frameBuffer->type() == webrtc::VideoFrameBuffer::Type::kNative) {
    GpuFrame gpuFrame = {};
    bool bGet = rtcBuffer->toFrame(gpuFrame);
    if (!bGet) {
      log(LogLevel::info, "webrtc onframe get gpu frame fail");
      return;
    }
    gpuFrame.pts = frame.render_time_ms();
    // 给渲染器渲染
    dispatch(&IRawSourceOb::onGpuFrame, gpuFrame, 0);
    // android的buffer渲染后,要通知一下已释放
    rtcBuffer->use();
  } else {
    YUVFrame yuvFrame = {};
    bool bGet = rtcBuffer->toFrame(yuvFrame);
    if (!bGet) {
      log(LogLevel::info, "webrtc onframe get yuv frame fail");
      return;
    }
    yuvFrame.pts = frame.render_time_ms();
    dispatch(&IRawSourceOb::onVideoFrame, yuvFrame, 0);
  }
}

void RtcParse::OnData(const void* data, int bits_per_sample, int sample_rate,
                      size_t number_of_channels, size_t number_of_frames) {
  if (audioTracks.size() == 0) {
    adesc.channels = number_of_channels;
    adesc.sampleRate = sample_rate;
    adesc.format = AudioFormat::AVOX_AUDIO_S16;
    setAudioDesc(adesc);
  }
  if (!bReady) {
    return;
  }
  // 音频会由webrtc音频模块自动处理
  // 返回音频数据的毫秒数
  int32_t frameMs = number_of_frames * 1000 / sample_rate;
  AvoxAFrame aframe = {};
  aframe.pts = videoPts + timeStampMS() - videoTime;
  aframe.buffer.data = (uint8_t*)data;
  aframe.buffer.size = getAudioFrameSize(adesc, frameMs);
  aframe.buffer.bRef = true;
  // 这里的data还在，需要在onDecode用掉或是保存
  dispatch(&IRawSourceOb::onAudioFrame, aframe, 0);
}

void RtcParse::OnStateChange() {
  if (dataChannel) {
    LOGFLF(LogLevel::info, "data channel state:", (int32_t)dataChannel->state());
  }
}

void RtcParse::OnMessage(const webrtc::DataBuffer& buffer) {
  if (dataChannelMsgCb) {
    dataChannelMsgCb((const char*)buffer.data.data(),
                     (int32_t)buffer.data.size());
  }
}

}
