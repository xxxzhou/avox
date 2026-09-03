#pragma once

#include "FFVDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"
#if AVOX_ENABLE_VULKAN
#include "avox_vulkan/VkContext.hpp"
#endif
// 必须引入的核心头文件
#if defined(__linux1__) && !defined(__ANDROID__)
#include <va/va.h>
// Linux 平台扩展
#include <va/va_drm.h>
#endif

namespace avox {

#if defined(__linux1__) && !defined(__ANDROID__)
#if AVOX_ENABLE_VULKAN

class FFVADecoder : public FFVDecoder, public VkContext {
public:
  FFVADecoder();
  virtual ~FFVADecoder();

public:
  // 初始化
  virtual bool onVaild() override;

public:
  void bindVk();

protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;

protected:
  virtual void onAttachContext() override;

protected:
  AVBufferRef *hwBuffer = nullptr;
  VADisplay va_display = nullptr;
};

#endif
#endif

}