#include "FFVkDecoder.hpp"

#include "avox/module/AvoxManager.hpp"
// AVOX_FFVULKAN_H264/H265/VP9/AV1_DECODER 常量
#include "avox/player/AVTrack.hpp"

#if AVOX_ENABLE_VULKAN && !defined(__APPLE__)
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#endif

namespace avox {

#if AVOX_ENABLE_VULKAN

#if defined(__APPLE__)
// Apple: MoltenVK 无 VK_KHR_video_queue, vulkan 硬解不可用, 注册留空(走软解)
void regFFVkDecoder() {}

#else

namespace {
// FFmpeg 是否为该编码编入了 vulkan hwaccel(hwconfig 表里找 VULKAN 像素格式)
bool hasVulkanHwaccel(AVCodecID codecId) {
  const AVCodec* codec = avcodec_find_decoder(codecId);
  if (!codec) {
    return false;
  }
  for (int i = 0;; ++i) {
    const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
    if (!config) {
      return false;
    }
    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->pix_fmt == AV_PIX_FMT_VULKAN) {
      return true;
    }
  }
}
}  // namespace

void regFFVkDecoder() {
  RegFunc regFunc = {"ffmpeg vulkan video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFVULKAN_H264_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc,
                           []() -> VideoDecoder* { return new FFVkDecoder(); });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVULKAN_H265_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc,
                           []() -> VideoDecoder* { return new FFVkDecoder(); });
#if defined(__ONLY_LINUX__)
                       // VP9/AV1: Linux Vulkan Video 车道(P0 补齐, 仅 Linux 注册,
                       // Win/And 各有原生车道不动); onVaild 探测 hwaccel 编入,
                       // 驱动级能力靠运行期 openFailed 上浮降级
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVULKAN_VP9_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_VP9;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::vp9;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::vp9, codecDesc,
                           []() -> VideoDecoder* { return new FFVkDecoder(); });
                       // AV1
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVULKAN_AV1_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_AV1;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::av1;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::av1, codecDesc,
                           []() -> VideoDecoder* { return new FFVkDecoder(); });
#endif
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFVkDecoder::FFVkDecoder() {
  // 输出 CPU NV12 帧, 渲染侧与软解同车道(上传), 不走 dx11 的 GPU 纹理直通
  codecTH = VCodecTh::cpu;
  // 构造时探测 Vulkan 运行时; 加载器缺失/驱动不可用时创建失败,
  // onVaild 返回 false, 由解码器选择链跳到下一候选
  av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VULKAN, NULL, NULL, 0);
}

FFVkDecoder::~FFVkDecoder() { onDetachContext(); }

bool FFVkDecoder::onVaild() {
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  if (!hwBuffer) {
    LOGFLF(LogLevel::warn, "vulkan hwBuffer is null, skip vulkan decoder");
    return false;
  }
  // vp9/av1 先确认 FFmpeg 侧 vulkan hwaccel 已编入(白名单缺项时车道必不可用);
  // 驱动是否有 video decode 队列在运行期由 openFailed 上浮降级(P2 再补 caps 探测)
  if ((codecDesc.vcodecId == VCodecId::vp9 ||
       codecDesc.vcodecId == VCodecId::av1) &&
      !hasVulkanHwaccel((AVCodecID)codecDesc.codecId)) {
    LOGFLF(LogLevel::warn, "vulkan hwaccel not compiled for vcodec:",
           (int32_t)codecDesc.vcodecId, ", skip vulkan decoder");
    return false;
  }
  return true;
}

void FFVkDecoder::onAttachContext() {
  // 重新生成,释放老的,可能分辨率变化后重置解码器了
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
  int32_t ret = av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VULKAN,
                                       NULL, NULL, 0);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "vulkan av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  codecCtx->hw_device_ctx = av_buffer_ref(hwBuffer);
  // 显式选择 VULKAN 像素格式; 候选里没有(hwaccel 未编入/驱动不支持该编码)时
  // 回退默认(软解), 解码会话协商失败则在 open 阶段报错, 由选择链降级
  codecCtx->get_format = [](AVCodecContext* ctx,
                            const enum AVPixelFormat* fmt)
      -> enum AVPixelFormat {
    for (const enum AVPixelFormat* p = fmt; *p != AV_PIX_FMT_NONE; ++p) {
      if (*p == AV_PIX_FMT_VULKAN) {
        return AV_PIX_FMT_VULKAN;
      }
    }
    return avcodec_default_get_format(ctx, fmt);
  };
  LOGFLF(LogLevel::info, "vulkan onAttachContext hwBuffer:", hwBuffer);
}

void FFVkDecoder::onDetachContext() {
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
}

void FFVkDecoder::onFrame(AVFrame* avFrame, bool bDrop) {
  // 非VULKAN帧(软解回退)直接走软解链路
  if (avFrame->format != AV_PIX_FMT_VULKAN) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  // VkImage -> CPU NV12 (ffmpeg 内部走 staging 拷贝/映射)
  AVFrame* swFrame = av_frame_alloc();
  if (!swFrame) {
    LOGFLF(LogLevel::warn, "vulkan av_frame_alloc failed");
    return;
  }
  int32_t ret = av_hwframe_transfer_data(swFrame, avFrame, 0);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "vulkan av_hwframe_transfer_data failed, ret:", ret);
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

#endif

}

#endif
