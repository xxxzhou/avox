# WebRTC集成本地播放器解码器实践

如下是现在实现效果，windows 平台拉 WebRTC 的H264数据，调用当前播放器已封装的 dx11va 硬解并渲染出来。

![webrtc decoder](../../assets/images/media/webrtc%20decoder.png)

这是WebRTC分支M138自带H265协议支持，以及使用播放器封装H265硬解渲染。

![webrtc decoder](../../assets/images/media/webRtc_h265.png)

在[播放器 WebRTC](播放器WebRTC.md)中，把 WebRTC 的数据渲染到自身播放器各平台的原生窗口中，其中说过几个问题，M138 已经支持 H265，但是在 WebRTC 项目本身只是有限整合 ffmpeg 软解，并且会导致与本身播放器 ffmpeg 重复定义等问题，查找资料，整合硬解都是在 WebRTC 本身上，如[Android 平台 WebRTC 开启 H265 编解码](https://hanniballol.github.io/2022/09/29/Android%E5%B9%B3%E5%8F%B0WebRTC%E5%BC%80%E5%90%AFH265%E7%BC%96%E8%A7%A3%E7%A0%81/)/[h265_ios](https://github.com/shiguredo-webrtc-build/webrtc-build/blob/master/patches/h265_ios.patch)，但是这种方式有个问题，WebRTC 一直在更新，改动多后合并新版本是个非常大的工作量，这项目大，改动后编译时间长。

在前面把 windows/Android/IOS 的原生渲染 dx11/openeles/metal 的 GPU 数据直接对接到 Vulkan 渲染管线后，我对项目做了二次大的重构，一是把各平台 VideoRender 统一接口渲染 GpuFrame，主要功能只渲染 GpuFrame，把原来在 VideoRender 的 Tick 与音频同步移到 Windows/VideoTrack,由窗口 tick 拿到同步后帧队列数据，交由 VideoRender 渲染，这样 VideoRender 就脱离播放器本身，只渲染 GpuFrame 到窗口或是离线 GPU 数据上，同样 AudioRender 也做相似处理。二是重构所有解码器，我想了下，解码器丢给外部的接口，其实只需要二个，一是输入数据，二是释放，初始化解码器这个接口都不需要放出来，可以在输入线程中检测到配置帧齐后自动调用初始化解码器 API。

在上面解码与渲染的重构完成后，我重新想了下，如何实现 WebRTC 的解码，当前播放器项目，已经实现了各平台软解，硬解以及图像处理，窗口渲染，能否直接把我已经实现的解码器整合到 WebRTC 框架中，毕竟在 PeerConnectionFactory 中可以指定解码工厂，从这来看，开发人员应该是可以实现自己的解码器的，这样的话，我不需要改动 WebRTC 本身的代码，并且前面说的二边都链接 ffmpeg 的问题也没了，也直接实现 H265/H264 各平台的硬解了。

先改变 WebRTC 编译参数重新编译，主要是把 rtc_use_h264 变为 false,这样可以去掉 WebRTC 对 ffmpeg 的链接，rtc_use_h265=true 支持H265协议。还有之前说的 WebRTC 只能编译 Release,Debug 编译不过，需要找到 webrtc 源目录下的 src/third_party/protobuf/src/google/protobuf/port_def.inc 文件,把 PROTOBUF_CONSTINIT constinit 改为 PROTOBUF_CONSTINIT，Debug 可以编译通过。

```bat
:: is_clang=false use_lld=false rtc_use_h264=false去掉ffmpeg引用
cd src
gn gen --target=x64 --ide=vs2022 ../build/windows/debug --args="is_debug=true enable_iterator_debugging=true use_custom_libcxx=false use_rtti=true rtc_include_tests=false rtc_enable_protobuf=false rtc_build_tools=false rtc_build_examples=true rtc_use_h264=false rtc_use_h265=true proprietary_codecs=true "
cd ..
```

先看重构后的解码器接口。

```C++
// 不成功，没有必要继续尝试，负值
// 0 成功
// 不成功，但是能继续尝试，正值
#define AVOX_MAP_DECODE_RESULT(XX)          \
  XX(timeout, -100, "timeout")             \
  XX(noSupport, -4, "no support")          \
  XX(noFind, -3, "no find")                \
  XX(openFailed, -2, "open failed")        \
  XX(startFailed, -1, "start failed")      \
  XX(success, 0, "success")                \
  XX(noConfig, 1, "noConfig")              \
  XX(complete, 2, "complete")              \
  XX(dataNoReady, 3, "data no ready")      \
  XX(dataError, 4, "data error")

enum class DecodeResult {
#define XX(name, value, str) name = value,
  AVOX_MAP_DECODE_RESULT(XX)
#undef XX
};
class VideoDecoder : public AVDecoder, public Observer<IVideoDecoderOb> {
  // 由decode在收集到配置帧后调用，初始化过程比较复杂，所以抽离出来
  virtual DecodeResult onPreDecoder() { return DecodeResult::noConfig; }
  // 返回true表示可弹出
  virtual DecodeResult decode(const AvoxPacket &packet) {
    return DecodeResult::noConfig;
  }
  virtual void flush() {}
  virtual void onClose() {};
}
// 列出IOS硬解初始化，原播放器IOS篇里说过，因为重构过，列出主要变化
DecodeResult IOSVDecoder::decode(const AvoxPacket & packet) {
  if ((codecDesc.vcodecId == VCodecId::h264 && configPackets.size() < 2) ||
      (codecDesc.vcodecId == VCodecId::h265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  // 还没初始化，如果是配置帧，前面会VideoDecoder会先放入configPackets
  // 在这onPreDecoder查看是否能正常初始化了
  if (!decompressionSession) {
    return onPreDecoder();
  }
  if (packet.prefixSize == 0) {
    packet.prefixSize = 4;
  }
  uint8_t ualUnit = getNalUnit(codecDesc.vcodecId,packet);
  // 是否可解码数据
  bool bDecode = naluDataFrame(codecDesc.vcodecId,ualUnit);
  bool bKeyFrame = naluKeyFrame(codecDesc.vcodecId,ualUnit);
  // ios里不能解码的包不要输入,可能引起问题
  if (!bDecode) {
    return DecodeResult::dataError;
  }
}

```

需要先理清这个接口，这是当前播放器项目所有平台实现的解码器约束实现接口，只需要把这些接口用 webrtc::VideoDecoder 转发一次，原则上就能给 WebRTC 使用了。

```C++
class RtcDecoder : public webrtc::VideoDecoder, public avox::IVideoDecoderOb {
public:
  RtcDecoder();
  virtual ~RtcDecoder();

protected:
  VCodecId codecId = VCodecId::none;
  std::unique_ptr<avox::VideoDecoder> decode = nullptr;
  webrtc::DecodedImageCallback *callback = nullptr;
  // sps,vps由avox本身分析,这里主要是为了QP(视频质量与压缩率)
  std::unique_ptr<webrtc::BitstreamParser> parser = nullptr;
  std::optional<int> qp = std::nullopt;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  //
  std::deque<ImageTimeInfo> imageTimeInfos;

public:
  uint32_t getRTPTimestamp(int64_t pts) ;

// webrtc::VideoDecoder
public:
  virtual bool Configure(const Settings &settings) override;
  virtual int32_t Release() override;
  virtual int32_t Decode(const webrtc::EncodedImage &input_image,
                         bool missing_frames,
                         int64_t render_time_ms = -1) override;
  virtual int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback *callback) override;
  virtual DecoderInfo GetDecoderInfo() const override;
  virtual const char *ImplementationName() const override;

  // IVideoDecoderOb
public:
  virtual void onDecode(const YUVFrame &frame) override;
  virtual void onDecodeGpu(const GpuFrame &frame) override;
};

using namespace webrtc;

RtcDecoder::RtcDecoder() {}

RtcDecoder::~RtcDecoder() {}

bool RtcDecoder::Configure(const Settings &settings) {
  log(LogLevel::info, "RtcDecoder::Configure", settings.number_of_cores());
  if (settings.codec_type() == kVideoCodecH264) {
    codecId = VCodecId::h264;
  } else if (settings.codec_type() == kVideoCodecH265) {
    codecId = VCodecId::h265;
  } else {
    return false;
  }
  bool bHard = true;
  const char *sName = getDefaultDecoderName(codecId, bHard);
  // 查找解码器列表
  const auto &decodes = AvoxManager::Get().vDecoders.initFuncs(codecId);
  if (decodes.size() < 0) {
    return false;
  }
  size_t sIndex = 0;
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto &vDecode = decodes[sIndex];
  // 初始化解码器
  decode = std::unique_ptr<avox::VideoDecoder>(vDecode.initFunc());
  if (!decode) {
    return false;
  }
  if (codecId == VCodecId::h264) {
    parser = std::make_unique<H264BitstreamParser>();
  } else {
    parser = std::make_unique<H265BitstreamParser>();
  }
  decode->addObserver(this);
  VideoDesc videoDesc = {};
  IoPackFormat packFormat = IoPackFormat::annexb;
  return decode->setContext(vDecode.desc, videoDesc, packFormat);
}

int32_t RtcDecoder::Release() {
  if (decode) {
    decode->removeObserver(this);
    decode->close();
    decode.reset();
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RtcDecoder::Decode(const webrtc::EncodedImage &input_image,
                           bool missing_frames, int64_t render_time_ms) {
  if (!decode) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  if (parser) {
    parser->ParseBitstream(input_image);
    qp = parser->GetLastSliceQp();
  }
  // 记录rtp与render_time
  ImageTimeInfo info = {};
  info.pts = render_time_ms;
  info.rtpTimestamp = input_image.RtpTimestamp();
  // log(LogLevel::info, "record pts:", render_time_ms,
  //     " rtpTimestamp:", info.rtpTimestamp);
  imageTimeInfos.push_back(info);
  // 交由avox解码器解码
  AvoxPacket packet = {};
  packet.pts = render_time_ms;
  packet.dts = render_time_ms;
  packet.index = 0;
  packet.data.data = const_cast<uint8_t *>(input_image.data());
  packet.data.size = input_image.size();
  // 固定 0001
  packet.prefixSize = 4;
  DecodeResult result = DecodeResult::success;
  uint8_t nalu = getNalUnit(codecId, packet);
  // 配置帧
  if (naluConfigFrame(codecId, nalu)) {
    splitAnnexbNalu(packet, spiltBufs);
    // 配置帧可能合并在一起并带个I帧，试着分开
    if (spiltBufs.size() > 1) {
      for (auto &buf : spiltBufs) {
        uint8_t nal = getNalUnit(codecId, buf);
        if (naluConfigFrame(codecId, nal)) {
          buf.packtype = (int32_t)PackType::vconfig;
          decode->pushConfig(buf);
        } else {
          // 后面的I帧
          buf.packtype = (int32_t)PackType::video;
          result = decode->decode(buf);
        }
      }
    } else {
      // 配置帧分开的
      packet.packtype = (int32_t)PackType::vconfig;
      decode->pushConfig(packet);
    }
  } else {
    packet.packtype = (int32_t)PackType::video;
    result = decode->decode(packet);
  }
  switch (result) {
  case DecodeResult::success:
    // input_image的数据添加进队列
    // EncoderInfo info = {};
    return WEBRTC_VIDEO_CODEC_OK;
  case DecodeResult::noConfig:
  case DecodeResult::dataNoReady:
    return WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME;
  case DecodeResult::dataError:
    return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
  case DecodeResult::timeout:
    return WEBRTC_VIDEO_CODEC_TIMEOUT;
  case DecodeResult::noSupport:
  case DecodeResult::noFind:
  case DecodeResult::openFailed:
  case DecodeResult::startFailed:
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  default:
    break;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

const char *RtcDecoder::ImplementationName() const {
  return "avox webrtc decoder";
}

int32_t RtcDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback *callback_) {
  callback = callback_;
  return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoDecoder::DecoderInfo RtcDecoder::GetDecoderInfo() const {
  webrtc::VideoDecoder::DecoderInfo info = {};
  info.implementation_name = ImplementationName();
  if (decode) {
    VCodecTh th = decode->getCodecTh();
    info.is_hardware_accelerated = th != VCodecTh::other && th != VCodecTh::cpu;
  }
  return info;
}

uint32_t RtcDecoder::getRTPTimestamp(int64_t pts) {
  uint32_t rtpTimestamp = 0;
  while (!imageTimeInfos.empty()) {
    ImageTimeInfo info = imageTimeInfos.front();
    imageTimeInfos.pop_front();
    if (info.pts >= pts) {
      rtpTimestamp = info.rtpTimestamp;
      break;
    }
  }
  return rtpTimestamp;
}

void RtcDecoder::onDecode(const YUVFrame &frame) {
  if (!callback) {
    return;
  }
  uint32_t rtpTimestamp = getRTPTimestamp(frame.pts);
  // log(LogLevel::info, "get pts:", frame.pts, " rtpTimestamp:", rtpTimestamp);
  webrtc::scoped_refptr<RtcVideoBuffer> videoBuffer =
      webrtc::make_ref_counted<RtcVideoBuffer>();
  videoBuffer->form(frame);
  webrtc::VideoFrame decoded_frame = webrtc::VideoFrame::Builder()
                                         .set_video_frame_buffer(videoBuffer)
                                         .set_timestamp_ms(frame.pts)
                                         .set_rtp_timestamp(rtpTimestamp)
                                         .build();
  callback->Decoded(decoded_frame, std::nullopt, qp);
}

void RtcDecoder::onDecodeGpu(const GpuFrame &frame) {
  if (!callback) {
    return;
  }
  uint32_t rtpTimestamp = getRTPTimestamp(frame.pts);
  webrtc::scoped_refptr<RtcVideoBuffer> videoBuffer =
      webrtc::make_ref_counted<RtcVideoBuffer>();
  videoBuffer->form(frame);
  webrtc::VideoFrame decoded_frame = webrtc::VideoFrame::Builder()
                                         .set_video_frame_buffer(videoBuffer)
                                         .set_timestamp_ms(frame.pts)
                                         .set_rtp_timestamp(rtpTimestamp)
                                         .build();
  callback->Decoded(decoded_frame, std::nullopt, qp);
}
```

简单说明下，在 RtcDecoder::Configure 里，检查到 H264/H265，根据是否硬解，查找不同平台实现的解码器，如 windows/android/ios 硬解分别会查找到 dx11va/mediaCodec/VideoToolbox，然后在 webrtc::VideoDecoder 使用已经封装好的解码，注意 webrtc::EncodedImage 和 ffmpeg 给的包有些类似，其配置帧可能合并在一起并带个 I 帧，mediaCodec/VideoToolbox 都需要一个一个的配，所以需要把这个帧分开，在调用项目本身解码，在解码中检查到配置后满足后，开始初始后，然后就可以处理 EncodedImage 里的数据了，这个数据应该是固定的 annexb 头 4 的格式，如果是 3，可以再加个处理。

项目自身解码器的数据分别返回 YUVFrame/GpuFrame，需要把这二个数据做下转换，变成 webrtc::VideoFrame，转换给 webrtc::VideoDecoder 里回调。注意这里 pts 和 rtp 时间的设置，我这边测试，如果没正常设置，日志会报要么说解码队列数据太多，直接丢弃，要么说渲染延迟太大。有没大佬说下，解码后的帧为什么需要 RTP 时间？

```C++
class RtcVideoBuffer : public webrtc::VideoFrameBuffer {
public:
  RtcVideoBuffer();
  virtual ~RtcVideoBuffer();

protected:
  VideoBufferPtr buffer = nullptr;
  YUVFormat format = {};

public:
  void form(const YUVFrame &yuvFrame);
  void form(const GpuFrame &gpuFrame);

public:
  bool toFrame(YUVFrame &frame);
  bool toFrame(GpuFrame &frame);

public:
  void release();

public:
  virtual VideoFrameBuffer::Type type() const override;
  virtual int32_t width() const override;
  virtual int32_t height() const override;
  virtual webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
};
using namespace webrtc;

RtcVideoBuffer::RtcVideoBuffer() {}

RtcVideoBuffer::~RtcVideoBuffer() { release(); }

void RtcVideoBuffer::form(const YUVFrame &yuvFrame) {
  release();
  format = yuvFrame.format;
  auto temp = std::make_shared<SwVideoBuffer>();
  temp->form(yuvFrame);
  buffer = temp;
}

void RtcVideoBuffer::form(const GpuFrame &gpuFrame) {
  release();
  format = gpuFrame.format;
  auto temp = std::make_shared<HwVideoBuffer>();
  temp->setGPUFrame(gpuFrame);
  buffer = temp;
}

bool RtcVideoBuffer::toFrame(YUVFrame &frame) {
  if (!buffer) {
    return false;
  }
  if (buffer->getBufferType() != VBufferType::cpu) {
    return false;
  }
  SwVideoBuffer *swBuffer = (SwVideoBuffer *)buffer.get();
  swBuffer->to(frame, swBuffer->getYuvType());
  return true;
}

bool RtcVideoBuffer::toFrame(GpuFrame &frame) {
  if (!buffer) {
    return false;
  }
  if (buffer->getBufferType() == VBufferType::cpu) {
    return false;
  }
  HwVideoBuffer *hwBuffer = (HwVideoBuffer *)buffer.get();
  frame = hwBuffer->getGPUFrame();
  return true;
}

void RtcVideoBuffer::release() {
  if (buffer) {
    buffer->release();
    buffer.reset();
  }
}

VideoFrameBuffer::Type RtcVideoBuffer::type() const {
  if (!buffer) {
    return VideoFrameBuffer::Type::kNative;
  }
  if (buffer->getBufferType() != VBufferType::cpu) {
    return VideoFrameBuffer::Type::kNative;
  } else {
    switch (format.type) {
    case YuvType::yuv420P:
      return VideoFrameBuffer::Type::kI420;
    case YuvType::nv12:
      return VideoFrameBuffer::Type::kNV12;
    case YuvType::yuv422P:
      return VideoFrameBuffer::Type::kI422;
    case YuvType::yuv444P:
      return VideoFrameBuffer::Type::kI444;
    default:
      return VideoFrameBuffer::Type::kNV12;
    }
  }
}

int32_t RtcVideoBuffer::height() const { return format.height; }

int32_t RtcVideoBuffer::width() const { return format.width; }

webrtc::scoped_refptr<webrtc::I420BufferInterface> RtcVideoBuffer::ToI420() {
  return nullptr;
}
```

简单说下，就是我项目解码后数据放入 VideoFrameBuffer 接口中，因为我这边会有 Vulkan 管线直接处理 SwVideoBuffer 里所有 YUV 格式，所以这里就不实现 webrtc::ToI420 接口，毕竟渲染也是当前项目完成的。

交给播放器渲染。

```C++
void RtcParse::OnFrame(const webrtc::VideoFrame &frame) {
  int64_t ms = frame.render_time_ms();
  Timespan nowTime = {ms * 10000};
  auto frameBuffer = frame.video_frame_buffer();
  auto rtcBuffer = dynamic_cast<RtcVideoBuffer *>(frameBuffer.get());
  // 硬解
  if (frameBuffer->type() == webrtc::VideoFrameBuffer::Type::kNative) {
    GpuFrame gpuFrame = {};
    bool bGet = rtcBuffer->toFrame(gpuFrame);
    if (!bGet) {
      log(LogLevel::info, "webrtc onframe get gpu frame fail");
      return;
    }
    gpuFrame.pts = frame.render_time_ms();
    dispatch(&IOParseOb::onGpuFrame, gpuFrame);
  } else {
    YUVFrame yuvFrame = {};
    bool bGet = rtcBuffer->toFrame(yuvFrame);
    if (!bGet) {
      log(LogLevel::info, "webrtc onframe get yuv frame fail");
      return;
    }
    yuvFrame.pts = frame.render_time_ms();
    dispatch(&IOParseOb::onVideoFrame, yuvFrame);
  }
  // log(LogLevel::info, "webrtc onframe video:", nowTime);
}
```

渲染器最新接口，项目文档难维护，这几步重构后，几乎所有之前文档里的代码实现都有变化，蛋疼。

```C++
// 视频渲染主要负责平台窗口渲染
// 整合VideoGraph,自己不Tick,由窗口Tick
// 1. 各平台硬解的原生GPU数据，转为RGBA8格式的GPU数据
// 2. 根据窗口呈现，确定是渲染到窗口还是纹理上。
// 3. 包含原生DX11/OpenGL/Metal与Vulkan交互
// VideoRender由windows窗口驱动,每次Tick检查是否需要重置,脱离状态
class VideoRender : public IOptionOb {
    protected:
  // 对应各平台渲染窗口，如果为空，则可能是渲染到FBO
  AvoxSurfaceType surface = nullptr;
  // 窗口格式，窗口有大小，则输出窗口的大小
  ImageFormat windowFormat = {};
  // 窗口为空，渲染离屏的大小
  ImageFormat imageFormat = {};
  bool bResetFlag = false;
  RenderType renderType = RenderType::other;

public:
  // 改变窗口，会重置bResetFlag,这样会在运行时重置
  void setSurface(AvoxSurfaceType surface);
  // 对应dx11/opengl/metal无屏渲染需要的格式
  void setImageFormat(const ImageFormat &imageFormat);
  // 这函数在窗口线程运行，需要需要调用releaseGraph
  // 检查GPU资源是否已准备就绪，没有就调用initGraph
  void renderFrame(const avox::VideoFrame &videoBuffer);
  void renderFrame(const avox::VideoFrame &videoBuffer,
                   IRenderContext *context);

protected:
  virtual void onSetSurface() {};
  virtual bool vaildAndInitGraph(const avox::VideoFrame &frame) = 0;
  virtual void releaseGraph() {};
  virtual void renderGpuFrame(const GpuFrame& frame) = 0;
  virtual void renderCpuFrame(SwVideoBuffer *frame) {};

public:
  // DX11/OpenGL/Metal的RGBA纹理GPU数据上下文
  virtual IRenderContext *getGpuContext() = 0;
  // 暂时来看，只有VkVideoRender需要
  // 这是VideoRender的GPU数据，高效交到VkVideoRender处理
  virtual void renderGpuFrame(IRenderContext *context) {};
  //  Vk输出到窗口,Vk计算与呈现分离
  virtual void renderWindow(IWindow *window) {};
}
```

在这就可以使用当前项目解码与渲染 WebRTC 数据到各平台本地窗口了，下一步，测试H265的流。
