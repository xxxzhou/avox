#include "RtcHelper.hpp"

#include "RtcExport.h"
#include "RtcParse.hpp"
#include "RtcPlayer.hpp"
#include "avox/AvoxCodec.h"
#include "avox/module/Json.hpp"
#include "avox/player/SourcePlayer.hpp"

using namespace webrtc;

namespace avox {

const char* getIceConnectionState(
    webrtc::PeerConnectionInterface::IceConnectionState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return "new";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return "checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return "connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return "completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return "failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return "disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return "closed";
    default:
      return "unknown";
  }
}

const char* getIceGatheringState(
    webrtc::PeerConnectionInterface::IceGatheringState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
      return "new";
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
      return "gathering";
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
      return "complete";
    default:
      return "unknown";
  }
}

const char* getSignalingStateState(
    webrtc::PeerConnectionInterface::SignalingState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::kStable:
      return "stable";
    case webrtc::PeerConnectionInterface::kHaveLocalOffer:
      return "have-local-offer";
    case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
      return "have-local-pranswer";
    case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
      return "have-remote-offer";
    case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
      return "have-remote-pranswer";
    case webrtc::PeerConnectionInterface::kClosed:
      return "closed";
    default:
      return "unknown";
  }
}

webrtc::PeerConnectionInterface::IceServer IceServer::getRtcIceServer() {
  webrtc::PeerConnectionInterface::IceServer server = {};
  server.uri = uri;
  if (!username.empty()) {
    server.username = username;
  }
  if (!password.empty()) {
    server.password = password;
  }
  return server;
}

// createWebRtcPlayer 已移至核心层 RtcPlayerApi.cpp (走 AvoxManager 工厂)
// addRtcPlayerOb/removeRtcPlayerOb 已移至核心层 Player.cpp (dynamic_cast<BasePlayer*> cross-cast)

AudioFormat rtcAudioFromat(int32_t sample_bit) {
  if (sample_bit == 16) {
    return AudioFormat::AVOX_AUDIO_S16;
  } else if (sample_bit == 8) {
    return AudioFormat::AVOX_AUDIO_U8;
  }
  return AudioFormat::other;
}

CreateOfferObserver::CreateOfferObserver(RtcParse* parse_) : parse(parse_) {}

void CreateOfferObserver::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  parse->onSetLocalSdp(desc);
}

void CreateOfferObserver::OnFailure(webrtc::RTCError error) {
  LOGFLF(LogLevel::warn, error.message());
}

void SetRemoteOfferObserver::OnSetRemoteDescriptionComplete(
    webrtc::RTCError error) {
  LOGFLF(LogLevel::warn, error.message());
}

void SetLocalDescriptionObserver::OnSetLocalDescriptionComplete(
    webrtc::RTCError error) {
  LOGFLF(LogLevel::warn, error.message());
}

CreateAnswerObserver::CreateAnswerObserver(RtcParse* parse_) : parse(parse_) {}

void CreateAnswerObserver::OnSuccess(
    webrtc::SessionDescriptionInterface* desc) {
  parse->onSetLocalSdp(desc);
}

void CreateAnswerObserver::OnFailure(webrtc::RTCError error) {
  LOGFLF(LogLevel::warn, error.message());
}

void SetRemoteAnswerObserver::OnSetRemoteDescriptionComplete(
    webrtc::RTCError error) {
  if (error.ok()) {
    // SetRemoteDescription 成功，现在可以创建 Answer
    parse->createAnswer();
  } else {
    LOGFLF(LogLevel::error, error.message());
  }
}

