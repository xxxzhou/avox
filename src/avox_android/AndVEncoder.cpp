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

namespace {

// 键名用字面量, 不引 NDK 的 AMEDIAFORMAT_KEY_*: 那几个是 __INTRODUCED_IN(28) 的
// extern 符号, 老设备 libmediandk 里没有, 引用会在加载期找不到符号; 键名本身稳定
const char* kKeyProfile = "profile";
const char* kKeyBitrateMode = "bitrate-mode";
const char* kKeyQuality = "quality";
const char* kKeyMaxBFrames = "max-bframes";
// MediaCodecInfo.EncoderCapabilities 的码控模式
constexpr int32_t kBitrateModeCQ = 0;
constexpr int32_t kBitrateModeVBR = 1;
constexpr int32_t kBitrateModeCBR = 2;
// MediaCodecInfo.CodecProfileLevel(Java 侧常量, NDK 头里没有)。AVC 的 1 是
// Baseline, 与推流侧 WebRTC SDP 的 profile-level-id 契约一致
constexpr int32_t kAvcProfileBaseline = 1;
constexpr int32_t kAvcProfileMain = 2;
constexpr int32_t kAvcProfileHigh = 8;
constexpr int32_t kHevcProfileMain = 1;

}  // namespace

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
  // 分级下发: 设备对 profile/码控模式的支持差异极大(不支持的组合会直接 configure
  // 失败, 而不是忽略), 猜错就降级 —— 0=画质档+High, 1=码率档+Main, 2=只给基本项
  for (int32_t tier = 0; tier < 3; ++tier) {
    DecodeResult result = openCodec(mimeType, tier);
    if (result == DecodeResult::success) {
      return result;
    }
    releaseCodec();
  }
  LOGFLF(LogLevel::warn, "failed to create encoder for MIME:", mimeType);
  return DecodeResult::openFailed;
}

void AndVEncoder::applyFormatTier(int32_t tier) {
  const bool bH265 = desc.codecId == VCodecId::h265;
  // profile: 可移植三档 → MediaCodecInfo.CodecProfileLevel(HEVC 三档都落 Main)
  int32_t profile = kAvcProfileBaseline;
  if (bH265) {
    profile = kHevcProfileMain;
  } else {
    switch (getProfileLevel()) {
      case EncodeProfile::compat:
        profile = kAvcProfileBaseline;
        break;
      case EncodeProfile::main:
        profile = kAvcProfileMain;
        break;
      default:
        profile = kAvcProfileHigh;
        break;
    }
    // 降级一级时 High 退 Main(部分设备对 High 的 8x8 变换能力位直接报错)
    if (tier > 0 && profile == kAvcProfileHigh) {
      profile = kAvcProfileMain;
    }
  }
  // 码控模式: CQ(质量定档)/VBR/CBR; 降级一级时 CQ 退 VBR
  int32_t bitrateMode = kBitrateModeCBR;
  switch (getRateMode()) {
    case EncodeRateMode::quality:
      bitrateMode = kBitrateModeCQ;
      break;
    case EncodeRateMode::vbr:
      bitrateMode = kBitrateModeVBR;
      break;
    default:
      bitrateMode = kBitrateModeCBR;
      break;
  }
  if (tier > 0 && bitrateMode == kBitrateModeCQ) {
    bitrateMode = kBitrateModeVBR;
  }
  AMediaFormat_setInt32(mediaFormat, kKeyProfile, profile);
  AMediaFormat_setInt32(mediaFormat, kKeyBitrateMode, bitrateMode);
  // CQ 模式下 quality(0~100 越大越好)才是画质档, bit_rate 退成上限。
  // MediaCodec 没有预设(速度↔质量)档, 预设旋钮在这条车道不生效
  if (bitrateMode == kBitrateModeCQ) {
    AMediaFormat_setInt32(mediaFormat, kKeyQuality, getQualityLevel());
  }
}

DecodeResult AndVEncoder::openCodec(const char* mimeType, int32_t tier) {
  // 创建编码器
  mediaCodec = AMediaCodec_createEncoderByType(mimeType);
  if (!mediaCodec) {
    return DecodeResult::openFailed;
  }
  mediaFormat = AMediaFormat_new();
  if (!mediaFormat) {
    return DecodeResult::openFailed;
  }
  const int32_t bitrate = getTargetBitrate();
  AMediaFormat_setString(mediaFormat, AMEDIAFORMAT_KEY_MIME, mimeType);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_WIDTH, desc.desc.width);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_HEIGHT, desc.desc.height);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
  AMediaFormat_setFloat(mediaFormat, AMEDIAFORMAT_KEY_FRAME_RATE, desc.desc.fps);
  AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, gop);
  // B 帧恒关: MediaCodec 对 max-bframes 的支持与出帧顺序在设备间差异过大,
  // surface 输入下部分实现会乱序出帧(录制产物时间戳错乱远重于体积收益)
  AMediaFormat_setInt32(mediaFormat, kKeyMaxBFrames, 0);
  if (!bGpu) {
    // 设置颜色格式为NV12(YUV420SemiPlanar 0x15)
    AMediaFormat_setInt32(mediaFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x15);
  } else {
    // 使用 Surface 输入格式 COLOR_FormatSurface(由 createInputSurface 设定)
  }
  if (tier < 2) {
    applyFormatTier(tier);
  }
  media_status_t status =
      AMediaCodec_configure(mediaCodec, mediaFormat, nullptr, nullptr,
                            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "failed to configure encoder:", status,
           " tier:", tier);
    return DecodeResult::openFailed;
  }
  if (bGpu) {
// AMediaCodec_createInputSurface需要NDK26及以上版本支持
#if __ANDROID_API__ >= 26
    ANativeWindow* surface = nullptr;
    status = AMediaCodec_createInputSurface(mediaCodec, &surface);
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
  // 启动编码器
  status = AMediaCodec_start(mediaCodec);
  if (status != AMEDIA_OK) {
    LOGFLF(LogLevel::warn, "failed to start encoder:", status);
    return DecodeResult::startFailed;
  }
  LOGFLF(LogLevel::info, "android encoder open:", mimeType, " tier:", tier,
         " usage:", encodeParam.usage == EncodeUsage::stream ? "stream" : "record",
         " bitrate:", bitrate, " gop:", gop);
  return DecodeResult::success;
}

