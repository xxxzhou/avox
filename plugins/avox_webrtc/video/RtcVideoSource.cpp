#include "RtcVideoSource.hpp"

#include "../RtcHelper.hpp"
#include "avox/module/LogHelper.hpp"

#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#endif

namespace avox {

RtcVideoSource::RtcVideoSource() : webrtc::VideoTrackSource(false) {
  localVRender = std::make_unique<WindowRender>();
  vrender = localVRender->getVkVideoRender();
}

RtcVideoSource::~RtcVideoSource() {
  if (videoSource) {
    videoSource->removeObserver(this);
  }
}

void RtcVideoSource::setSource(IVideoSource* source) {
  std::lock_guard<std::mutex> lock(sinkLock);
  avox::VideoSource* xsource = dynamic_cast<avox::VideoSource*>(source);
  if (xsource == videoSource) {
    return;
  }
  if (videoSource) {
    videoSource->close();
    videoSource->removeObserver(this);
  }
  videoSource = xsource;
  if (videoSource) {
    videoSource->setObserver(this);
    if (bHasSinks) {
      videoSource->open();
    }
  }
}

void RtcVideoSource::close() {
  std::lock_guard<std::mutex> lock(sinkLock);
  // 1. 停止并移除上游视频源监听
  if (videoSource) { 
    videoSource->close();  
    videoSource->removeObserver(this);
  }
  // 2. 销毁本地渲染资源（防止显存/纹理泄漏）
  if (localVRender) {
    localVRender->stop();
  }
  // 3. 通知 WebRTC 广播器丢弃所有待处理帧并断开下游
  // 虽然 Broadcaster 没有 explicit close，但我们要确保状态同步
  bHasSinks = false;
  // 4. 更新状态并通知 WebRTC 框架
  sstate = SourceState::kEnded;
  // 通知
  //  FireOnChanged();
  LOGFLF(LogLevel::info, "rtc video source closed");
}

webrtc::VideoSourceInterface<webrtc::VideoFrame>* RtcVideoSource::source() {
  return &broadcaster;
}

void RtcVideoSource::AddOrUpdateSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
    const webrtc::VideoSinkWants& wants) {
  // 必须调用广播器的接口来注册 Sink
  broadcaster.AddOrUpdateSink(sink, wants);
  std::lock_guard<std::mutex> lock(sinkLock);
  // 逻辑：当第一个 Sink 加入时，打开底层视频源
  if (!bHasSinks && broadcaster.frame_wanted() && videoSource) {
    // 窗口渲染线程启动
    localVRender->start();
    // 如果得到RtcVideoEncoder是否用的硬编了?
    bool bHardEncoder = true;
    if (bHardEncoder) {
      // windows平台,硬编码器暂时还没找到直接输入DX11数据的方法
      // 因此需要输出到CPU的NV12数据,根据GPU尝试用QSV/AMF/CUDA硬编
#ifdef _WIN32
      vrender->enableYuvOut(YuvType::nv12);
#endif
    } else {  // 如果选择软解,ffmpeg需要yuv420p数据
      vrender->enableYuvOut(YuvType::yuv420P);
    }
    videoSource->setObserver(this);
    videoSource->open();
    sstate = SourceState::kLive;
    bHasSinks = true;
  }
}

void RtcVideoSource::RemoveSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
  // 从广播器移除 Sink
  broadcaster.RemoveSink(sink);
  std::lock_guard<std::mutex> lock(sinkLock);
  // 逻辑：当最后一个 Sink 离开时，关闭底层视频源以节省 GPU/CPU 资源
  if (bHasSinks && !broadcaster.frame_wanted() && videoSource) {
    videoSource->close();
    videoSource->removeObserver(this);
    localVRender->stop();
    sstate = SourceState::kEnded;
    bHasSinks = false;
  }
}

void RtcVideoSource::onVideoFrame(const YUVFrame& frame) {
  // 如果当前没有 Sink（例如预览被关闭），直接跳过处理
  if (!broadcaster.frame_wanted()) {
    return;
  }
  // 窗口渲染
  localVRender->render(frame);
  pushFrame();
}

void RtcVideoSource::onGpuFrame(const GpuFrame& frame) {
  if (!broadcaster.frame_wanted()) {
    return;
  }
  // 窗口渲染
  localVRender->render(frame);
  pushFrame();
}

void RtcVideoSource::pushFrame() {
  VideoRender* vrender = localVRender->getVkVideoRender();
  if (vrender->bCpuOut()) {
    YUVFrame yframe = {};
    bool bGet = vrender->getCpuFrame(yframe);
    if (bGet) {
      // CPU 数据直接封装,到编码会取出其GpuFrame处理
      auto rtcBuffer = webrtc::make_ref_counted<RtcVideoBuffer>();
      rtcBuffer->form(yframe);
      webrtc::VideoFrame videoFrame =
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(rtcBuffer)
              .set_rotation(webrtc::kVideoRotation_0)
              .set_timestamp_us(yframe.pts * 1000)
              .build();
      // log(LogLevel::info, "rtc camera video cpu pts:", yframe.pts);
      // CPU 直通分发
      broadcaster.OnFrame(videoFrame);
    }
  } else {
    GpuFrame vframe = {};
    bool bGet = vrender->getGpuFrame(vframe);
    if (bGet) {
#ifdef __APPLE__
      // 增加引用,因为相机的GPU资源出了回调就会自动释放
      CFRetain(vframe.buffer);
#endif
      // GPU 数据直接封装,到编码会取出其GpuFrame处理
      auto rtcBuffer = webrtc::make_ref_counted<RtcVideoBuffer>();
      rtcBuffer->form(vframe);
      webrtc::VideoFrame videoFrame =
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(rtcBuffer)
              .set_rotation(webrtc::kVideoRotation_0)
              .set_timestamp_us(vframe.pts * 1000)
              .build();
      // log(LogLevel::info, "rtc camera video gpu pts:", vframe.pts);
      // GPU 纹理直通分发
      broadcaster.OnFrame(videoFrame);
    }
  }
}

void RtcVideoSource::onVideoDesc(const VideoDesc& desc) {
  // 更新描述信息，sstate 在第一个 Sink 进来时已经设为 kLive
}

void RtcVideoSource::onVideoError(AVError error, const char* msg) {
  std::lock_guard<std::mutex> lock(sinkLock);
  sstate = SourceState::kEnded;
}

void RtcVideoSource::onVideoClose() {
  std::lock_guard<std::mutex> lock(sinkLock);
  sstate = SourceState::kEnded;
}

}
