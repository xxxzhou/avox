#include "AndVEncoder.hpp"

#include "avox/AvoxCodec.h"
#include "avox/AvoxLog.h"
#include "avox/module/AvoxManager.hpp"

namespace avox {

void regVEncoderReg() {
  RegFunc andVEncoderReg = {
      "android video encoder init", []() {
        // H264
        VCodecDesc codecDesc = {};
        codecDesc.name = AVOX_ANDROID_H264_ENCODER;
        codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::h264;
        AvoxManager::Get().vEncoders.regInitFunc(
            VCodecId::h264, codecDesc,
            []() -> VideoEncoder* { return new AndVEncoder(); });
        // H265
        codecDesc = {};
        codecDesc.name = AVOX_ANDROID_H265_ENCODER;
        codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
        codecDesc.bHardware = true;
        codecDesc.vcodecId = VCodecId::h265;
        AvoxManager::Get().vEncoders.regInitFunc(
            VCodecId::h265, codecDesc,
            []() -> VideoEncoder* { return new AndVEncoder(); });
      }};
  AvoxManager::Get().initFuncs.push_back(andVEncoderReg);
}

AndVEncoder::AndVEncoder() {}

AndVEncoder::~AndVEncoder() { onClose(); }

DecodeResult AndVEncoder::onPreEncoder() {
  const char* mimeType = nullptr;
  // 根据codecId选择MIME类型
  switch (desc.codecId) {
    case VCodecId::h264:
      mimeType = "video/avc";
      break;
    case VCodecId::h265:
      mimeType = "video/hevc";
      break;
    default:
      LOGFLF(LogLevel::warn, "unsupported codec:", getVCodecName(desc.codecId));
      return DecodeResult::noSupport;
  }
  // 创建编码器
  mediaCodec = AMediaCodec_createEncoderByType(mimeType);
  if (!mediaCodec) {
    LOGFLF(LogLevel::warn, "failed to create encoder for MIME:", mimeType);
    return DecodeResult::openFailed;
  }
  // 创建媒体格式
  mediaFormat = AMediaFormat_new();
  if (!mediaFormat) {
    LOGFLF(LogLevel::warn, "failed to create media format");
    return DecodeResult::openFailed;
  }
  // 使用基类的desc成员变量配置编码参数
  AMediaFormat_setString(
      mediaFormat, AMEDIAFORMAT_KEY_MIME,
      desc.codecId == VCodecId::h264 ? "video/avc" : "video/hevc");
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_WIDTH, desc.desc.width);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_HEIGHT, desc.desc.height);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
  AMediaFormat_setFloat(mediaFormat, AMEDIAFORMAT_KEY_FRAME_RATE,
                        desc.desc.fps);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, gop);
  if (desc.codecId == VCodecId::h264) {
    // AVOXProfileBaseline
    AMediaFormat_setInt32(mediaFormat, "profile", 1);
  } else if (desc.codecId == VCodecId::h265) {
    // HEVCProfileMain
    AMediaFormat_setInt32(mediaFormat, "profile", 1);
  }
  // 禁用B帧
  AMediaFormat_setInt32(mediaFormat, "max-bframes", 0);
  LOGFLF(LogLevel::info, "bitrate:", bitrate, " gop:", gop);
  if (!bGpu) {
    // 设置颜色格式为NV12(YUV420SemiPlanar 0x15)
    AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x15);
  } else {
    // 使用 Surface 输入格式 COLOR_FormatSurface
    // AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT,
    //                       0x7F000789);
  }
  media_status_t status =
      AMediaCodec_configure(mediaCodec, mediaFormat, nullptr, nullptr,
                            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  if (bGpu) {
// AMediaCodec_createInputSurface需要NDK26及以上版本支持
#if __ANDROID_API__ >= 26
    ANativeWindow* surface = nullptr;
    media_status_t status =
        AMediaCodec_createInputSurface(mediaCodec, &surface);
    if (status == AMEDIA_OK) {
      // eglVideoYuv用来把原始GPU数据渲染到surface上
      eglVideoYuv->setSurface(surface);
    } else {
      LOGFLF(LogLevel::warn, "failed to create input surface:", status);
      return DecodeResult::openFailed;
    }
#else
    LOGFLF(LogLevel::warn, "AMediaCodec_createInputSurface not supported");
    return DecodeResult::openFailed;  
#endif
  }
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "failed to configure encoder:", status);
    return DecodeResult::openFailed;
  }
  // 启动编码器
  status = AMediaCodec_start(mediaCodec);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "failed to start encoder:", status);
    return DecodeResult::startFailed;
  }
  return DecodeResult::success;
}

void AndVEncoder::flush() {
  if (!mediaCodec) {
    return;
  }
  AMediaCodec_flush(mediaCodec);
}

