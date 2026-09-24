#pragma once

#include "FFVDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"

namespace avox {

#if AVOX_ENABLE_VULKAN && !defined(__APPLE__)

// FFmpeg Vulkan 硬解(桌面备选车道): 注册排在 dx11/vaapi 主路之后, 环境不支持时逐层降级
class FFVkDecoder : public FFVDecoder {
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
  virtual void onDetachContext() override;

protected:
  AVBufferRef *hwBuffer = nullptr;
};
#endif
}
