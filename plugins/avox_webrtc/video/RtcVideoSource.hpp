#pragma once

#include "../RtcHelper.hpp"
#include "RtcVideoBuffer.hpp"
#include "api/notifier.h"
#include "api/video/video_frame.h"
#include "api/video/video_source_interface.h"
#include "avox/source/VideoSource.hpp"
#include "avox/video/WindowRender.hpp"
#include "media/base/adapted_video_track_source.h"
#include "media/base/video_broadcaster.h"
#include "pc/video_track_source.h"

namespace avox {

class RtcVideoSource : public webrtc::VideoTrackSource, public IVideoSourceOb {
 public:
  RtcVideoSource();
  virtual ~RtcVideoSource();

 private:
  // 广播视频帧到多个 Sink
  webrtc::VideoBroadcaster broadcaster;
  std::mutex sinkLock;
  // 项目当前视频源
  avox::VideoSource* videoSource = nullptr;
  // 本地源描述
  VideoDesc vdesc = {};
  // 对应videoSource的本地渲染器及图像处理
  std::unique_ptr<WindowRender> localVRender;
  // localVRender的图像处理器
  VideoRender* vrender = nullptr;
  SourceState sstate = SourceState::kInitializing;
  bool bHasSinks = false;

 public:
  void setSource(IVideoSource* source);
  void close();
  // 是否设置了推流源(决定open时是否AddTrack)
  bool hasSource();
  WindowRender* getSurfaceRender() { return localVRender.get(); };
  // 本地源描述(onVideoDesc后有效)
  VideoDesc getVideoDesc();

 public:
  void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                       const webrtc::VideoSinkWants& wants) override;
  void RemoveSink(
      webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

 public:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override;
  // 状态查询
  SourceState state() const override { return sstate; }
  bool remote() const override { return false; }

  // IVideoSourceOb
 public:
  void onVideoDesc(const VideoDesc& desc) override;
  void onVideoFrame(const YUVFrame& frame) override;
  void onGpuFrame(const GpuFrame& frame) override;
  void onVideoError(AVError error, const char* msg) override;
  void onVideoClose() override;

 private:
  void pushFrame();
};

}