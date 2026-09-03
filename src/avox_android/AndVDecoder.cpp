#include "AndVDecoder.hpp"

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include "avox_egl/EglWindow.hpp"

namespace avox {

#if __ANDROID_API__ >= 21

void regVDecoderReg() {
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
  AvoxManager::Get().initFuncs.push_back(andVDecoderReg);
}

AndVDecoder::AndVDecoder() {
  codecTH = VCodecTh::androidMC;
  bMustAnnexb = true;
}

AndVDecoder::~AndVDecoder() { onClose(); }

bool AndVDecoder::onVaild() {
  VCodecId codecId = codecDesc.vcodecId;
  const char* mime = nullptr;
  switch (codecId) {
    case VCodecId::h264:
      // AMEDIAFORMAT_KEY_THUMBNAIL_CSD_AV1C
      mime = "video/avc";  // H264 MIME类型
      break;
    case VCodecId::h265:
      mime = "video/hevc";  // H265 MIME类型
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
  const auto& packets = configPackets;
  if (codecDesc.vcodecId == VCodecId::h264) {
    if (packets.size() < 2) {
      return DecodeResult::noConfig;
    }
    bool has_sps = false, has_pps = false;
    for (auto& packet : packets) {
      // 获取NAL单元数据(跳过起始码)
      const uint8_t* nalu = packet.buff.data() + packet.prefixSize;
      uint8_t nalu_type = nalu[0] & 0x1F;  // H264类型在低5位

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
    for (auto& packet : packets) {
      // 跳过起始码前缀(假设prefixSize包含起始码长度)
      const uint8_t* nalu = packet.buff.data() + packet.prefixSize;
      int nalu_size = packet.size - packet.prefixSize;
      // H265的NAL类型在第一个字节的2-7位
      uint8_t nalu_type = (nalu[0] >> 1) & 0x3F;
      switch (nalu_type) {
        case 32:  // VPS
          AMediaFormat_setBuffer(format, "csd-0", packet.buff.data(),
                                 packet.size);
          has_vps = true;
          break;
        case 33:  // SPS
          AMediaFormat_setBuffer(format, "csd-1", packet.buff.data(),
                                 packet.size);
          has_sps = true;
          break;
        case 34:  // PPS
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
  // 输出窗口的渲染模式，检查是否支持opengl 纹理输入
  eglSize.width = params.width;
  eglSize.height = params.height;
  LOGFLF(LogLevel::info, "init egl context, width:", params.width,
         " height:", params.height);
  ANativeWindow* nativeWindow = nullptr;
  // 解码线程里的EGLContext为主，渲染的为辅，这样渲染的出了问题还能解码
  initContext(EGL_NO_CONTEXT);
  // 创建EGLSurface
  if (eglGetCurrentContext()) {
    // 渲染到OES纹理上
    surfaceTexture = std::make_unique<JniSurfaceTexture>();
    // 从GPU队列取出来的存放的OES纹理
    glGenTextures(1, &textureId);
    AVOX_GL_LOG("gen texture failed.");
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    surfaceTexture->init(textureId);
    surfaceTexture->setImageSize(params.width, params.height);
    // std::function<void()> onAvailable = [&]() { bFrameAvailable = true; };
    // surfaceTexture->setOnFrameAvailableListener(onAvailable);
    nativeWindow = surfaceTexture->getNativeWindow();
    LOGFLF(LogLevel::info, "textureId:", textureId, " width:", params.width,
           " height:", params.height);
    bOpenglRender = true;
  }
  LOGFLF(LogLevel::info, "bOpenglRender:", bOpenglRender);
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
  bOpen = true;
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

DecodeResult AndVDecoder::decode(const AvoxPacket& packet) {
  if ((codecDesc.vcodecId == VCodecId::h264 && configPackets.size() < 2) ||
      (codecDesc.vcodecId == VCodecId::h265 && configPackets.size() < 3)) {
    return DecodeResult::noConfig;
  }
  if (!bOpen) {
    return onPreDecoder();
  }
  // 1.0 取buffer，填充数据，入队
  ssize_t bufidx = AMediaCodec_dequeueInputBuffer(
      mediaCodec, AVOX_ANDROID_MEDIACODEC_TIMEOUT_US);
  if (bufidx >= 0) {
    // 当取不到空buffer的时候，有可能是解码慢跟不上输入速度，导致buffer不够用
    // 所以还需要在后面继续取解码后的数据。
    size_t bufsize = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(mediaCodec, bufidx, &bufsize);
    // 新增缓冲区大小检查
    if (packet.data.size > bufsize) {
      log(LogLevel::warn,
          "android video decoder input buffer overflow bufsize:", bufsize,
          " packetSize:", packet.data.size);
      AMediaCodec_queueInputBuffer(mediaCodec, bufidx, 0, 0, 0,
                                   AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME);
      return DecodeResult::dataNoReady;
    }
    memcpy(buf, packet.data.data, packet.data.size);
    // 入队列 给到解码器
    AMediaCodec_queueInputBuffer(mediaCodec, bufidx, 0, packet.data.size,
                                 packet.pts * 1000, 0);
  }
  // 2 取输出，拿走数据，归还buffer
  size_t bufsize = 0;
  while (mediaCodec) {
    AMediaCodecBufferInfo info = {};
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
      return DecodeResult::dataNoReady;
    }
    // 输入的buffer已解析完毕，结束当前while循环
    if (bufidx == AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      return DecodeResult::success;
    }
    // 微秒转毫秒
    int64_t rawPtsUs = info.presentationTimeUs / 1000;
    // LOGFLF(LogLevel::info, "pts:", rawPtsUs);
    if (bOpenglRender) {
      //  https://blog.csdn.net/weiwei9363/article/details/135908473
      //  如果 render 参数为true，那么解码后的数据（Buffer）会立即被送到 Surface
      //  进行渲染（播放），播放完后，这个 Buffer 就会被标记为可用，返回给
      //  MediaCodec。如果 render 参数为 false，那么这个 Buffer 不会被送到
      //  Surface，而是直接被释放，然后被标记为可用，返回给 MediaCodec。
      // 这句调用要在队列取出之后
      // AMediaCodec_releaseOutputBuffer(mediaCodec, bufidx, false);
      GpuFrame frame = {};
      frame.pts = rawPtsUs;
      frame.dts = frame.pts;
      frame.context = this;
      frame.format = yuvFormat;
      frame.queueIndex = bufidx;
      if (info.flags == 1) {
        frame.keyFrame = true;
      }
      dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
    } else {
      YUVFrame frame = {};
      frame.stride[0] = stride + info.offset;
      frame.format = yuvFormat;
      frame.pts = rawPtsUs;
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
  }
  return DecodeResult::success;
}

void AndVDecoder::flush() {
  if (!mediaCodec) {
    return;
  }
  AMediaCodec_flush(mediaCodec);
}

void AndVDecoder::onClose() {
  bOpen = false;
  if (mediaCodec) {
    // AMediaCodec_flush(mediaCodec);
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (format) {
    AMediaFormat_delete(format);
    format = nullptr;
  }
  if (surfaceTexture) {
    surfaceTexture->close();
    surfaceTexture.reset();
  }
  unInit();
}

// 硬解由对应的EGLWindows窗口Tick线程调用，其窗口相应的GLES上下文共享给MediaCodec
void AndVDecoder::onFrameRelease(bool bRender, const GpuFrame& frame) {
  if (!mediaCodec || frame.queueIndex < 0) {
    return;
  }
  // bRender为true,硬解队列数据压入OES纹理
  media_status_t status =
      AMediaCodec_releaseOutputBuffer(mediaCodec, frame.queueIndex, bRender);
  if (status != AMEDIA_OK && status != -13) {
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
  // && bFrameAvailable
  if (bRender) {
    // 这个只能在initContext自身线程上以及相应的共享context的线程上
    // surfaceTexture队列输出到纹理上
    surfaceTexture->updateTexImage();
    bFrameAvailable = false;
  }
}

#endif

}
