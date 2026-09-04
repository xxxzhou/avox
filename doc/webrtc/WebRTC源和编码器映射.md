# WebRTC 原生编码解码与数据源集成

## 概述

在完成播放器核心数据源框架(IVideoSource/IAudioSource)与跨平台硬件编解码器(VideoEncoder/AudioEncoder)的实现后,本项目已能够通过 zlmediakit/FFmpeg 等工具,向各平台推送 RTSP, RTMP 等通用协议流.

为了在实时音视频通信场景中进一步提升用户体验,引入 WebRTC 成熟的网络传输优势,如 JitterBuffer(抗抖动缓冲区),带宽自适应和低延迟传输机制.尽管 WebRTC 本身自带编码器和数据源,但其原生实现存在以下局限:
- 无法直接使用项目已有的,自定义的硬件编码器实现.
- 不支持项目已有的自定义数据源(如屏幕录制,虚拟摄像头)
- 缺乏项目已有的图像处理能力(如滤镜,水印等)

在不改变 WebRTC 网络传输层的前提下,整合了项目已有的音视频数据源,各平台编码器和解码器,并将这些组件映射到对应的 WebRTC 接口.这样既能充分利用 WebRTC 的协议优势,又能保持项目的完整能力体系,统一 WebRTC 及已实现功能,方便后续扩展和维护.

当前推流效果如下.

[推流效果](../../assets/video/webrtc_pull.mp4)

## 设计思路

该方案的核心思想是:

- 将现有的 IVideoSource/IAudioSource 通过适配器(RtcVideoSource/RtcAudioSource)桥接为 WebRTC 可用的 VideoTrackSource 和 AudioSourceInterface.
- 将现有的 VideoEncoder/AudioEncoder 通过适配器(RtcVideoEncoder/RtcAudioEncoder)桥接为 WebRTC 可用的 VideoEncoder 和 AudioEncoder.
- 构建统一的 RtcPlayer,利用上述适配器,让 WebRTC 内部协议栈完全基于现有数据源,编码器和解码器工作,从而实现高性能,低延迟的推拉流.

### RtcVideoSource – 视频源适配器

RtcVideoSource 继承自 WebRTC 的 VideoTrackSource,并实现了 IVideoSourceOb 接口,作为桥梁连接项目已实现的视频源与 WebRTC 的视频轨道.

该适配器的设计核心在于将项目现有的视频数据源(如摄像头采集、屏幕录制、视频文件解码等)无缝集成到 WebRTC 的 PeerConnection 框架中.通过适配器模式,RtcVideoSource 保持了原有数据源的完整功能,同时满足了 WebRTC 对视频轨道接口的要求,避免了修改原有代码或重新实现数据源逻辑.

在架构设计上,RtcVideoSource 内部维护了一个 VideoBroadcaster 实例,用于将接收到的视频帧同时分发给多个消费者,典型场景包括本地预览窗口和 WebRTC 编码器.这种广播机制确保了数据源只需要采集一次,即可同时服务于预览和网络传输,大大降低了 CPU/GPU 的资源消耗.

本地渲染管线是 RtcVideoSource 的另一个关键特性.通过集成了项目的 WindowRender 和 VideoRender 组件,该适配器能够在数据传递给 WebRTC 之前,先经过完整的图像处理流程,包括裁剪、缩放、滤镜、水印等操作.

```cpp
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
  // 对应videoSource的本地渲染器及图像处理
  std::unique_ptr<WindowRender> localVRender;
  // localVRender的图像处理器
  VideoRender* vrender = nullptr;
  SourceState sstate = SourceState::kInitializing;
  bool bHasSinks = false;

 public:
  void setSource(IVideoSource* source);
  WindowRender* getLocalSurfaceRender() { return localVRender.get(); };

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
```

- 懒加载资源管理: 仅在第一个 Sink(通常是编码器或本地预览)注册时,才打开底层 VideoSource 并启动本地渲染窗口,节省 GPU/CPU 资源.当所有 Sink 都移除后,自动关闭底层资源,实现资源的按需分配和释放.

- 输出资源经过图像处理和渲染后,根据平台及是否启用硬件编码,决定直接输出 GPU 纹理还是转换为 YUV420P/NV12 格式的 CPU 数据.对于支持 GPU 纹理输入的硬件编码器(如 iOS 的 VideoToolbox),采用零拷贝的 GPU 直通方式,避免了 GPU 到 CPU 的数据传输,大幅降低延迟和功耗.对于需要 CPU 数据的场景,通过 Vulkan 的 compute shader 将 GPU 纹理转换为编码器所需的 YUV 格式.

