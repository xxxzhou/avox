#include "FFVADecoder.hpp"

#include "avox/module/AvoxManager.hpp"
// AVOX_FFVAAPI_H264/H265_DECODER 常量
#include "avox/player/AVTrack.hpp"

#if defined(__ONLY_LINUX__) && defined(AVOX_ENABLE_FFMPEG)

#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

namespace avox {

void regFFVADecoder() {
  RegFunc regFunc = {"ffmpeg vaapi video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H264_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc,
                           []() -> VideoDecoder* { return new FFVADecoder(); });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H265_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc,
                           []() -> VideoDecoder* { return new FFVADecoder(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFVADecoder::FFVADecoder() {
  codecTH = VCodecTh::cpu;
  // 构造时探测 VAAPI 设备, onVaild 据此决定是否可用(无设备时降级软解)
  av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
}

FFVADecoder::~FFVADecoder() { onDetachContext(); }

bool FFVADecoder::onVaild() {
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  // WSL/无VAAPI设备时 hwdevice 创建失败, 这里返回 false 降级软解
  if (!hwBuffer) {
    LOGFLF(LogLevel::warn, "vaapi hwBuffer is null, fallback to soft decode");
    return false;
  }
  return true;
}

void FFVADecoder::onAttachContext() {
  // 重新生成,释放老的,可能分辨率变化后重置解码器了
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
  // NULL = 按默认次序探测 DRM render node (i915/iHD/radeonsi); 构造时已探过,
  // 失败这里重试一次, 仍失败则 hw_device_ctx 为空, get_format 自动回退软解
  int32_t ret = av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VAAPI,
                                       NULL, NULL, 0);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "vaapi av_hwdevice_ctx_create failed, ret:", ret,
           " (无VAAPI设备时自动降级软解)");
    return;
  }
  codecCtx->hw_device_ctx = av_buffer_ref(hwBuffer);
  // 显式选择 VAAPI 像素格式, 不在候选里则回退默认(软解)
  codecCtx->get_format = [](AVCodecContext* ctx,
                            const enum AVPixelFormat* fmt)
      -> enum AVPixelFormat {
    for (const enum AVPixelFormat* p = fmt; *p != AV_PIX_FMT_NONE; ++p) {
      if (*p == AV_PIX_FMT_VAAPI) {
        return AV_PIX_FMT_VAAPI;
      }
    }
    return avcodec_default_get_format(ctx, fmt);
  };
  LOGFLF(LogLevel::info, "vaapi onAttachContext hwBuffer:", hwBuffer);
}

void FFVADecoder::onDetachContext() {
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
}

void FFVADecoder::onFrame(AVFrame* avFrame, bool bDrop) {
  // 非VAAPI帧(如降级软解)直接走软解链路
  if (avFrame->format != AV_PIX_FMT_VAAPI) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  // VAAPI surface -> CPU NV12 (ffmpeg 内部走 vaDeriveImage/vaMapBuffer)
  AVFrame* swFrame = av_frame_alloc();
  if (!swFrame) {
    LOGFLF(LogLevel::warn, "vaapi av_frame_alloc failed");
    return;
  }
  int32_t ret = av_hwframe_transfer_data(swFrame, avFrame, 0);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "av_hwframe_transfer_data failed, ret:", ret);
    av_frame_free(&swFrame);
    return;
  }
  // transfer_data 只搬像素, 元数据手动带上; onFrame 的 dispatch 是同步的,
  // 帧数据在消费方返回前持续有效(与软解 onFrame 的生命周期契约一致)
  swFrame->best_effort_timestamp = avFrame->best_effort_timestamp;
  swFrame->pkt_dts = avFrame->pkt_dts;
  swFrame->pict_type = avFrame->pict_type;
  FFVDecoder::onFrame(swFrame, bDrop);
  av_frame_free(&swFrame);
}

}

#endif
