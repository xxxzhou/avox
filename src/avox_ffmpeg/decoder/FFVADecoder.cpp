#include "FFVADecoder.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/AVTrack.hpp"

namespace avox {

#if defined(__linux1__) && !defined(__ANDROID__)
#if AVOX_ENABLE_VULKAN

void regFFDx11Decoder() {
  RegFunc regFunc = {"ffmpeg vaapi video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H264_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoDecoder * {
                             return new FFVADecoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFVAAPI_H265_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoDecoder * {
                             return new FFVADecoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFVADecoder::FFVADecoder() {
  // 如果是Debug模式，添加Debug信息
  AVDictionary *opts = NULL;
  // 创建 VAAPI 硬件设备上下文
  int32_t ret =
      av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_VAAPI, NULL, opts, 0);
  av_dict_free(&opts);
  if (ret < 0) {
    LOGFLF(LogLevel::error, "av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  AVHWFramesContext *hwCtx = (AVHWFramesContext *)hwBuffer->data;
  AVVAAPIDeviceContext *va_device_ctx = (AVVAAPIDeviceContext *)hwCtx->hwctx;
  VADisplay va_display = va_device_ctx->display;
}

FFVADecoder::~FFVADecoder() {}

bool FFVADecoder::onVaild() {
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

void FFVADecoder::onFrame(AVFrame *avFrame, bool bDrop) {
  AVPixelFormat vaFormat = (AVPixelFormat)avFrame->format;
  if (vaFormat != AV_PIX_FMT_VAAPI) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  AVHWFramesContext *hw_frames_ctx =
      (AVHWFramesContext *)avFrame->hw_frames_ctx->data;
  AVVAAPIDeviceContext *va_device_ctx = hw_device_ctx->hwctx;
  VADisplay va_display = va_device_ctx->display;
  // 获取 VAAPI 表面
  VASurfaceID surface = (VASurfaceID)(uintptr_t)avFrame->data[3];
  if (!surface) {
    // 无效表面处理
    return;
  }
}

void FFVADecoder::bindVk() {
  // 获取 VAAPI 表面
  VASurfaceID surface = (VASurfaceID)(uintptr_t)avFrame->data[3];
  // 获取 VAAPI 表面的 DMA-BUF 文件描述符
  int dmaBufFd = -1;
  VAStatus status =
      vaExportSurfaceHandle(vaDisplay,                          // VA 显示句柄
                            vaSurface,                          // VASurfaceID
                            VA_SURFACE_ATTRIB_MEM_TYPE_DMA_BUF, // 内存类型
                            VA_EXPORT_SURFACE_READ_ONLY,        // 权限
                            &dmaBufFd // 输出文件描述符
      );
  if (status != VA_STATUS_SUCCESS) {
    // 错误处理
  }
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = nullptr;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // 根据实际格式调整
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  // 指定外部内存
  VkExternalMemoryImageCreateInfo externalInfo = {};
  externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  imageInfo.pNext = &externalInfo;

  VkImage vkImage;
  VkResult result = vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage);
  if (result != VK_SUCCESS) {
    // 错误处理
  }
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(vkDevice, vkImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = // 选择合适的内存类型索引

      // 指定外部内存
      VkImportMemoryFdInfoKHR importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
  importInfo.fd = dmaBufFd;
  importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  allocInfo.pNext = &importInfo;

  VkDeviceMemory vkMemory;
  result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &vkMemory);
  if (result != VK_SUCCESS) {
    // 错误处理
  }
  result = vkBindImageMemory(vkDevice, vkImage, vkMemory, 0);
  if (result != VK_SUCCESS) {
    // 错误处理
  }
}

void FFVADecoder::onAttachContext() {}

#endif
#endif

}
