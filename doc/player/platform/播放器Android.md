# 播放器Android

## MediaCodec硬解

MediaCodec是android提供的硬解接口。

因实际使用中，解码与渲染是在不同线程的，其有不同的Opengl上下文,主要难点在于如何二个上下文纹理交互。

前面在播放器FFmpeg篇中说过，windows集成原生的dx11硬解比较麻烦，而android/ios提供的原生SDK硬解方案就非常简单了，比集成ffmpeg里提供的封装硬解然后使用对应GPU上下文到渲染要方便，所以android/ios我都是用原生平台提供的SDK，然后使用对应GPU上下文直接与渲染线程交互。

注意的点。

1. 这里不使用外部传入的ANativeWindow，因为这样就把硬解流程绑定到ANativeWindow，当这个窗口到后台销毁时，硬解流程就会出现问题。
2. 解码与渲染有不同的EGLContext，二边要共同操作一个纹理，需要其中一个EGLContext创建时以另一个EGLContext为共享Context,我最开始把渲染的EGLContext给到解码EGLContext，但是后面发现解码的EGLContext要更独立，其解码的EGLContext不应依赖别的EGLContext，别的EGLContext也不应该影响到解码的EGLContext，而渲染本身是在解码出来的纹理之上渲染的，所以我认为渲染的EGLContext以解码的EGLContext为共享是一个更优选择。
3. 如果你把数据map下来，就可以在AMediaCodec_getOutputBuffer后直接AMediaCodec_releaseOutputBuffer，否则你需要在渲染线程，根据音视频同步确定当前帧是否渲染，来调用AMediaCodec_releaseOutputBuffer给不同参数，确保硬解数据出队列。

流程：

1. 创建一个独立的JniSurfaceTexture，对java的SurfaceTexture的C++封装，提供给MediaCodec解码的EGLContext用于渲染表面及挂接生成的渲染纹理。
2. 然后就是AMediaCode文档里的解码流程。
3. 如果使用vulkan渲染，暂时还没找到直接把解码后的opengl纹理直接转vulkan纹理方法，因此需要map数据，然后走vulkan渲染的cpu数据提交流程，vulkan渲染会把各YUV格式转RGBA数据。[播放器FFmpeg](../播放器FFmpeg.md)这里有介绍。
4. 如果是用opengl渲染，保存相应的AMediaCodec_dequeueOutputBuffer得到的索引，然后在opengl渲染线程中使用。先看代码。

