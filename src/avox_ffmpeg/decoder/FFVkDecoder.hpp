#pragma once

#include "FFVDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"
#if AVOX_ENABLE_VULKAN
#include "avox_vulkan/VkContext.hpp"
#endif

namespace avox {

#if AVOX_ENABLE_VULKAN

class FFVkDecoder : public FFVDecoder, public VkContext {
public:
  FFVkDecoder();
  virtual ~FFVkDecoder();

public:
  // 初始化
  virtual bool onVaild() override;

protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;

protected:
  virtual void onAttachContext() override;

protected:
  AVBufferRef *hwBuffer = nullptr;
};
#endif
}