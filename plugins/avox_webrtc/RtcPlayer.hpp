#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>

#include "RtcParse.hpp"
#include "avox/audio/AudioRender.hpp"
#include "avox/player/BasePlayer.hpp"
#include "avox/video/WindowRender.hpp"

namespace avox {

// 推流本地源信息(getLocalSourceInfo返回)
class RtcSourceInfo : public ISourceInfo {
 public:
  std::vector<VTrackDesc> vtracks;
  std::vector<ATrackDesc> atracks;

 public:
  virtual int32_t videoSize() override { return (int32_t)vtracks.size(); }
  virtual int32_t audioSize() override { return (int32_t)atracks.size(); }
  virtual VTrackDesc getVideoDesc(int32_t index) override;
  virtual ATrackDesc getAudioDesc(int32_t index) override;
  virtual bool canSeek() override { return false; }
};

// IRtcPlayer实现: RtcParse封装作为RawSource源
// 其返回数据remoteVRender/remoteARender处理
// 本地SDP/ICE经此转发给上层ISdpAgentOb/ISignalChannel
class RtcPlayer : public IRtcPlayer,
                  public BasePlayer,
                  public IRawSourceOb,
                  public ISdpAgentOb,
                  public ISignalOb,
                  public Observer<IRtcPlayerOb>,
                  public RunTask {
 public:
  RtcPlayer();
  virtual ~RtcPlayer();

 private:
  // WebRTC 数据源
  std::unique_ptr<RtcParse> source;
  // 视频渲染器 (远端视频渲染)
  std::unique_ptr<WindowRender> remoteVRender;
  // 音频渲染器 (平台设备输出, 用于远端音频播放)
  std::unique_ptr<AudioRender> remoteARender;
  // 远端音频描述是否已设置到渲染器
  bool bAudioDescSet = false;
  // 上层SDP钩子与信令通道
  ISdpAgentOb* userSdpOb = nullptr;
  ISignalChannel* signalChannel = nullptr;
  // open命令结果(cmdOpen在播放线程回填)
  std::mutex cmdMtx;
  std::shared_ptr<std::promise<bool>> openPromise;
  // 自动重连
  bool bAutoReconnect = false;
  int32_t maxRetryCount = 0;
  int32_t retryCount = 0;
  int64_t retryAtMs = 0;
  // 用户是否请求过open(close后不再自动重连)
  bool bOpenRequested = false;
  // 上次连接状态(轮询比较用)
  std::atomic<RtcConnState> lastConnState = RtcConnState::init;
  // 首帧标记
  bool bFirstFrame = false;
  // 渲染帧率统计
  std::atomic<int32_t> fpsCount = 0;
  std::atomic<int32_t> fpsValue = 0;
  std::atomic<int64_t> fpsTime = 0;
  // 推流本地源信息
  std::unique_ptr<RtcSourceInfo> localSourceInfo;

 public:
  // IRtcPlayer 接口实现
  virtual void setRollType(RtcRollType type) override;
  virtual void addIceServer(const char* uri, const char* username,
                            const char* password) override;
  virtual void setVideoDirection(RtpDirection direction) override;
  virtual void setAudioDirection(RtpDirection direction) override;
  virtual void setSendVideoBitrate(int32_t maxKbps) override;
  virtual void setSendVideoFps(int32_t maxFps) override;
  virtual void setPreferredVideoCodec(const char* codec) override;
  virtual void setEnableDataChannel(bool bEnable) override;
  virtual void setSdpAgentOb(ISdpAgentOb* ob) override;
  virtual void setSignalChannel(ISignalChannel* channel) override;
  virtual void setVideoSource(IVideoSource* videoSource) override;
  virtual void setAudioSource(IAudioSource* audioSource) override;
  virtual void setAutoReconnect(bool bEnable, int32_t maxRetries) override;
  virtual bool open() override;
  virtual void close() override;
  virtual void reconnect() override;
  virtual const char* getLocalSdp() override;
  virtual void setRemoteSdp(const char* sdp) override;
  virtual void addIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) override;
  virtual PlayerState getState() override;
  virtual RtcConnState getConnectionState() override;
  virtual double getFps() override;
  virtual float getLossRate() override;
  virtual int32_t getRttMs() override;
  virtual ISourceInfo* getRemoteSourceInfo() override;
  virtual ISourceInfo* getLocalSourceInfo() override;
  virtual ISurfaceRender* getLocalSurfaceRender() override;
  virtual IAudioRender* getLocalAudioRender() override;
  virtual ISurfaceRender* getRemoteSurfaceRender() override;
  virtual IAudioRender* getRemoteAudioRender() override;
  virtual bool sendDataChannel(const char* data, int32_t size) override;
  virtual void addOb(IRtcPlayerOb* ob) override;
  virtual void removeOb(IRtcPlayerOb* ob) override;

 protected:
  // 播放器线程
  virtual void onRunTask() override;

 private:
  void cmdOpen();
  void cmdReady();
  void cmdSetRemoteSdp(SetRemoteSdpCommandPtr cmd);
  void cmdClose();
  void cmdReopen();
  // 轮询连接状态: 变化派发onConnectionState, failed时自动重连
  void pollConnection();
  // 远端帧到达统一处理: 帧率统计+首帧事件
  void onRemoteFrame(bool bVideo);

 public:
  // IRawSourceOb 实现 (RtcParse回调)
  virtual void onReady() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId = 0) override;
  virtual void onAudioFrame(const AvoxAFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onClose() override;

 public:
  // ISdpAgentOb 实现 (注册到RtcParse, 信令线程回调, 转发上层ob/信令通道)
  virtual void onLocalSdp(const char* localSdp) override;
  virtual void onIceCandidate(const char* candidate, const char* mid,
                              int mlineIndex) override;

 public:
  // ISignalOb 实现 (信令通道回填, 可能来自通道线程)
  virtual void onRemoteSdp(const char* sdp) override;
  virtual void onRemoteIceCandidate(const char* candidate, const char* mid,
                                    int mlineIndex) override;
};

}