``` C++
class AndVDecoder : public VideoDecoder, public GLESContext {
 public:
  AndVDecoder();
  virtual ~AndVDecoder();

 private:
  AMediaCodec* mediaCodec = nullptr;
  AMediaFormat* format = nullptr;
  // 硬解直接渲染到窗口？音视频同步要单独再做一套？
  // 如果硬解直接渲染到窗口，需要硬解队列做同步机制
  // 或者在AMediaCodec_releaseOutputBuffer之前，取ANativeWindow对应的AHardwareBuffer数据？
  ANativeWindow* nativeWindow = nullptr;

  // 如果直接使用opengl渲染
  // 1.渲染到opengl texture
  bool bOpenglRender = false;
  std::unique_ptr<JniSurfaceTexture> surfaceTexture = nullptr;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;

 public:
  void close();
  void updateYuvFormat();

  // AVDecoder
 public:
  // 初始化
  virtual bool onInit() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual bool decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;

  // VideoDecoder
 public:
  virtual void onClose() override;
  virtual void onFrameRelease(bool bRender, const GpuFrame &frame) override;
};

RegFunc andVDecoderReg = {
    "android video decoder init", []() {
      VCodecDesc codecDesc = {};
      // 初始化 faadDesc 的相关信息，例如名称、是否支持硬件加速等 ;
      codecDesc.name = AVOX_ANDROID_H264_DECODER;
      codecDesc.bHardware = true;
      codecDesc.vcodecId = VCodecId::h264;
      AvoxManager::Get().vDecoders.regInitFunc(
          VCodecId::h264, codecDesc,
          []() -> VideoDecoder* { return new AndVDecoder(); });

      codecDesc = {};
      // 初始化 faadDesc 的相关信息，例如名称、是否支持硬件加速等 ;
      codecDesc.name = AVOX_ANDROID_H265_DECODER;
      codecDesc.bHardware = true;
      codecDesc.vcodecId = VCodecId::h265;
      AvoxManager::Get().vDecoders.regInitFunc(
          VCodecId::h265, codecDesc,
          []() -> VideoDecoder* { return new AndVDecoder(); });
    }};

AndVDecoder::AndVDecoder() { codecTH = VCodecTh::androidMC; }

AndVDecoder::~AndVDecoder() { onClose(); }

bool AndVDecoder::onInit() {
  VCodecId codecId = codecDesc.vcodecId;
  const char *mime = nullptr;
  switch (codecId) {
  case VCodecId::h264:
    // AMEDIAFORMAT_KEY_THUMBNAIL_CSD_AV1C
    mime = "video/avc"; // H264 MIME类型
    break;
  case VCodecId::h265:
    mime = "video/hevc"; // H265 MIME类型
    break;
  default:
    LOGFLF(LogLevel::warn, "unsupported codec");
    return false;
  }
  // 先关闭可能存在的mediaCodec/format
  onClose();
  mediaCodec = AMediaCodec_createDecoderByType(mime);
  format = AMediaFormat_new();
  return true;
}

DecodeResult AndVDecoder::onPreDecoder() {
  const auto &packets = trackContext->getConfigPackets();
  if (codecDesc.vcodecId == VCodecId::h264) {
    if (packets.size() < 2) {
      return DecodeResult::noConfig;
    }
    bool has_sps = false, has_pps = false;
    for (auto &packet : packets) {
      // 获取NAL单元数据(跳过起始码)
      const uint8_t *nalu = packet.buff.data() + packet.prefixSize;
      uint8_t nalu_type = nalu[0] & 0x1F; // H264类型在低5位

      if (nalu_type == (uint8_t)H264NAL::NAL_SPS) {
        AMediaFormat_setBuffer(format, "csd-0", packet.buff.data(),
                               packet.size);
        has_sps = true;
      } else if (nalu_type == (uint8_t)H264NAL::NAL_PPS) {
        AMediaFormat_setBuffer(format, "csd-1", packet.buff.data(),
                               packet.size);
        has_pps = true;
      }
    }
    if (!has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing SPS or PPS in H264 stream");
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/avc");
  } else {
    if (packets.size() < 3) {
      return DecodeResult::noConfig;
    }
    // H265需要单独解析VPS/SPS/PPS
    bool has_vps = false, has_sps = false, has_pps = false;
    for (auto &packet : packets) {
      // 跳过起始码前缀(假设prefixSize包含起始码长度)
      const uint8_t *nalu = packet.buff.data() + packet.prefixSize;
      int nalu_size = packet.size - packet.prefixSize;
      // H265的NAL类型在第一个字节的2-7位
      uint8_t nalu_type = (nalu[0] >> 1) & 0x3F;
      switch (nalu_type) {
      case 32: // VPS
        AMediaFormat_setBuffer(format, "csd-0", packet.buff.data(),
                               packet.size);
        has_vps = true;
        break;
      case 33: // SPS
        AMediaFormat_setBuffer(format, "csd-1", packet.buff.data(),
                               packet.size);
        has_sps = true;
        break;
      case 34: // PPS
        AMediaFormat_setBuffer(format, "csd-2", packet.buff.data(),
                               packet.size);
        has_pps = true;
        break;
      }
    }
    if (!has_vps || !has_sps || !has_pps) {
      LOGFLF(LogLevel::warn, "missing VPS, SPS, or PPS in H265 stream");
      return DecodeResult::noConfig;
    }
    AMediaFormat_setString(format, "mime", "video/hevc");
  }
  bool bParse = parseConfigs();
  if (!bParse) {
    LOGFLF(LogLevel::warn,
           "parse configs failed: decoder parameters may contain incorrect "
           "information");
  }
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, params.width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, params.height);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                        getYuvType(params.yuvType));
  bOpenglRender = false;
  // 如果渲染使用GLES渲染，直接渲染到纹理
  RenderType renderType = mediaPlayer->getVRenderType();
  if (renderType == RenderType::OpenGLES) {
    imageFormat.width = params.width;
    imageFormat.height = params.height;
    LOGFLF(LogLevel::info, "init egl context");
    // 解码线程里的EGLContext为主，渲染的为辅，这样渲染的出了问题还能解码
    initContext(EGL_NO_CONTEXT, true);
    if (eglGetCurrentContext()) {
      // 渲染到OES纹理上
      surfaceTexture = std::make_unique<JniSurfaceTexture>();
      // 从GPU队列取出来的存放的OES纹理
      glGenTextures(1, &textureId);
      AVOX_GL_LOG("gen texture failed.");
      glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
      glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR);
      glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
                      GL_LINEAR);
      glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                      GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                      GL_CLAMP_TO_EDGE);
      surfaceTexture->init(textureId);
      surfaceTexture->setImageSize(params.width, params.height);
      nativeWindow = surfaceTexture->getNativeWindow();
      log(LogLevel::info, "onPreDecoder: textureId:", textureId);
      bOpenglRender = true;
    }
  }
  LOGFLF(LogLevel::info, "onPreDecoder bOpenglRender:", bOpenglRender);
  // 配置解码器
  media_status_t status =
      AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "configure failed:", status);
    return DecodeResult::openFailed;
  }
  // 启动解码器
  status = AMediaCodec_start(mediaCodec);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "start failed:", status);
    return DecodeResult::startFailed;
  }
  // 重置
  yuvFormat.width = 0;
  yuvFormat.height = 0;
  yuvFormat.type = params.yuvType;
  return DecodeResult::success;
}

void AndVDecoder::updateYuvFormat() {
  if (!mediaCodec) {
    return;
  }
  auto format = AMediaCodec_getOutputFormat(mediaCodec);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &yuvFormat.width);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &yuvFormat.height);
  int32_t localColorFMT = 0;
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &localColorFMT);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_STRIDE, &stride);
  yuvFormat.type = andYuvType(localColorFMT);
}

bool AndVDecoder::decode(const AvoxPacket & packet) {
  if (!mediaCodec) {
    return false;
  }
  // 1.0 取buffer，填充数据，入队
  ssize_t bufidx = AMediaCodec_dequeueInputBuffer(
      mediaCodec, AVOX_ANDROID_MEDIACODEC_TIMEOUT_US);
  if (bufidx >= 0) {
    // 当取不到空buffer的时候，有可能是解码慢跟不上输入速度，导致buffer不够用
    // 所以还需要在后面继续取解码后的数据。
    size_t bufsize = 0;
    uint8_t *buf = AMediaCodec_getInputBuffer(mediaCodec, bufidx, &bufsize);
    // 新增缓冲区大小检查
    if (packet->size > bufsize) {
      log(LogLevel::warn,
          "android video decoder input buffer overflow bufsize:", bufsize,
          " packetSize:", packet->size);
      AMediaCodec_queueInputBuffer(mediaCodec, bufidx, 0, 0, 0,
                                   AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME);
      return false;
    }
    memcpy(buf, packet->buff.data(), packet->size);
    // 入队列 给到解码器
    AMediaCodec_queueInputBuffer(mediaCodec, bufidx, 0, packet->size,
                                 packet->pts * 1000, 0);
  }

  // 2 取输出，拿走数据，归还buffer
  size_t bufsize = 0;
  AMediaCodecBufferInfo info = {};
  do {
    bufidx = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 0);
    if (yuvFormat.width == 0 || yuvFormat.height == 0) {
      updateYuvFormat();
    }
    if (bufidx < 0) {
      // 大小变化
      if (bufidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        updateYuvFormat();
      }
      // 数据不够，等下次,重试
      if (bufidx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      }
      if (bufidx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      }
      return false;
    }
    // 结束
    if (bufidx == AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      dispatch(&IVideoDecoderOb::onComplete);
      return true;
    }
    if (bOpenglRender) {
      //  https://blog.csdn.net/weiwei9363/article/details/135908473
      //  如果 render 参数为true，那么解码后的数据（Buffer）会立即被送到 Surface
      //  进行渲染（播放），播放完后，这个 Buffer 就会被标记为可用，返回给
      //  MediaCodec。如果 render 参数为 false，那么这个 Buffer 不会被送到
      //  Surface，而是直接被释放，然后被标记为可用，返回给 MediaCodec。
      // 这句调用要在队列取出之后
      // AMediaCodec_releaseOutputBuffer(mediaCodec, bufidx, false);
      GpuFrame frame = {};
      frame.pts = info.presentationTimeUs / 1000;
      frame.dts = frame.pts;
      frame.context = this;     
      frame.queueIndex = bufidx;
      if (info.flags == 1) {
        frame.keyFrame = true;
      }
      dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
    } else {
      YUVFrame frame = {};
      frame.stride[0] = stride + info.offset;
      frame.format = yuvFormat;
      frame.pts = info.presentationTimeUs / 1000;
      frame.dts = frame.pts;
      frame.data[0] = AMediaCodec_getOutputBuffer(mediaCodec, bufidx, &bufsize);
      // https://developer.android.com/reference/android/media/MediaCodec.BufferInfo.html
      // 1 keyframe 2 config 4 end of stream
      if (info.flags == 1) {
        frame.keyFrame = true;
      }
      // 加入队列
      dispatch(&IVideoDecoderOb::onDecode, frame);
      // CPU在这直接释放mediaCodec缓冲区中相应的资源
      AMediaCodec_releaseOutputBuffer(mediaCodec, bufidx, false);
    }
  } while (true);
  return true;
}

void AndVDecoder::flush() {
  if (!mediaCodec) {
    return;
  }
  AMediaCodec_flush(mediaCodec);
}

void AndVDecoder::onClose() {
  unInit();
  if (mediaCodec) {
    AMediaCodec_flush(mediaCodec);
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (format) {
    AMediaFormat_delete(format);
    format = nullptr;
  }
}

// 硬解由对应的EGLWindows窗口Tick线程调用，其窗口相应的GLES上下文共享给MediaCodec
void AndVDecoder::onFrameRelease(bool bRender, const GpuFrame &frame) {
  if (!mediaCodec) {
    return;
  }
  // 确保解码还在运行
  bRender = bRender && running();
  // bRender为true,硬解队列数据压入OES纹理
  media_status_t status =
      AMediaCodec_releaseOutputBuffer(mediaCodec, frame.queueIndex, bRender);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "failed:", status);
    return;
  }
  if (!bOpenglRender) {
    return;
  }
  if (!surfaceTexture) {
    LOGFLF(LogLevel::warn, "surface texture is null");
    return;
  }
  if (bRender) {
    // 这个只能在initContext自身线程上以及相应的共享context的线程上
    // surfaceTexture队列输出到纹理上
    surfaceTexture->updateTexImage();
  }
}
```