### RtcAudioSource – 音频源适配器

RtcAudioSource 继承自 WebRTC 的 AudioSourceInterface,同时实现了 IAudioSourceOb 接口,作为桥梁连接已实现音频源与 WebRTC 的音频轨道.

音频适配器的设计面临的主要挑战是格式转换,RtcAudioSource 通过继承 AudioReshaper 类,获得了强大的音频重采样能力,能够处理不同采样率、声道数和采样格式的音频数据,并将其转换为 WebRTC 所需的标准格式.这种设计使得项目可以支持多样化的音频输入源,包括麦克风采集、系统音频录制、音频文件解码等,而不需要为每种源单独实现 WebRTC 接口.

``` cpp
class RtcAudioSource : public webrtc::AudioSourceInterface,
                       public AudioReshaper,
                       public IAudioSourceOb {
 public:
  RtcAudioSource();
  virtual ~RtcAudioSource();

 private:
  SourceState sstate = SourceState::kInitializing;
  std::mutex sinkLock;
  std::set<webrtc::AudioTrackSinkInterface*> sinks;
  // 项目已实现的音频源
  avox::AudioSource* source = nullptr;

 public:
  void setSource(IAudioSource* source);

  // IAudioSourceOb
 public:
  virtual void onAudioDesc(const AudioDesc& desc) override;
  virtual void onAudioError(AVError error, const char* msg) override;
  virtual void onAudioFrame(const AvoxAFrame& frame) override;
  virtual void onAudioClose() override;

 protected:
  // 由AudioReshaper先重采样到webrtc格式
  // 再把音频数据切片成10ms一帧
  virtual void onProcess() override;

 public:
  virtual void AddSink(webrtc::AudioTrackSinkInterface* sink) override;
  virtual void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override;
  virtual SourceState state() const override { return sstate; };
  virtual bool remote() const override { return false; };
  void RegisterObserver(webrtc::ObserverInterface* observer) override {}
  void UnregisterObserver(webrtc::ObserverInterface* observer) override {}
};
```

- WebRTC 音频处理有特定要求(如采样率需为 8000Hz 的整数倍,格式为带符号 16 位交错格式,声道数不超过 2),RtcAudioSource 利用 AudioReshaper 的重采样功能,将音频数据转换为符合 WebRTC 要求的格式.AudioReshaper 支持高质量的采样率转换算法,能够在保证音质的前提下,平滑地处理任意采样率之间的转换,同时自动处理声道映射,支持从多声道到立体声或单声道的转换.
- 动态连接管理: 与视频源类似,音频源也在第一个 Sink 注册时打开,在最后一个 Sink 移除时关闭.这种设计确保了音频采集资源只在需要时占用,对于支持全双工通信的场景(同时进行推流和拉流),音频源可以根据当前的连接状态智能地控制采集和处理的启停,优化系统资源使用.

### RtcVideoEncoder – 视频编码器适配器

RtcVideoEncoder 实现了 WebRTC 的 VideoEncoder 接口,内部封装了已实现avox::VideoEncoder,支持各平台的 H.264/H.265 的硬编和软编.

视频编码器适配器的设计目标是让 WebRTC 的网络传输层能够使用项目已有的各种编码器实现,包括各平台的硬件编码器(如 iOS 的 VideoToolbox、Android 的 MediaCodec、Windows 的 Media Foundation 等)和软件编码器(如 FFmpeg 的 x264, x265).通过适配器模式,RtcVideoEncoder 屏蔽了底层编码器的差异,为 WebRTC 提供了统一的编码接口,同时保留了各平台编码器的性能优势.

```cpp
// 把avox实现的编码封装成webrtc的接口
// 包含windows/ios/android相应的h264/h265的硬编/软编实现
class RtcVideoEncoder : public webrtc::VideoEncoder, public avox::IEncoderOb {
 public:
  RtcVideoEncoder();
  virtual ~RtcVideoEncoder();

 protected:
  VCodecId codecId = VCodecId::none;
  std::unique_ptr<avox::VideoEncoder> encode = nullptr;
  webrtc::EncodedImageCallback* callback = nullptr;
  VCodecDesc codecDesc = {};
  webrtc::EncodedImage encodedImage;
  int64_t currentPts = 0;
  uint32_t currentRtpTimestamp = 0;
  bool bHard = true;
  YUVFormat yuvFormat = {};

 protected:
  void findEncoder(VCodecId codecId, bool bHard);

 public:
  virtual void SetFecControllerOverride(
      webrtc::FecControllerOverride* fec_controller_override) override;
  virtual webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;
  virtual int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  virtual int32_t Release() override;
  virtual int32_t InitEncode(
      const webrtc::VideoCodec* codec_settings,
      const webrtc::VideoEncoder::Settings& settings) override;
  virtual int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;
  virtual void SetRates(
      const webrtc::VideoEncoder::RateControlParameters& parameters) override;
  // IEncoderOb
 public:
  virtual void onPacket(AvoxPacket& packet) override;
};
```

