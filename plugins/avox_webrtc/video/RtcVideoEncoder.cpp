#include "RtcVideoEncoder.hpp"

#include "RtcVideoBuffer.hpp"
#include "api/video_codecs/video_encoder.h"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/muxer/Muxer.hpp"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace avox {

using namespace webrtc;

RtcVideoEncoder::RtcVideoEncoder() { bHard = true; }

RtcVideoEncoder::~RtcVideoEncoder() { Release(); }

void RtcVideoEncoder::SetFecControllerOverride(
    webrtc::FecControllerOverride* fec_controller_override) {}

webrtc::VideoEncoder::EncoderInfo RtcVideoEncoder::GetEncoderInfo() const {
  webrtc::VideoEncoder::EncoderInfo info;
  info.implementation_name = "avox webrtc encoder";
  if (encode) {
    info.implementation_name = "webrtc " + codecDesc.name;
    std::string name = codecDesc.name;
    info.is_hardware_accelerated =
        (name.find("hardware") != std::string::npos) ||
        (name.find("MediaCodec") != std::string::npos) ||
        (name.find("VideoToolbox") != std::string::npos) ||
        (name.find("dx11") != std::string::npos);
  }
  info.supports_native_handle = true;
  return info;
}

int32_t RtcVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback_) {
  callback = callback_;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RtcVideoEncoder::Release() {
  if (encode) {
    encode->removeObserver(this);
    encode->onClose();
    encode.reset();
  }
  callback = nullptr;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RtcVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  if (!codec_settings) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  // 确定编码器类型
  VCodecId newCodecId = VCodecId::none;
  if (codec_settings->codecType == kVideoCodecH264) {
    newCodecId = VCodecId::h264;
  } else if (codec_settings->codecType == kVideoCodecH265) {
    newCodecId = VCodecId::h265;
  } else {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  // 如果编码器类型改变，需要重新初始化
  if (codecId != newCodecId || !encode) {
    Release();
    codecId = newCodecId;
    findEncoder(codecId, bHard);
  }
  if (!encode) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  // 设置编码器参数
  VTrackDesc trackDesc = {};
  trackDesc.codecId = codecId;
  trackDesc.desc.width = codec_settings->width;
  trackDesc.desc.height = codec_settings->height;
  trackDesc.desc.fps = codec_settings->maxFramerate;
  if (bHard) {
    trackDesc.desc.type = YuvType::nv12;
  } else {
    trackDesc.desc.type = YuvType::yuv420P;
  }
  if (codec_settings->maxBitrate > 0) {
    encode->setBitrate(codec_settings->maxBitrate);
  }
  encode->setDesc(trackDesc);
  bCheckAcc = false;
  bvcc = false;
  // 放入帧时初始化
  // DecodeResult result = encode->onPreEncoder();
  // if (result != DecodeResult::success) {
  //   return WEBRTC_VIDEO_CODEC_ERROR;
  // }
  return WEBRTC_VIDEO_CODEC_OK;
}

void RtcVideoEncoder::findEncoder(VCodecId codecId_, bool bHard) {
  bool bFind = AvoxManager::Get().vEncoders.hasObjectId(codecId_);
  if (!bFind) {
    log(LogLevel::info, "not find encoder:", getVCodecName(codecId_));
    return;
  }
  const auto& encodes = AvoxManager::Get().vEncoders.initFuncs(codecId_);
  if (encodes.empty()) {
    return;
  }
  // 根据是否硬编选择编码器
  size_t sIndex = 0;
  const char* sName = getDefaultEncoderName(codecId_, bHard);
  for (size_t i = 0; i < encodes.size(); ++i) {
    if (encodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  auto& vEncode = encodes[sIndex];
  encode = std::unique_ptr<avox::VideoEncoder>(vEncode.initFunc());
  if (!encode) {
    return;
  }
  encode->setObserver(this);
  codecDesc = vEncode.desc;
}

int32_t RtcVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  if (!encode) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  // 转换为毫秒
  currentPts = frame.timestamp_us() / 1000;
  currentRtpTimestamp = frame.rtp_timestamp();
  // log(LogLevel::info, "encode frame pts:", currentPts,
  //    " rtp:", currentRtpTimestamp);
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
  // parameters.bitrate.get_sum_bps() 获取 WebRTC 期望的总比特率 (bps)
  uint32_t bitrate_bps = parameters.bitrate.get_sum_bps();
  if (bitrate_bps > 0) {
    // 将 bps 转换为你的底层编码器需要的单位（假设是 bps 或 kbps）
    // 记得在底层 encode->setBitrate 中处理这个变化
    encode->setBitrate(bitrate_bps);
    // log(LogLevel::info, "WebRTC SetRates: ", bitrate_bps / 1000,
    //     " kbps, FPS: ", parameters.framerate_fps);
  }
}

void RtcVideoEncoder::onPacket(AvoxPacket& packet) {
  // 先检查avcc,webrtc需要annexb格式,IOS硬解是avcc/hvcc
  if (!bCheckAcc) {
    bvcc = checkAvccPacket(packet.data.data, packet.data.size);
    LOGFLF(LogLevel::info, "check avcc packet:", bvcc);
    bCheckAcc = true;
  }
  if (bvcc) {
    uint32_t naluLength = (packet.data.data[0] << 24) |
                          (packet.data.data[1] << 16) |
                          (packet.data.data[2] << 8) | packet.data.data[3];
    if (packet.data.size > naluLength) {
      std::vector<AvoxPacket> spiltBufs;
      splitAvccNalu(packet, spiltBufs);
      // 表示长度的几个字节转成annexb4字节
      for (auto& buf : spiltBufs) {
        avcc2AnnexbPacket(buf);
      }
    } else {
      // 只有一个包
      avcc2AnnexbPacket(packet);
    }
  }
  processPacket(packet);
}

void RtcVideoEncoder::processPacket(AvoxPacket& packet) {
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
      LOGFLF(LogLevel::warn, "error frame id:", result.frame_id,
             " result:", result.error, " nal:", (int32_t)nal,
             " pts:", packet.pts, " size:", packet.data.size,
             " rtp:", currentRtpTimestamp);
    }
  }
}

}
