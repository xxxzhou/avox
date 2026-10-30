#pragma once

#include "AvoxDef.h"

namespace avox {

// FFmpeg 运行时版本号
struct FFmpegVersionInfo {
  int32_t avcodecVersion = 0;
  int32_t avformatVersion = 0;
  int32_t avutilVersion = 0;
};

extern "C" {
AVOX_EXPORT FFmpegVersionInfo getFFmpegVersionInfo();
// 版本串/编译配置串(engine 托管, 进程内稳定)
AVOX_EXPORT const char* getFFmpegVersion();
AVOX_EXPORT const char* getFFmpegBuildConfig();
// 只查编码器是否注册, 不做真编
AVOX_EXPORT bool isEncoderRegistered(const char* name);
AVOX_EXPORT bool hasMuxer(const char* name);
// 输入/输出协议名(engine 托管, 首次调用枚举一次); index 越界返回空串
AVOX_EXPORT int32_t getProtocolCount(bool output);
AVOX_EXPORT const char* getProtocolName(bool output, int32_t index);
// 抓帧所需的 Vulkan 渲染链是否可用(首次调用真建/销 VkInstance+VkDevice, 重操作)
AVOX_EXPORT bool isVulkanAvailable();
}

}  // namespace avox