- 编码器动态发现与选择: 通过 AvoxManager 查询注册的编码器,并根据配置(硬编/软编)选择最合适的实例.AvoxManager 维护了一个编码器注册表,包含了项目中所有可用的编码器实现,RtcVideoEncoder 根据当前的运行平台和用户配置,自动选择最优的编码器.例如,在 iOS 上优先选择 VideoToolbox 的 H.264 硬件编码器,如果硬件编码器不可用,则降级到 FFmpeg 的软件编码器.

- 帧类型转换: 将 WebRTC 传入的 VideoFrame(通过 RtcVideoBuffer 承载)转换为内部的 YUVFrame 或 GpuFrame,然后调用底层编码器的 encode 方法.RtcVideoBuffer 是一个轻量级的包装类,支持两种数据模式:CPU 模式和 GPU 模式.在 CPU 模式下,数据以 YUVFrame 的形式存储,包含实际的 YUV 图像数据;在 GPU 模式下,数据以 GpuFrame 的形式存储,包含 GPU 纹理的句柄和相关元数据.这种设计使得适配器能够无缝地支持 CPU 和 GPU 两种编码路径,充分发挥硬件编码器的零拷贝优势.

- 同步编码回调: 底层编码器通过 IEncoderOb::onPacket 同步返回编码后的数据包.在此回调中,适配器将 AvoxPacket 封装为 WebRTC 的 EncodedImage,并通过 EncodedImageCallback 返回给 WebRTC.封装过程包括时间戳映射、帧类型判断、编码参数设置等,确保 WebRTC 能够正确识别和处理每个编码帧.对于 H.264 和 H.265 编码,适配器还会设置相应的 CodecSpecificInfo,包括 NAL 单元类型、参考帧索引等信息,支持 WebRTC 的 RTP 打包和传输优化.

### RtcPlayer – 统一的 WebRTC 播放器

RtcPlayer 是面向用户的顶层接口 IRtcPlayer 的实现,整合了 RtcParse(负责 WebRTC 信令和数据流),本地/远端视频渲染器以及远端音频渲染器.

作为整个 WebRTC 集成方案的顶层入口,RtcPlayer 提供了简洁易用的 API,屏蔽了 WebRTC 复杂的底层细节.开发者只需要设置数据源、调用 open 方法,即可实现完整的 WebRTC 推拉流功能.RtcPlayer 内部封装了 PeerConnection 的创建和管理、SDP 的协商、ICE 候选的交换等复杂流程,通过统一的回调接口向上层通知连接状态、媒体信息等关键事件.

RtcPlayer 支持两种连接角色:Offer 方和 Answer 方.Offer 方主动发起连接,适用于没有长连接信令通道的场景,如直接通过 HTTP 接口交换 SDP 的推流场景.Answer 方被动等待连接,适用于有长连接信令通道的场景,如基于 WebSocket 的音视频通话应用.通过 setRollType 方法可以灵活选择连接角色,RtcPlayer 会根据角色自动调整 SetLocalDescription 和 SetRemoteDescription 的调用顺序,简化了 SDP 协商的逻辑.

在媒体处理方面,RtcPlayer 提供了完整的预览和渲染功能.对于推流场景,可以通过 getLocalSurfaceRender 获取本地预览窗口,实时查看编码前的画面;对于拉流场景,可以通过 getRemoteSurfaceRender 获取远端画面渲染器,通过 getAudioRender 获取音频播放器.这些渲染器复用了项目已有的渲染管线,包括 Vulkan 图像处理和硬件加速渲染,确保了与其他播放器功能的一致性和高性能.

