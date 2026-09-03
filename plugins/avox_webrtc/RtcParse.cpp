#include "RtcParse.hpp"

#include "RtcEngine.hpp"
#include "avox/module/AvoxManager.hpp"
#ifdef __APPLE__
#include "avox_ios/IOSHelper.h"
#endif

namespace avox {

using namespace webrtc;

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
                            const char* password) {}

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
  PeerConnectionInterface::RTCConfiguration config = {};
  // 仅使用核心网络接口，忽略那些不可靠的虚拟接口
  config.type = webrtc::PeerConnectionInterface::kAll;
  config.continual_gathering_policy =
      webrtc::PeerConnectionInterface::GATHER_ONCE;
  // 用物理网卡，不使用 VPN
  config.candidate_network_policy = webrtc::PeerConnectionInterface::
      CandidateNetworkPolicy::kCandidateNetworkPolicyLowCost;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  if (iceServers.size() > 0) {
    for (auto& server : iceServers) {
      config.servers.push_back(server.getRtcIceServer());
    }
  } else {
    PeerConnectionInterface::IceServer ice_server = {};
    ice_server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(ice_server);
    ice_server.uri = "turn:122.9.78.240:8478";
    ice_server.username = "demo";
    ice_server.password = "123456";
    config.servers.push_back(ice_server);
  }
  //
  config.set_cpu_adaptation(false);
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  // 回调
  webrtc::PeerConnectionDependencies deps(this);
  auto result = factory->CreatePeerConnectionOrError(config, std::move(deps));
  if (!result.ok()) {
    LOGFLF(LogLevel::warn, "failed:", result.error().message());
    return false;
  }
  pc = result.MoveValue();
  if (videoSource) {
    localVideoTrack =
        factory->CreateVideoTrack("video_label", videoSource.get());
    // AddTrack自动创建Transceiver，先假定为sendrecv
    auto addResult = pc->AddTrack(localVideoTrack, {"stream_id"});
    if (!addResult.ok()) {
      LOGFLF(LogLevel::warn, "failed: ", addResult.error().message());
    }
  } else if (!bDisableVideo) {
    // 只有不推,但是要拉,kRecvOnly
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
    pc->AddTransceiver(webrtc::MediaType::MEDIA_TYPE_VIDEO, init);
  }
  if (audioSource) {
    localAudioTrack =
        factory->CreateAudioTrack("audio_label", audioSource.get());
    auto addResult = pc->AddTrack(localAudioTrack, {"stream_id"});
    if (!addResult.ok()) {
      LOGFLF(LogLevel::warn, "failed: ", addResult.error().message());
    }
  } else if (!bDisableAudio) {
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
    pc->AddTransceiver(webrtc::MediaType::MEDIA_TYPE_AUDIO, init);
  }
  // 如果是offer,先得到本地offer,再提交给服务器
  if (rollType == RtcRollType::offer) {
    createOffer();
  }
  videoTracks.clear();
  audioTracks.clear();
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

IAudioRender* RtcParse::getLocalAudioRender() {
  if (audioSource) {
  }
  return nullptr;
}

void RtcParse::close() {
  localSdp = "";
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

const char* RtcParse::getLocalSdp() { return localSdp.c_str(); }

void RtcParse::setTransceiverDirection() {
  for (auto transceiver : pc->GetTransceivers()) {
    auto media_type = transceiver->media_type();
    if (media_type == webrtc::MediaType::AUDIO) {
      if (localAudioTrack) {
        transceiver->SetDirection(
            bDisableAudio ? webrtc::RtpTransceiverDirection::kSendOnly
                          : webrtc::RtpTransceiverDirection::kSendRecv);
      } else {
        transceiver->SetDirection(
            bDisableAudio ? webrtc::RtpTransceiverDirection::kInactive
                          : webrtc::RtpTransceiverDirection::kRecvOnly);
      }
    } else if (media_type == webrtc::MediaType::VIDEO) {
      if (localVideoTrack) {
        transceiver->SetDirection(
            bDisableVideo ? webrtc::RtpTransceiverDirection::kSendOnly
                          : webrtc::RtpTransceiverDirection::kSendRecv);
      } else {
        transceiver->SetDirection(
            bDisableVideo ? webrtc::RtpTransceiverDirection::kInactive
                          : webrtc::RtpTransceiverDirection::kRecvOnly);
      }
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
  LOGFLF(LogLevel::info,
         "now signal state:", getSignalingStateState(pc->signaling_state()));
}

void RtcParse::createAnswer() {
  LOGFLF(LogLevel::info,
         "signal state:", getSignalingStateState(pc->signaling_state()));
  setTransceiverDirection();
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  pc->CreateAnswer(webrtc::make_ref_counted<CreateAnswerObserver>(this).get(),
                   options);
  LOGFLF(LogLevel::info,
         "now signal state:", getSignalingStateState(pc->signaling_state()));
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
  desc->ToString(&localSdp);
  LOGFLF(LogLevel::info, "set local sdp:", localSdp);
  if (!localSdp.empty()) {
    sdpOb->onLocalSdp(localSdp.c_str());
  }
}

void RtcParse::setRemoteSdp(const std::string& sdp) {
  if (!pc) {
    LOGFLF(LogLevel::warn, "pc not create");
    return;
  }
  webrtc::PeerConnectionInterface::SignalingState nowState =
      pc->signaling_state();
  LOGFLF(LogLevel::info, "set remote sdp:", sdp);
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
  LOGFLF(LogLevel::info, "webrtc onDataChannel");
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
      // dispatch(&IRawSourceOb::onError, AVError::endOfFile, stateStr);
      // dispatch(&IRawSourceOb::onReady);
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
    // 音频的渲染本身由webrtc处理,因此这是否有必要?
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
  // log(LogLevel::info, "webrtc onframe video:", nowTime);
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
  // log(LogLevel::info, "now ms:", timeStampMS(),
  //     " number_of_frames:", number_of_frames, " size:", aframe.buffer.size);
  // 这里的data还在，需要在onDecode用掉或是保存
  dispatch(&IRawSourceOb::onAudioFrame, aframe, 0);
  // log(LogLevel::info, "webrtc onframe audio");
}

}
