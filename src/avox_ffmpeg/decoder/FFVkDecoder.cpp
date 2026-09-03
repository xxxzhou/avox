#include "FFVkDecoder.hpp"
#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"

#if AVOX_ENABLE_VULKAN
#include <libavutil/hwcontext_vulkan.h>
#endif

namespace avox {

#if AVOX_ENABLE_VULKAN

void regFFVkDecoder() {
  // RegFunc regFunc = {"ffmpeg vulkan video decoder init", []() {
  //                      // H264
  //                      VCodecDesc codecDesc = {};
  //                      codecDesc.name = AVOX_FFVULKAN_H264_DECODER;
  //                      codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
  //                      codecDesc.bHardware = true;
  //                      codecDesc.vcodecId = VCodecId::h264;
  //                      AvoxManager::Get().vDecoders.regInitFunc(
  //                          VCodecId::h264, codecDesc, []() -> VideoDecoder * {
  //                            return new FFVkDecoder();
  //                          });
  //                      // H265
  //                      codecDesc = {};
  //                      codecDesc.name = AVOX_FFVULKAN_H265_DECODER;
  //                      codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
  //                      codecDesc.bHardware = true;
  //                      codecDesc.vcodecId = VCodecId::h265;
  //                      AvoxManager::Get().vDecoders.regInitFunc(
  //                          VCodecId::h265, codecDesc, []() -> VideoDecoder * {
  //                            return new FFVkDecoder();
  //                          });
  //                    }};
  // AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFVkDecoder::FFVkDecoder() {
  codecTH = VCodecTh::vulkan;
  // 尝试从外部设置vkinstance不可行 vulkan_device_create_internal
  // AVBufferRef *hwBuffer = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
  // AVHWDeviceContext *hwCtx = (AVHWDeviceContext *)hwBuffer->data;
  // AVVulkanDeviceContext *vkCtx = (AVVulkanDeviceContext *)hwCtx->hwctx;
  // vkCtx->inst = vkInstance;
  // int ret = av_hwdevice_ctx_init(hwBuffer);
  // 创建 Vulkan 硬件设备上下文 vulkan_device_create_internal
  // 如果是Debug模式，添加Debug信息
  AVDictionary *opts = NULL;
#if AVOX_DEBUG
  // av_dict_set(&opts, "debug", "1", 0);
#endif
  int32_t ret =
      av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VULKAN, NULL, opts, 0);
  av_dict_free(&opts);
  if (ret < 0) {
    LOGFLF(LogLevel::error, "av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  // 获取 AVHWDeviceContext 指针
  AVHWDeviceContext *hwCtx = (AVHWDeviceContext *)hwBuffer->data;
  AVVulkanDeviceContext *vkCtx = (AVVulkanDeviceContext *)hwCtx->hwctx;
  // 设置 Vulkan 实例、物理设备和活动设备
  initContext(vkCtx->inst, vkCtx->phys_dev, vkCtx->act_dev);
}

FFVkDecoder::~FFVkDecoder() {
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
  }
}

bool FFVkDecoder::onVaild() {
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  if (!hwBuffer) {
    LOGFLF(LogLevel::error, "hwBuffer is null");
    return false;
  }
  return true;
}

void FFVkDecoder::onFrame(AVFrame *avFrame, bool bDrop) {
  // codecCt有可能不是用的硬件解码器
  AVPixelFormat vkFormat = (AVPixelFormat)avFrame->format;
  if (vkFormat != AV_PIX_FMT_VULKAN) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  AVVulkanFramesContext *vkFrameCtx =
      (AVVulkanFramesContext *)codecCtx->hw_frames_ctx;
  VkFormat vf = vkFrameCtx->format[0];
  // VkImage vkImage = (VkImage)avFrame->data[0];
}

void FFVkDecoder::onAttachContext() {
  codecCtx->hw_device_ctx = hwBuffer;
  LOGFLF(LogLevel::info, "vk onAttachContext hwBuffer:", hwBuffer,
         " codecCtx->hw_device_ctx:", codecCtx->hw_device_ctx);
}

#endif

}