```cpp
// 专门用于处理 webrtc 对应的推拉流
class IRtcPlayer {
 public:
  IRtcPlayer() = default;
  virtual ~IRtcPlayer() = default;

 public:
  // 有长连接信令通道 -> 客户端适合作为 Answer 方(被动等待)
  // 没有长连接信令通道 -> 客户端适合作为 Offer 方(主动请求)
  // Answer/Offer,其SetLocalDescription/SetRemoteDescription顺序不同
  virtual void setRollType(RtcRollType type) = 0;
  // 当本地SDP/ICE生成后,回调给上层处理
  virtual void setSdpAgentOb(ISdpAgentOb* ob) = 0;
  // 如果要推视频流,设置本地视频源,否则设置 nullptr
  virtual void setVideoSource(IVideoSource* videoSource) = 0;
  // 如果要推音频流,设置本地音频源,否则设置 nullptr
  virtual void setAudioSource(IAudioSource* audioSource) = 0;
  virtual bool open() = 0;
  // 拉流的源信息
  virtual ISourceInfo* getRemoteSourceInfo() = 0;
  // 推流的源信息
  virtual ISourceInfo* getLocalSourceInfo() = 0;
  virtual void close() = 0;
  // 如果设置了 videoSource,则返回本地渲染器,否则返回 nullptr
  virtual ISurfaceRender* getLocalSurfaceRender() = 0;
  // 返回拉流的远端渲染器
  virtual ISurfaceRender* getRemoteSurfaceRender() = 0;
  // 返回拉流的音频渲染
  virtual IAudioRender* getAudioRender() = 0;
  // 得到本地 SDP
  virtual const char* getLocalSdp() = 0;
  // 设置远端 SDP
  virtual void setRemoteSdp(const char* sdp) = 0;
  // 设置 ICE 候选
  virtual void addIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) = 0;
};
```

## 具体代码实现

下面将介绍各个适配器的具体实现细节.

直接使用播放器已实现的各平台硬件编解码器(如 Windows DX11/DX12, Android MediaCodec, iOS VideoToolbox),实现了全 GPU 流程:从原生窗口采集,渲染处理,编码,解码,再到最终的渲染输出.统一原播放器及 WebRTC 的各个子模块实现,使用同样的 Vulkan 图像处理管线和原生窗口渲染,提供给外部统一的图像处理接口和渲染接口,方便扩展及维护.

具体实现代码位于以下位置,有兴趣的读者可以参考:

1. 启动实例测试,当前视频里的推流.

```cpp
int main(int argc, char* argv[]) {
  IRtcPlayer* sp = createWebRtcPlayer();
  sp->setRollType(RtcRollType::offer);
  const char* url =
      "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=push";
  ISdpAgentOb* sdpOb = createZlTestSdpAgent(sp, url);
  sp->setSdpAgentOb(sdpOb);
  IAudioManager* audioMgr = getAudioManager(ADeviceSdk::wasapi);
  IVideoManager* videoMgr = getVideoManager(VDeviceSdk::win_capture);
  int32_t count = videoMgr->getDeviceCount();
  int32_t vIndex = 1;
  for (int32_t i = 0; i < count; i++) {
    std::string name = videoMgr->getDeviceName(i);
    if (name.find("RenderDoc") != std::string::npos) {
      vIndex = i;
      break;
    }
    log(LogLevel::info, "video device: ", name);
  }
  sp->setAudioSource(audioMgr->getDevice(0));
  sp->setVideoSource(videoMgr->getDevice(vIndex));
  TextRender* textRender = new TextRender(sp->getLocalSurfaceRender());
  addWindowRenderOb(sp->getLocalSurfaceRender(), textRender);
  sp->getLocalSurfaceRender()->setSurface(nullptr, true);
  sp->getRemoteSurfaceRender()->setSurface(nullptr, true);
  sp->open();
  ...
}
```

2. RtcVideoSource的具体实现

```cpp
void RtcVideoSource::AddOrUpdateSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
    const webrtc::VideoSinkWants& wants) {
  // 必须调用广播器的接口来注册 Sink
  broadcaster.AddOrUpdateSink(sink, wants);
  std::lock_guard<std::mutex> lock(sinkLock);
  // 逻辑: 当第一个 Sink 加入时,打开底层视频源
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
  // 逻辑: 当最后一个 Sink 离开时,关闭底层视频源以节省 GPU/CPU 资源
  if (bHasSinks && !broadcaster.frame_wanted() && videoSource) {
    videoSource->close();
    videoSource->removeObserver(this);
    localVRender->stop();
    sstate = SourceState::kEnded;
    bHasSinks = false;
  }
}

void RtcVideoSource::onVideoFrame(const YUVFrame& frame) {
  // 如果当前没有 Sink(例如预览被关闭),直接跳过处理
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
      // CPU 直通分发
      broadcaster.OnFrame(videoFrame);
    }
  } else {
    GpuFrame vframe = {};
    bool bGet = vrender->getGpuFrame(vframe);
    if (bGet) {
      // GPU 数据直接封装,到编码会取出其GpuFrame处理
      auto rtcBuffer = webrtc::make_ref_counted<RtcVideoBuffer>();
      rtcBuffer->form(vframe);
      webrtc::VideoFrame videoFrame =
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(rtcBuffer)
              .set_rotation(webrtc::kVideoRotation_0)
              .set_timestamp_us(vframe.pts * 1000)
              .build();
      // GPU 纹理直通分发
      broadcaster.OnFrame(videoFrame);
    }
  }
}
```

