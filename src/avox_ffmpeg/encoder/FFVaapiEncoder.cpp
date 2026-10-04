#include "FFVaapiEncoder.hpp"

#include "avox/module/AvoxManager.hpp"
// AVOX_FFVAAPI_H264/H265_ENCODER 常量
#include "avox/muxer/Muxer.hpp"

#if defined(__ONLY_LINUX__) && defined(AVOX_ENABLE_FFMPEG)

#include <libavutil/hwcontext.h>

namespace avox {

void regFFVaapiEncoder() {
  RegFunc regFunc = {"ffmpeg vaapi video encoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H264_ENCODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h264, codecDesc,
                           []() -> VideoEncoder* { return new FFVaapiEncoder(); });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H265_ENCODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h265, codecDesc,
                           []() -> VideoEncoder* { return new FFVaapiEncoder(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFVaapiEncoder::FFVaapiEncoder() {
  bHwEncode = true;
  // 构造时探测 VAAPI 设备(与 FFVADecoder 同款), encode 据此分流软编兜底
  av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
}

FFVaapiEncoder::~FFVaapiEncoder() {
  // codecCtx(基类成员)后于本析构释放, 其持有 hwBuffer 的独立引用, 次序安全
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
}

void FFVaapiEncoder::onAttachContext() {
  if (!hwBuffer) {
    // 无 VAAPI 设备(WSL/无 /dev/dri): 不动 pix_fmt(保持软格式), open2 明确
    // 失败由上层报错, 不静默
    LOGFLF(LogLevel::warn,
           "vaapi hwdevice unavailable, encoder open will fail, fallback soft");
    return;
  }
  if (desc.desc.type != YuvType::nv12) {
    // surface 池按 NV12 建(转码车道硬编喂 NV12), 其余输入格式上载会失败
    LOGFLF(LogLevel::warn, "vaapi encoder expects nv12 input, got:",
           (int32_t)desc.desc.type);
  }
  // 重置场景(分辨率变化)先放老的再重建, 防 device 与 frames_ctx 分家
  AVBufferRef* deviceRef = nullptr;
  int32_t ret = av_hwdevice_ctx_create(&deviceRef, AV_HWDEVICE_TYPE_VAAPI, NULL,
                                       NULL, 0);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "vaapi av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  av_buffer_unref(&hwBuffer);
  hwBuffer = deviceRef;
  codecCtx->hw_device_ctx = av_buffer_ref(hwBuffer);
  // vaapi 编码器只收 VAAPI surface 帧
  codecCtx->pix_fmt = AV_PIX_FMT_VAAPI;
  // surface 池: NV12, 容量留 seek/上载并发余量
  AVBufferRef* framesRef = av_hwframe_ctx_alloc(hwBuffer);
  AVHWFramesContext* frames = (AVHWFramesContext*)framesRef->data;
  frames->format = AV_PIX_FMT_VAAPI;
  frames->sw_format = AV_PIX_FMT_NV12;
  frames->width = desc.desc.width;
  frames->height = desc.desc.height;
  frames->initial_pool_size = 8;
  ret = av_hwframe_ctx_init(framesRef);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "vaapi av_hwframe_ctx_init failed");
    av_buffer_unref(&framesRef);
    return;
  }
  codecCtx->hw_frames_ctx = av_buffer_ref(framesRef);
  av_buffer_unref(&framesRef);
  LOGFLF(LogLevel::info, "vaapi encoder attached:", desc.desc.width, "x",
         desc.desc.height);
}

DecodeResult FFVaapiEncoder::encode(const YUVFrame& yframe) {
  // 无 VAAPI 设备: 回基类软编链路(Linux 白名单无视频软编时 openFailed 明确报错)
  if (!hwBuffer) {
    return FFVEncoder::encode(yframe);
  }
  // 分辨率变化/未初始化时重走 onPreEncoder(与基类判定一致, 内部回调本类
  // onAttachContext 建 hwdevice/hwframes 并置 pix_fmt=VAAPI)
  bool bChange = desc.desc.width != yframe.format.width ||
                 desc.desc.height != yframe.format.height;
  if (!codecCtx || !frame || bChange) {
    if (bChange) {
      LOGFLF(LogLevel::info, "old size width:", desc.desc.width,
             " height:", desc.desc.height,
             " change size width:", yframe.format.width,
             " height:", yframe.format.height);
    }
    yuvType = yframe.format.type;
    desc.desc.width = yframe.format.width;
    desc.desc.height = yframe.format.height;
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  if (yframe.format.type != YuvType::nv12 || !codecCtx->hw_frames_ctx) {
    LOGFLF(LogLevel::warn, "vaapi encode needs nv12 + hw_frames_ctx, type:",
           (int32_t)yframe.format.type);
    return DecodeResult::dataError;
  }
  // 取 surface 后同步上载 CPU NV12(sw 视图只引用调用方指针, 不拷贝头结构外数据)
  av_frame_unref(frame.get());
  frame->format = AV_PIX_FMT_VAAPI;
  frame->width = yframe.format.width;
  frame->height = yframe.format.height;
  frame->pts = yframe.pts;
  int32_t ret = av_hwframe_get_buffer(codecCtx->hw_frames_ctx, frame.get(), 0);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "vaapi av_hwframe_get_buffer failed");
    return DecodeResult::dataError;
  }
  AVFrame* sw = av_frame_alloc();
  sw->format = getFFVideoFormat(yframe.format.type);
  sw->width = yframe.format.width;
  sw->height = yframe.format.height;
  sw->data[0] = yframe.data[0];
  sw->data[1] = yframe.data[1];
  sw->data[2] = yframe.data[2];
  sw->linesize[0] = yframe.stride[0];
  sw->linesize[1] = yframe.stride[1];
  sw->linesize[2] = yframe.stride[2];
  ret = av_hwframe_transfer_data(frame.get(), sw, 0);
  av_frame_free(&sw);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "vaapi av_hwframe_transfer_data upload failed");
    return DecodeResult::dataError;
  }
  ret = avcodec_send_frame(codecCtx.get(), frame.get());
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      return DecodeResult::complete;
    }
    if (ret == AVERROR(EAGAIN)) {
      return DecodeResult::dataNoReady;
    }
    AVOX_FFMEPG_LOG(ret, "vaapi avcodec_send_frame failed");
    return DecodeResult::dataError;
  }
  while (true) {
    AVPacket* packet = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx.get(), packet);
    if (ret < 0) {
      av_packet_free(&packet);
      if (ret == AVERROR_EOF) {
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        break;
      }
      AVOX_FFMEPG_LOG(ret, "vaapi avcodec_receive_packet failed");
      return DecodeResult::dataError;
    }
    // time_base 已是毫秒, rescale 保持与基类同口径
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::video;
    dispatch(&IEncoderOb::onPacket, cpacket);
    av_packet_free(&packet);
  }
  return DecodeResult::success;
}

}

#endif