std::string getSDP() {
  std::ostringstream sdp;

  // 向sdp输出流中添加SDP协议版本行，固定为v=0，表示SDP协议版本为0
  sdp << "v=0" << std::endl;
  // 向sdp输出流中添加会话发起者和会话标识信息
  sdp << "o=- 8056465047193717911 2 IN IP4 127.0.0.1" << std::endl;
  // 向sdp输出流中添加会话名称，这里为空
  sdp << "s=-" << std::endl;
  // 向sdp输出流中添加会话活动时间，这里表示持续时间为0
  sdp << "t=0 0" << std::endl;
  // 向sdp输出流中添加捆绑多个媒体流信息，这里捆绑了0、1和2三个媒体流
  // sdp << "a=group:BUNDLE 0 1 2" << std::endl;
  sdp << "a=group:BUNDLE 0" << std::endl;
  // 向sdp输出流中添加允许混合使用不同的扩展头信息
  sdp << "a=extmap-allow-mixed" << std::endl;
  // 向sdp输出流中添加指定媒体流的语义信息，这里使用WMS（WebRTC Media Stream）
  sdp << "a=msid-semantic: WMS" << std::endl;

  // 视频部分
  // 定义视频媒体流信息，包括端口、传输协议、编码格式等
  sdp << "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101 35 36 37 38 103 104 "
         "107 108 109 114 115 116 117 118 39 40 41 42 43 44 45 46 47 48 119 "
         "120 121 122 123 124 125 49"
      << std::endl;
  // 指定视频媒体流的网络地址和类型
  sdp << "c=IN IP4 0.0.0.0" << std::endl;
  // 指定RTCP（Real-time Transport Control Protocol）的端口和网络地址
  sdp << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
  // ICE（Interactive Connectivity Establishment）用户名片段
  sdp << "a=ice-ufrag:R7DT" << std::endl;
  // ICE密码
  sdp << "a=ice-pwd:S5AnXLnQKmupL7NxNovGks84" << std::endl;
  // ICE选项，使用trickle模式
  sdp << "a=ice-options:trickle" << std::endl;
  // 指纹信息，用于验证DTLS（Datagram Transport Layer Security）会话
  sdp << "a=fingerprint:sha-256 "
         "C1:B4:31:ED:D9:81:C5:1B:B4:C4:C9:A0:FE:09:7A:19:DD:C1:54:9A:E2:94:CD:"
         "D3:B8:F1:11:3B:8C:6E:4C:7C"
      << std::endl;
  // DTLS连接设置模式，actpass表示主动-被动模式
  sdp << "a=setup:actpass" << std::endl;
  // 媒体流的标识符
  sdp << "a=mid:0" << std::endl;
  // RTP扩展头映射，时间偏移
  sdp << "a=extmap:1 urn:ietf:params:rtp-hdrext:toffset" << std::endl;
  // RTP扩展头映射，绝对发送时间
  sdp << "a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
      << std::endl;
  // RTP扩展头映射，视频方向
  sdp << "a=extmap:3 urn:3gpp:video-orientation" << std::endl;
  // RTP扩展头映射，传输层拥塞控制扩展
  sdp << "a=extmap:4 "
         "http://www.ietf.org/id/"
         "draft-holmer-rmcat-transport-wide-cc-extensions-01"
      << std::endl;
  // RTP扩展头映射，播放延迟
  sdp << "a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"
      << std::endl;
  // RTP扩展头映射，视频内容类型
  sdp << "a=extmap:6 "
         "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type"
      << std::endl;
  // RTP扩展头映射，视频时间信息
  sdp << "a=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/video-timing"
      << std::endl;
  // RTP扩展头映射，颜色空间
  sdp << "a=extmap:8 http://www.webrtc.org/experiments/rtp-hdrext/color-space"
      << std::endl;
  // RTP扩展头映射，媒体流标识
  sdp << "a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid" << std::endl;
  // RTP扩展头映射，RTP流标识
  sdp << "a=extmap:10 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
      << std::endl;
  // RTP扩展头映射，修复后的RTP流标识
  sdp << "a=extmap:11 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"
      << std::endl;
  // 媒体流接收模式，仅接收
  sdp << "a=recvonly" << std::endl;
  // RTCP复用
  sdp << "a=rtcp-mux" << std::endl;
  // RTCP报告大小
  sdp << "a=rtcp-rsize" << std::endl;
  // RTP映射，VP8编码，时钟频率90000Hz
  sdp << "a=rtpmap:96 VP8/90000" << std::endl;
  // RTCP反馈，Google REMB（Receiver Estimated Maximum Bitrate）
  sdp << "a=rtcp-fb:96 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:96 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:96 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:96 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:96 nack pli" << std::endl;
  // RTP映射，RTX（Retransmission），时钟频率90000Hz
  sdp << "a=rtpmap:97 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:97 apt=96" << std::endl;
  // RTP映射，VP9编码，时钟频率90000Hz
  sdp << "a=rtpmap:98 VP9/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:98 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:98 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:98 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:98 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:98 nack pli" << std::endl;
  // VP9编码参数，profile-id为0
  sdp << "a=fmtp:98 profile-id=0" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:99 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:99 apt=98" << std::endl;
  // RTP映射，VP9编码，时钟频率90000Hz
  sdp << "a=rtpmap:100 VP9/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:100 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:100 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:100 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:100 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:100 nack pli" << std::endl;
  // VP9编码参数，profile-id为2
  sdp << "a=fmtp:100 profile-id=2" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:101 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:101 apt=100" << std::endl;
  // RTP映射，VP9编码，时钟频率90000Hz
  sdp << "a=rtpmap:35 VP9/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:35 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:35 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:35 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:35 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:35 nack pli" << std::endl;
  // VP9编码参数，profile-id为1
  sdp << "a=fmtp:35 profile-id=1" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:36 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:36 apt=35" << std::endl;
  // RTP映射，VP9编码，时钟频率90000Hz
  sdp << "a=rtpmap:37 VP9/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:37 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:37 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:37 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:37 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:37 nack pli" << std::endl;
  // VP9编码参数，profile-id为3
  sdp << "a=fmtp:37 profile-id=3" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:38 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:38 apt=37" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:103 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:103 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:103 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:103 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:103 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:103 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:103 "
         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
         "42001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:104 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:104 apt=103" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:107 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:107 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:107 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:107 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:107 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:107 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:107 "
         "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id="
         "42001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:108 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:108 apt=107" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:109 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:109 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:109 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:109 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:109 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:109 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:109 "
         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
         "42e01f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:114 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:114 apt=109" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:115 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:115 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:115 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:115 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:115 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:115 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:115 "
         "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id="
         "42e01f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:116 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:116 apt=115" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:117 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:117 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:117 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:117 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:117 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:117 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:117 "
         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
         "4d001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:118 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:118 apt=117" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:39 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:39 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:39 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:39 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:39 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:39 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:39 "
         "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id="
         "4d001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:40 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:40 apt=39" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:41 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:41 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:41 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:41 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:41 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:41 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:41 "
         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
         "f4001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:42 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:42 apt=41" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:43 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:43 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:43 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:43 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:43 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:43 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:43 "
         "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id="
         "f4001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:44 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:44 apt=43" << std::endl;
  // RTP映射，AV1编码，时钟频率90000Hz
  sdp << "a=rtpmap:45 AV1/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:45 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:45 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:45 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:45 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:45 nack pli" << std::endl;
  // AV1编码参数，包括级别索引、配置文件、层级
  sdp << "a=fmtp:45 level-idx=5;profile=0;tier=0" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:46 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:46 apt=45" << std::endl;
  // RTP映射，AV1编码，时钟频率90000Hz
  sdp << "a=rtpmap:47 AV1/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:47 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:47 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:47 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:47 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:47 nack pli" << std::endl;
  // AV1编码参数，包括级别索引、配置文件、层级
  sdp << "a=fmtp:47 level-idx=5;profile=1;tier=0" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:48 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:48 apt=47" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:119 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:119 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:119 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:119 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:119 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:119 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:119 "
         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
         "64001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:120 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:120 apt=119" << std::endl;
  // RTP映射，H264编码，时钟频率90000Hz
  sdp << "a=rtpmap:121 H264/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:121 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:121 transport-cc" << std::endl;
  // RTCP反馈，快速重传请求
  sdp << "a=rtcp-fb:121 ccm fir" << std::endl;
  // RTCP反馈，丢包通知
  sdp << "a=rtcp-fb:121 nack" << std::endl;
  // RTCP反馈，丢包请求重传
  sdp << "a=rtcp-fb:121 nack pli" << std::endl;
  // H264编码参数，包括级别不对称允许、包化模式、配置文件级别ID
  sdp << "a=fmtp:121 "
         "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id="
         "64001f"
      << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:122 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:122 apt=121" << std::endl;
  // RTP映射，RED（Redundant RTP Payload），时钟频率90000Hz
  sdp << "a=rtpmap:123 red/90000" << std::endl;
  // RTP映射，RTX，时钟频率90000Hz
  sdp << "a=rtpmap:124 rtx/90000" << std::endl;
  // RTX关联的原始编码格式
  sdp << "a=fmtp:124 apt=123" << std::endl;
  // RTP映射，ULPFEC（Unicast Low-Priority Forward Error
  // Correction），时钟频率90000Hz
  sdp << "a=rtpmap:125 ulpfec/90000" << std::endl;
  // RTP映射，FlexFEC-03，时钟频率90000Hz
  sdp << "a=rtpmap:49 flexfec-03/90000" << std::endl;
  // RTCP反馈，Google REMB
  sdp << "a=rtcp-fb:49 goog-remb" << std::endl;
  // RTCP反馈，传输层拥塞控制
  sdp << "a=rtcp-fb:49 transport-cc" << std::endl;
  // FlexFEC-03编码参数，修复窗口大小
  sdp << "a=fmtp:49 repair-window=10000000" << std::endl;

  // 音频部分
  // // 定义音频媒体流信息，包括端口、传输协议、编码格式等
  // sdp << "m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126" << std::endl;
  // // 指定音频媒体流的网络地址和类型
  // sdp << "c=IN IP4 0.0.0.0" << std::endl;
  // // 指定RTCP的端口和网络地址
  // sdp << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
  // // ICE用户名片段
  // sdp << "a=ice-ufrag:R7DT" << std::endl;
  // // ICE密码
  // sdp << "a=ice-pwd:S5AnXLnQKmupL7NxNovGks84" << std::endl;
  // // ICE选项，使用trickle模式
  // sdp << "a=ice-options:trickle" << std::endl;
  // // 指纹信息，用于验证DTLS会话
  // sdp << "a=fingerprint:sha-256 "
  //        "C1:B4:31:ED:D9:81:C5:1B:B4:C4:C9:A0:FE:09:7A:19:DD:C1:54:9A:E2:94:CD:"
  //        "D3:B8:F1:11:3B:8C:6E:4C:7C"
  //     << std::endl;
  // // DTLS连接设置模式，actpass表示主动-被动模式
  // sdp << "a=setup:actpass" << std::endl;
  // // 媒体流的标识符
  // sdp << "a=mid:1" << std::endl;
  // // RTP扩展头映射，SSRC音频级别
  // sdp << "a=extmap:14 urn:ietf:params:rtp-hdrext:ssrc-audio-level" <<
  // std::endl;
  // // RTP扩展头映射，绝对发送时间
  // sdp << "a=extmap:2
  // http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
  //     << std::endl;
  // // RTP扩展头映射，传输层拥塞控制扩展
  // sdp << "a=extmap:4 "
  //        "http://www.ietf.org/id/"
  //        "draft-holmer-rmcat-transport-wide-cc-extensions-01"
  //     << std::endl;
  // // RTP扩展头映射，媒体流标识
  // sdp << "a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid" << std::endl;
  // // 媒体流接收模式，仅接收
  // sdp << "a=recvonly" << std::endl;
  // // RTCP复用
  // sdp << "a=rtcp-mux" << std::endl;
  // // RTCP报告大小
  // sdp << "a=rtcp-rsize" << std::endl;
  // // RTP映射，Opus编码，时钟频率48000Hz，声道数2
  // sdp << "a=rtpmap:111 opus/48000/2" << std::endl;
  // // RTCP反馈，传输层拥塞控制
  // sdp << "a=rtcp-fb:111 transport-cc" << std::endl;
  // // Opus编码参数，最小播放时间、使用带内FEC
  // sdp << "a=fmtp:111 minptime=10;useinbandfec=1" << std::endl;
  // // RTP映射，RED，时钟频率48000Hz，声道数2
  // sdp << "a=rtpmap:63 red/48000/2" << std::endl;
  // // RED编码参数，关联的编码格式
  // sdp << "a=fmtp:63 111/111" << std::endl;
  // // RTP映射，G722编码，时钟频率8000Hz
  // sdp << "a=rtpmap:9 G722/8000" << std::endl;
  // // RTP映射，PCMU编码，时钟频率8000Hz
  // sdp << "a=rtpmap:0 PCMU/8000" << std::endl;
  // // RTP映射，PCMA编码，时钟频率8000Hz
  // sdp << "a=rtpmap:8 PCMA/8000" << std::endl;
  // // RTP映射，CN（Comfort Noise），时钟频率8000Hz
  // sdp << "a=rtpmap:13 CN/8000" << std::endl;
  // // RTP映射，电话事件，时钟频率48000Hz
  // sdp << "a=rtpmap:110 telephone-event/48000" << std::endl;
  // // RTP映射，电话事件，时钟频率8000Hz
  // sdp << "a=rtpmap:126 telephone-event/8000" << std::endl;

  // // 应用部分
  // // 定义应用媒体流信息，包括端口、传输协议、应用类型
  // sdp << "m=application 9 UDP/DTLS/SCTP webrtc-datachannel" << std::endl;
  // // 指定应用媒体流的网络地址和类型
  // sdp << "c=IN IP4 0.0.0.0" << std::endl;
  // // ICE用户名片段
  // sdp << "a=ice-ufrag:R7DT" << std::endl;
  // // ICE密码
  // sdp << "a=ice-pwd:S5AnXLnQKmupL7NxNovGks84" << std::endl;
  // // ICE选项，使用trickle模式
  // sdp << "a=ice-options:trickle" << std::endl;
  // // 指纹信息，用于验证DTLS会话
  // sdp << "a=fingerprint:sha-256 "
  //        "C1:B4:31:ED:D9:81:C5:1B:B4:C4:C9:A0:FE:09:7A:19:DD:C1:54:9A:E2:94:CD:"
  //        "D3:B8:F1:11:3B:8C:6E:4C:7C"
  //     << std::endl;
  // // DTLS连接设置模式，actpass表示主动-被动模式
  // sdp << "a=setup:actpass" << std::endl;
  // // 媒体流的标识符
  // sdp << "a=mid:2" << std::endl;
  // // SCTP（Stream Control Transmission Protocol）端口
  // sdp << "a=sctp-port:5000" << std::endl;
  // // 最大消息大小
  // sdp << "a=max-message-size:262144" << std::endl;
  return sdp.str();
}

}