void AndVEncoder::onClose() {
  if (mediaCodec) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (mediaFormat) {
    AMediaFormat_delete(mediaFormat);
    mediaFormat = nullptr;
  }
}

DecodeResult AndVEncoder::encode(const GpuFrame& frame) {
  bool reset = needReset(frame,tempFrame);
  tempFrame = frame;
  if (!mediaCodec || reset) {
    // 告诉编码器以surface方式
    bGpu = true;
    eglVideoYuv = std::make_unique<EglVideoYuv>();
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 原始GPU数据渲染到surface上
  eglVideoYuv->renderFrame(frame);
  // 使用surface作为输入时，不能使用queueInputBuffer
  return encode();
}

DecodeResult AndVEncoder::encode(const YUVFrame& frame) {
  if (!mediaCodec) {
    bGpu = false;
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 计算NV12数据大小
  // NV12格式：Y平面 + UV平面（U和V交错）
  size_t ySize = frame.stride[0] * frame.format.height;
  size_t uvSize = ySize / 2;
  size_t totalSize = ySize + uvSize;
  // 获取输入缓冲区
  ssize_t inputBufferIndex = AMediaCodec_dequeueInputBuffer(mediaCodec, 5000);
  if (inputBufferIndex >= 0) {
    // 获取输入缓冲区指针
    size_t bufferSize = 0;
    uint8_t* inputBuffer =
        AMediaCodec_getInputBuffer(mediaCodec, inputBufferIndex, &bufferSize);
    if (inputBuffer && bufferSize >= totalSize) {
      // 复制Y平面数据
      memcpy(inputBuffer, frame.data[0], ySize);
      // 复制UV平面数据
      if (frame.data[1]) {
        memcpy(inputBuffer + ySize, frame.data[1], uvSize);
      } else {
        LOGFLF(LogLevel::warn, "UV data is null");
        // 填充UV平面为零
        memset(inputBuffer + ySize, 0x80, uvSize);  // 0x80是中性值
      }
      // 将缓冲区提交给编码器
      media_status_t status = AMediaCodec_queueInputBuffer(
          mediaCodec, inputBufferIndex, 0, totalSize, frame.pts, 0);
      if (status != AMEDIA_OK) {
        LOGFLF(LogLevel::warn, "failed to queue input buffer:", status);
        return DecodeResult::dataError;
      }
    } else {
      LOGFLF(LogLevel::warn, "Input buffer is null or too small:", bufferSize,
             "required:", totalSize);
      // 即使缓冲区无效，也要释放它
      AMediaCodec_queueInputBuffer(mediaCodec, inputBufferIndex, 0, 0, 0, 0);
    }
  }
  return encode();
}

DecodeResult AndVEncoder::encode() {
  // 获取输出缓冲区
  AMediaCodecBufferInfo info = {};
  ssize_t outputBufferIndex =
      AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 5000);
  //  LOGFLF(LogLevel::info, "cpu outputBufferIndex:", outputBufferIndex,
  //         " info size:", info.size, " epts:", info.presentationTimeUs);
  // 处理输出数据
  while (outputBufferIndex >= 0) {
    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      LOGFLF(LogLevel::info, "end of stream reached");
      break;
    }
    if (info.size > 0) {
      // 获取编码数据
      size_t outputBufferSize = 0;
      uint8_t* outputBuffer = AMediaCodec_getOutputBuffer(
          mediaCodec, outputBufferIndex, &outputBufferSize);
      if (outputBuffer && outputBufferSize > 0) {
        // 创建AvoxPacket
        AvoxPacket packet = {};
        packet.packtype = (int32_t)PackType::video;
        packet.index = 0;
        packet.data.data = outputBuffer;
        packet.data.size = info.size;
        packet.data.bRef = true;
        packet.prefixSize = 4;
        packet.pts = info.presentationTimeUs;
        packet.dts = info.presentationTimeUs;
        // 判断帧类型
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
          packet.packtype = (int32_t)PackType::vconfig;
        } else if (info.flags & AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME) {
          packet.frameType = 1;
        } else {
          packet.frameType = 0;
        }
        dispatch(&IEncoderOb::onPacket, packet);
      }
    }
    // 释放缓冲区
    AMediaCodec_releaseOutputBuffer(mediaCodec, outputBufferIndex, false);
    // 获取下一个缓冲区
    outputBufferIndex = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 0);
  }
  // 处理编码器状态
  if (outputBufferIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
    // 正常情况，没有可用数据
  } else if (outputBufferIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
    AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mediaCodec);
    if (newFormat) {
      LOGFLF(LogLevel::info, "output format changed");
      AMediaFormat_delete(newFormat);
    }
  } else if (outputBufferIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
    LOGFLF(LogLevel::info, "output buffers changed");
  }
  return DecodeResult::success;
}

}