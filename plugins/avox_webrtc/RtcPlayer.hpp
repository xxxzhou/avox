#pragma once

#include "RtcParse.hpp"
#include "avox/audio/AudioRender.hpp"
#include "avox/player/BasePlayer.hpp"
#include "avox/video/WindowRender.hpp"

namespace avox {

// 根据
class RtcPlayer : public IRtcPlayer,
                  public BasePlayer,
                  public IRawSourceOb,
                  public RunTask {
 public:
  RtcPlayer();
  virtual ~RtcPlayer();

 private:
  // WebRTC 数据源
  std::unique_ptr<RtcParse> source;
  // 视频渲染器 (远端视频渲染)
  std::unique_ptr<WindowRender> remoteVRender;
  // 音频渲染器 (用于远端音频播放)
  std::unique_ptr<AudioRender> remoteARender;

 public:
  // IRtcPlayer 接口实现
  virtual void setRollType(RtcRollType type) override;
  virtual void addIceServer(const char* uri, const char* username,
                            const char* password) override;
  virtual void setSdpAgentOb(ISdpAgentOb* ob) override;
  virtual void setVideoSource(IVideoSource* videoSource) override;
  virtual void setAudioSource(IAudioSource* audioSource) override;
  virtual bool open() override;
  virtual ISourceInfo* getRemoteSourceInfo() override;
  virtual ISourceInfo* getLocalSourceInfo() override;
  virtual void close() override;
  virtual ISurfaceRender* getLocalSurfaceRender() override;
  virtual IAudioRender* getLocalAudioRender() override;
  virtual ISurfaceRender* getRemoteSurfaceRender() override;
  virtual IAudioRender* getRemoteAudioRender() override;
  virtual const char* getLocalSdp() override;
  virtual void setRemoteSdp(const char* sdp) override;
  virtual void addIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) override;

 protected:
  // 播放器线程
  virtual void onRunTask() override;

 private:
  void cmdOpen();
  void cmdReady();
  void cmdSetRemoteSdp(SetRemoteSdpCommandPtr cmd);
  void cmdClose();

 public:
  // RtcParse封装作为RawSource源
  // 其返回数据remoteVRender/remoteARender处理
  virtual void onReady() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId = 0) override;
  virtual void onAudioFrame(const AvoxAFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onClose() override;
};

}