3. RtcVideoEncoder

```cpp
int32_t RtcVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  if (!encode) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  // 转换为毫秒
  currentPts = frame.timestamp_us() / 1000;
  currentRtpTimestamp = frame.rtp_timestamp();
  // 从 WebRTC VideoFrame 转换为 avox 的帧格式
  auto frameBuffer = frame.video_frame_buffer();
  auto rtcBuffer = dynamic_cast<RtcVideoBuffer*>(frameBuffer.get());
  if (!rtcBuffer) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  bool needKeyFrame =
      frame_types && !frame_types->empty() &&
      frame_types->front() == webrtc::VideoFrameType::kVideoFrameKey;
  if (rtcBuffer->type() == webrtc::VideoFrameBuffer::Type::kNative) {
    // 检查是否是 GPU 帧
    GpuFrame gpuFrame = {};
    if (rtcBuffer->toFrame(gpuFrame)) {
      gpuFrame.pts = currentPts;
      gpuFrame.dts = currentPts;
      // 检查是否需要强制关键帧
      if (needKeyFrame) {
        gpuFrame.keyFrame = 1;
      }
      yuvFormat = gpuFrame.format;
      // GPU 帧编码
      DecodeResult result = encode->encode(gpuFrame);
      if (result != DecodeResult::success &&
          result != DecodeResult::dataNoReady) {
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
    }
  } else {
    // CPU 帧编码
    YUVFrame yuvFrame = {};
    if (rtcBuffer->toFrame(yuvFrame)) {
      yuvFrame.pts = currentPts;
      yuvFrame.dts = currentPts;
      // 检查是否需要强制关键帧
      if (needKeyFrame) {
        yuvFrame.keyFrame = 1;
      }
      yuvFormat = yuvFrame.format;
      DecodeResult result = encode->encode(yuvFrame);
      if (result != DecodeResult::success &&
          result != DecodeResult::dataNoReady) {
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
    } else {
      return WEBRTC_VIDEO_CODEC_OK;
    }
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

void RtcVideoEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& parameters) {
  if (!encode) {
    return;
  }
  // parameters.bitrate.get_sum_bps() 获取 WebRTC 期望的总比特率(bps)
  uint32_t bitrate_bps = parameters.bitrate.get_sum_bps();
  if (bitrate_bps > 0) {
    // 将 bps 转换为底层编码器需要的单位(假设是 bps 或 kbps)
    // 记得在底层 encode->setBitrate 中处理这个变化
    encode->setBitrate(bitrate_bps);
    log(LogLevel::info, "WebRTC SetRates: ", bitrate_bps / 1000,
        " kbps, FPS: ", parameters.framerate_fps);
  }
}

void RtcVideoEncoder::onPacket(AvoxPacket& packet) {
  uint8_t nal = getNalUnit(codecId, packet);
  bool bKeyFrame = naluKeyFrame(codecId, nal);  
  if (callback) {
    // 创建 EncodedImage
    encodedImage.SetEncodedData(
        webrtc::EncodedImageBuffer::Create(packet.data.data, packet.data.size));
    encodedImage._encodedWidth = yuvFormat.width;
    encodedImage._encodedHeight = yuvFormat.height;
    encodedImage.SetRtpTimestamp(currentRtpTimestamp);
    encodedImage._frameType = bKeyFrame
                                  ? webrtc::VideoFrameType::kVideoFrameKey
                                  : webrtc::VideoFrameType::kVideoFrameDelta;
    encodedImage.capture_time_ms_ = packet.pts;
    encodedImage.ntp_time_ms_ = 0;
    // 回调编码完成
    webrtc::CodecSpecificInfo codecInfo;
    if (codecId == VCodecId::h264) {
      codecInfo.codecType = webrtc::kVideoCodecH264;
    } else if (codecId == VCodecId::h265) {
      codecInfo.codecType = webrtc::kVideoCodecH265;
    }
    webrtc::EncodedImageCallback::Result result =
        callback->OnEncodedImage(encodedImage, &codecInfo);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
      // return WEBRTC_VIDEO_CODEC_ERROR;
      LOGFLF(LogLevel::warn, "error frame id:", result.frame_id);
    }
  }
}
```
