#include "VideoRender.hpp"

#include "../module/HighClock.hpp"
#include "ImageBuffer.hpp"

namespace avox {

void VideoRender::onOptionChange(const char* key, ArgType option) {}

VideoRender::VideoRender() {}

VideoRender::~VideoRender() {}

void VideoRender::setSurface(AvoxSurfaceType surface_) {
  if (surface == surface_) {
    return;
  }
  surface = surface_;
  onSetSurface();
  bResetFlag = true;
}

void VideoRender::enableYuvOut(YuvType yuvType) {
  if (!bOutCpuYuv || outYuvType != yuvType) {
    bOutCpuYuv = true;
    outYuvType = yuvType;
    bResetFlag = true;
    LOGFLF(LogLevel::info, "enable YUV out, type:", yuvType);
  }
}
void VideoRender::disableYuvOut() {
  if (bOutCpuYuv) {
    bOutCpuYuv = false;
    bResetFlag = true;
    LOGFLF(LogLevel::info, "disable YUV out");
  }
}
void VideoRender::enableImage(IImageBuffer* buf) {
  if (!buf) {
    disableImage();
    return;
  }
  ImageFormat fmt = buf->getImageFormat();
  // 当前仅支持 rgba8 (VkResizeLayer 主链是 rgba8, 其他格式需后续转换层)
  if (fmt.imageType != ImageType::rgba8 || !fmt.bVailid()) {
    LOGFLF(LogLevel::warn, "enableImage 仅支持 rgba8, 收到:",
           getImageTypeStr(fmt.imageType), " ", fmt.width, "x", fmt.height);
    return;
  }
  if (!bEnableImage || !(imageOutFormat == fmt) || imageOutBuffer != buf) {
    bEnableImage = true;
    imageOutFormat = fmt;
    imageOutBuffer = buf;
    bResetFlag = true;
    LOGFLF(LogLevel::info, "enable image out:", fmt.width, "x", fmt.height);
  }
}
void VideoRender::disableImage() {
  if (bEnableImage) {
    bEnableImage = false;
    imageOutBuffer = nullptr;
    bResetFlag = true;
    LOGFLF(LogLevel::info, "disable image out");
  }
}

void VideoRender::setFullScreen(bool bFull) {
  if (bFullScreen != bFull) {
    bFullScreen = bFull;
    if (bFullScreen) {
      aspect = 0.0f;
    }
    LOGFLF(LogLevel::info, "set full screen:", bFullScreen);
  }
}

void VideoRender::setAspect(float aspect_) {
  if (aspect != aspect_) {
    aspect = aspect_;
    bResetFlag = true;
    LOGFLF(LogLevel::info, aspect);
  }
}

bool VideoRender::screenShot(ImageBuffer* imageBuffer_) {
  std::unique_lock<std::mutex> lock(mtxShot);
  if (!imageBuffer_) {
    LOGFLF(LogLevel::warn, "imageBuffer is null");
    return false;
  }
  bShotComplete = std::make_shared<std::promise<bool>>();
  std::future<bool> bShotSingal = bShotComplete->get_future();
  imageBuffer = imageBuffer_;
  // 渲染线程检测到flag后，调用fetchFrame
  bShotFlag = true;
  // 等待渲染线程里调用fetchFrame完成,发送通知
  auto status = bShotSingal.wait_for(std::chrono::seconds(1));
  if (status == std::future_status::ready) {
    return bShotSingal.get();
  }
  bShotFlag = false;
  LOGFLF(LogLevel::warn, "screenShot timeout");
  return false;
}

void VideoRender::checkShot() {
  // 是否需要截图
  if (bShotFlag.exchange(false)) {
    LOGFLF(LogLevel::info, "start screen shot,ms:", timeStampMS());
    if (bShotComplete) {
      bool bRet = fetchFrame(imageBuffer);
      // 如果GPU渲染拿不到,并且是CPU输入,直接用CPU转换
      if (!bRet && cpuIn) {
        bRet = yuvframe2Rgba(yuvFrame, imageBuffer);
      }
      bShotComplete->set_value(bRet);
      LOGFLF(LogLevel::info, "screen shot:", bRet, " ms:", timeStampMS());
    }
  }
}

void VideoRender::closeResource() {
  releaseGraph();
  bResetFlag = true;
}

// 这种是插到队列的帧数据,由渲染线程调用release
void VideoRender::renderFrame(const avox::VideoFrame& frame) {
  if (!frame.buffer) {
    return;
  }
  if (frame.buffer->getBufferType() == VBufferType::cpu) {
    SwVideoBuffer* vbuffer = static_cast<SwVideoBuffer*>(frame.buffer.get());
    if (!vbuffer) {
      return;
    }
    if (!splitBuffer) {
      splitBuffer = std::make_unique<ImageBuffer>();
    }
    if (!vbuffer->to(yuvFrame, splitBuffer.get())) {
      return;
    }
    yuvFrame.pts = frame.pts;
    yuvFrame.dts = frame.dts;
    renderFrame(yuvFrame);
  }
  if ((int32_t)frame.buffer->getBufferType() > 0) {
    HwVideoBuffer* vbuffer = static_cast<HwVideoBuffer*>(frame.buffer.get());
    if (!vbuffer) {
      return;
    }
    GpuFrame& gFrame = vbuffer->getGPUFrame();
    renderFrame(gFrame);
#ifdef __ANDROID__
    // android在渲染时，已经在排队时释放了，这里设为-1避免后面再次释放
    gFrame.queueIndex = -1;
#endif
    vbuffer->release();
  }
}

void VideoRender::renderFrame(const GpuFrame& frame) {
  // 从CPU输入到GPU输入，需要重置
  if (cpuIn) {
    bResetFlag = true;
  }
  cpuIn = false;
  if (needReset(gpuFrame, frame)) {
    bResetFlag = true;
  }
  gpuFrame = frame;
  renderFrame();
}

void VideoRender::renderFrame(const YUVFrame& frame) {
  // 从非CPU输入到CPU输入，需要重置
  if (!cpuIn) {
    bResetFlag = true;
  }
  cpuIn = true;
  // YUV输入,从RGBA输入切换回来需要重置
  if (bRgbaInput) {
    bRgbaInput = false;
    bResetFlag = true;
  }
  if (needReset(yuvFrame, frame)) {
    bResetFlag = true;
  }
  yuvFrame = frame;
  renderFrame();
}

void VideoRender::renderFrame(IImageBuffer* buffer) {
  if (!buffer) {
    return;
  }
  // 从非CPU输入到CPU输入，需要重置
  if (!cpuIn) {
    bResetFlag = true;
  }
  cpuIn = true;
  // RGBA直接输入,从YUV输入切换过来需要重置
  if (!bRgbaInput) {
    bRgbaInput = true;
    bResetFlag = true;
  }
  // 检查图像格式变化
  ImageFormat fmt = buffer->getImageFormat();
  if (imageFormat.width != fmt.width || imageFormat.height != fmt.height ||
      imageFormat.imageType != fmt.imageType) {
    imageFormat = fmt;
    bResetFlag = true;
  }
  if (bFullScreen) {
    setAspect(0.0f);
  } else {
    if (fmt.width > 0 && fmt.height > 0) {
      setAspect((float)fmt.width / (float)fmt.height);
    }
  }
  // 验证是否需要重新初始化
  if (vaildAndInitGraph()) {
    bResetFlag = false;
    renderCpuFrame(buffer);
  }
  checkShot();
}

void VideoRender::renderFrame() {
  renderTick++;
  YUVFormat yuvFormat = {};
  if (cpuIn) {
    yuvFormat = yuvFrame.format;
  } else {
    yuvFormat = gpuFrame.format;
  }
  if (bFullScreen) {
    setAspect(0.0f);
  } else {
    if (yuvFormat.width > 0 && yuvFormat.height > 0) {
      setAspect((float)yuvFormat.width / (float)yuvFormat.height);
    }
  }
  // 检查imageFormat是否改变
  if (imageFormat.width != yuvFormat.width ||
      imageFormat.height != yuvFormat.height) {
    imageFormat.width = yuvFormat.width;
    imageFormat.height = yuvFormat.height;
    imageFormat.imageType = ImageType::rgba8;
    LOGFLF(LogLevel::info, "image size change,width:", imageFormat.width,
           " height:", imageFormat.height);
    bResetFlag = true;
  }
  HighClock clock = {};
  // 验证是否需要重新初始化
  if (vaildAndInitGraph()) {
    // vaildAndInitGraph通过后,bResetFlag重置为false
    bResetFlag = false;
    if (cpuIn) {
      renderCpuFrame(yuvFrame);
    } else {
      renderGpuFrame(gpuFrame);
    }
  }
  checkShot();
}

bool VideoRender::getCpuFrameBuffer(IImageBuffer** buffer, YuvType& yuvType,
                                    int64_t* pts) {
  // CPU输入(软解/透传): 布局与packed一致时零拷包装yuvFrame交付,
  // 布局不一致(带padding的420P/422P)走getCpuFrame的split路径
  if (!bOutCpuYuv || !cpuIn) {
    return false;
  }
  if (!bTightlyPacked(yuvFrame)) {
    return false;
  }
  ImageFormat fmt = {};
  yuv2ImageFormat(yuvFrame, fmt);
  cpuViewBuffer.setData(yuvFrame.data[0], fmt, false);
  *buffer = &cpuViewBuffer;
  yuvType = yuvFrame.format.type;
  if (pts) {
    *pts = yuvFrame.pts;
  }
  return true;
}

bool VideoRender::getCpuFrame(YUVFrame& frame) {
  if (cpuIn) {
    frame = yuvFrame;
    return true;
  }
  // 非CPU输入(硬解): 平台getCpuFrameBuffer回读交付packed帧,再做split重排
  IImageBuffer* buffer = nullptr;
  YuvType yuvType = YuvType::other;
  if (!getCpuFrameBuffer(&buffer, yuvType)) {
    return false;
  }
  if (!splitBuffer) {
    splitBuffer = std::make_unique<ImageBuffer>();
  }
  if (!image2SplitYUVFrame(buffer, yuvType, frame, splitBuffer.get())) {
    return false;
  }
  frame.pts = gpuFrame.pts;
  frame.dts = gpuFrame.dts;
  frame.keyFrame = gpuFrame.keyFrame;
  return true;
}

bool VideoRender::getGpuFrame(GpuFrame& frame) {
  if (!cpuIn) {
    frame = gpuFrame;
    return true;
  }
  return false;
}

void VideoRender::renderFrame(const avox::VideoFrame& videoBuffer,
                              IRenderContext* context) {
  // HighClock clock = {};
  renderFrame(videoBuffer);
  // log(LogLevel::info, "video render cost 1:", clock.recordLast());
  // 外部GPU渲染上下文
  if (context) {
    renderGpuFrame(context);
  }
  // log(LogLevel::info, "video render cost 2:", clock.recordLast());
}

}