void AndVEncoder::flush() {
  if (!mediaCodec) {
    return;
  }
  AMediaCodec_flush(mediaCodec);
}

// 排空: 送 EOS 让编码器把尾部缓冲吐完(AMediaCodec_flush 是丢弃语义, 会吃尾帧)。
// surface 输入用 signalEndOfInputStream, 缓冲输入用一个 EOS 旗帜的空输入缓冲
void AndVEncoder::drain() {
  if (!mediaCodec) {
    return;
  }
  if (bGpu) {
    AMediaCodec_signalEndOfInputStream(mediaCodec);
  } else {
    ssize_t index = AMediaCodec_dequeueInputBuffer(mediaCodec, 5000);
    if (index >= 0) {
      AMediaCodec_queueInputBuffer(mediaCodec, index, 0, 0, 0,
                                   AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    } else {
      LOGFLF(LogLevel::warn, "drain: no input buffer for EOS:", index);
    }
  }
  // encode() 内部循环取到 EOS 旗帜才收工
  encode();
}

void AndVEncoder::releaseCodec() {
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

void AndVEncoder::onClose() { releaseCodec(); }

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
  // 编码器输入按紧凑布局(width*height*1.5)分配, 源帧行常带padding,
  // 必须按行拷; 输入yuv420P时U/V平面需交错成NV12
  size_t yCompact = (size_t)frame.format.width * frame.format.height;
  size_t uvCompact = yCompact / 2;
  // 获取输入缓冲区
  ssize_t inputBufferIndex = AMediaCodec_dequeueInputBuffer(mediaCodec, 5000);
  if (inputBufferIndex >= 0) {
    // 获取输入缓冲区指针
    size_t bufferSize = 0;
    uint8_t* inputBuffer =
        AMediaCodec_getInputBuffer(mediaCodec, inputBufferIndex, &bufferSize);
    if (inputBuffer && bufferSize >= yCompact + uvCompact) {
      int32_t w = frame.format.width;
      int32_t h = frame.format.height;
      // 复制Y平面(去padding)
      for (int32_t r = 0; r < h; ++r) {
        memcpy(inputBuffer + (size_t)r * w,
               frame.data[0] + (size_t)r * frame.stride[0], w);
      }
      uint8_t* uvDst = inputBuffer + yCompact;
      if (frame.format.type == YuvType::nv12) {
        // 半平面: UV整行拷
        for (int32_t r = 0; r < h / 2; ++r) {
          memcpy(uvDst + (size_t)r * w,
                 frame.data[1] + (size_t)r * frame.stride[1], w);
        }
      } else {
        // yuv420P平面: U/V交错成NV12
        int32_t uw = w / 2;
        for (int32_t r = 0; r < h / 2; ++r) {
          const uint8_t* uRow = frame.data[1] + (size_t)r * frame.stride[1];
          const uint8_t* vRow = frame.data[2] + (size_t)r * frame.stride[2];
          uint8_t* dRow = uvDst + (size_t)r * w;
          for (int32_t c = 0; c < uw; ++c) {
            dRow[2 * c] = uRow[c];
            dRow[2 * c + 1] = vRow[c];
          }
        }
      }
      // 将缓冲区提交给编码器
      media_status_t status = AMediaCodec_queueInputBuffer(
          mediaCodec, inputBufferIndex, 0, yCompact + uvCompact, frame.pts, 0);
      if (status != AMEDIA_OK) {
        LOGFLF(LogLevel::warn, "failed to queue input buffer:", status);
        return DecodeResult::dataError;
      }
    } else {
      LOGFLF(LogLevel::warn, "Input buffer is null or too small:", bufferSize,
             "required:", yCompact + uvCompact);
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
// LOGFLF(LogLevel::info, "cpu outputBufferIndex:", outputBufferIndex,
// " info size:", info.size, " epts:", info.presentationTimeUs);
  while (outputBufferIndex >= 0) {
    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      LOGFLF(LogLevel::info, "end of stream reached");
      // 排空路径会走到这里: 不释放会让这个 output buffer 一直挂在编解码器上
      AMediaCodec_releaseOutputBuffer(mediaCodec, outputBufferIndex, false);
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