如下是使用opengl渲染NV12的代码。当渲染线程同步音视频时，当时队列数据可用，则先调用frame->release(bValid);这个会引起AndVDecoder::onFrameRelease，这里会调用AMediaCodec_releaseOutputBuffer(mediaCodec, frame.queueIndex, bRender);bValid为true时，bRender为true,把硬解的数据压入OES纹理，再调用GLPipeGraph::useProgram(textureId),把OES纹理转化成RGBA并渲染到渲染线程EglContext绑定的表面上，整体流利就完成了。

``` C++
class GLPipeGraph : public GLESContext {
 public:
  GLPipeGraph();
  virtual ~GLPipeGraph();

 protected:
  uint32_t glProgram = 0;
  uint32_t textureId = 0;
  uint32_t fboId = 0;
  int32_t posAttr = 0;
  int32_t uvAttr = 0;
  int32_t extAttr = 0;
  //
  EGLContext decodeCtx = EGL_NO_CONTEXT;
  EGLNativeWindowType window = nullptr;

 public:
  // initSurface需要与useProgram在同一线程使用
  void setSurface(EGLNativeWindowType window);
  // 显示解码后图像，这里设置的是解码器的EGLContext
  void setDecodeContext(EGLContext ctx);
  void initGraph();

 protected:
  virtual void onImageFormatChange() override;

 private:
  void createProgram();
  void closeProgram();

 public:
  void useProgram(uint32_t oesId);
};

static const char *VertexShaderString = R"(
precision mediump float;
attribute vec4 position;
attribute vec2 uv;
varying mediump vec2 textureCoordinate;
void main()
{
    gl_Position = position;
    // android里y倒置
    textureCoordinate = vec2(uv.x, 1.0 - uv.y);   
}
)";

static const char *FragmentShaderString = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying mediump vec2 textureCoordinate;
uniform samplerExternalOES oes_texture;
void main() {
    gl_FragColor = texture2D(oes_texture, textureCoordinate);
}
)";

static const GLfloat verts[] = {
    -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
};
static const GLfloat uvs[] = {
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

GLPipeGraph::GLPipeGraph() {}

GLPipeGraph::~GLPipeGraph() { closeProgram(); }

void GLPipeGraph::setSurface(EGLNativeWindowType window_) { window = window_; }
void GLPipeGraph::setDecodeContext(EGLContext ctx) { decodeCtx = ctx; }
void GLPipeGraph::initGraph() {
  if (!window || !decodeCtx) {
    log(LogLevel::warn, "initGraph failed, window or decodeCtx is null");
    return;
  }
  closeProgram();
  // 渲染解码后图像，
  // 之前解码用渲染的context做共享
  // 现改为渲染线程拿到解码的context做共享
  // 避免渲染窗口关闭后，导致解码线程出问题
  // 如何拿到解码器的context了？
  initContext(decodeCtx, true, window);
  createProgram();
}

void GLPipeGraph::onImageFormatChange() {
#ifndef WIN32
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageFormat.width, imageFormat.height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void GLPipeGraph::createProgram() {
#ifndef WIN32
  if (glProgram == 0) {
    glProgram = createGLProgram(VertexShaderString, FragmentShaderString);
  }
  if (glProgram == 0) {
    AVOX_GL_LOG("create gl program failed");
    return;
  }
  // 创建RGBA纹理
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  // 生成FBO
  glGenFramebuffers(1, &fboId);
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textureId, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  posAttr = glGetAttribLocation(glProgram, "position");
  uvAttr = glGetAttribLocation(glProgram, "uv");
  extAttr = glGetUniformLocation(glProgram, "oes_texture");
#endif
}

void GLPipeGraph::closeProgram() {
#ifndef WIN32
  if (glProgram) {
    glDeleteProgram(glProgram);
    glProgram = 0;
  }
  if (textureId) {
    glDeleteTextures(1, &textureId);
    textureId = 0;
  }
  if (fboId) {
    glDeleteFramebuffers(1, &fboId);
    fboId = 0;
  }
#endif
}

void GLPipeGraph::useProgram(uint32_t extId) {
  if (!surface) {
    return;
  }
#if __ANDROID__
  // 检查Surface是否有效,如果相关的EGLSurface无效，则不渲染
  EGLint width = 0;
  if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) || width <= 0) {
    return;
  }
#endif
#ifndef WIN32
  // glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  glViewport(0, 0, imageFormat.width, imageFormat.height);
  // glClearColor(1, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // 激活纹理
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, extId);
  glUniform1i(extAttr, 0);
  // 设置可编程管线参数
  glUseProgram(glProgram);
  glEnableVertexAttribArray(posAttr);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void *)(verts));
  glEnableVertexAttribArray(uvAttr);
  glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void *)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  //
  glDisableVertexAttribArray(posAttr);
  glDisableVertexAttribArray(uvAttr);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  // glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glFinish();
#endif

#ifdef __ANDROID__
  eglSwapBuffers(display, surface);
#endif
#ifdef __IPHONE__
  //[selfCtx presentRenderbuffer: GL_RENDERBUFFER];
#endif
}

void EglVideoRender::readFrame() {
  EglWindow *eglWindow = dynamic_cast<EglWindow *>(getWindow());
  GLPipeGraph *graph = eglWindow->getGraph();
#if __ANDROID_API__ >= 21
  // 拿到TrackContext的AndVDecoder,得到其glesContext
  AndVDecoder *andDecoder =
      dynamic_cast<AndVDecoder *>(trackContext->getVideoDecoder());
  if (andDecoder && andDecoder->getContext()) {
    graph->setDecodeContext(andDecoder->getContext());
  }
#endif
  auto frameAction = [&](const VideoFramePtr &frame) {
    // log(LogLevel::info, "onFrame pts:", frame->pts);
    if (frame->buffer->getCodecTh() == VCodecTh::androidMC) {
      // 引起AndVDecoder::onRender调用
      // bValid表明是否把MediaCodec解码队列的数据压入到OES纹理中，否则直接释放
      bool bValid = graph->getContext() && bWindowValid();      
      frame->release(bValid);
      if (bValid) {
        // 解码后的OES纹理复制到Surface上的RGBA纹理上
        HwVideoBuffer *hwBuffer =
            dynamic_cast<HwVideoBuffer *>(frame->buffer.get());
        if (hwBuffer && hwBuffer->getRenderContext()) {
          GLESContext *glesContext =
              dynamic_cast<GLESContext *>(hwBuffer->getRenderContext());
          if (eglWindow && glesContext->getImage() > 0) {
            graph->useProgram(glesContext->getImage());
          }
        }
      }
    }
  };
  // 确定是否需要刷新画画
  if (trackContext->syncVideo()) {
    //  FrameQueue里的线程锁下执行
    bool bGet = trackContext->getFrameQueue().dequeueAction(frameAction);
    trackContext->onFrameResult(bGet);
  }
}
```