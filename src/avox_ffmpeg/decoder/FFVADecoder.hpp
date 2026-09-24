#pragma once

#include "FFVDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"

#if defined(__ONLY_LINUX__) && defined(AVOX_ENABLE_FFMPEG)

namespace avox {

// Linux VAAPI 硬解: 解码落在 VAAPI surface, 经 av_hwframe_transfer_data
// 映射回 CPU NV12 后复用软解 YUVFrame 链路 (解码省CPU, 拷贝开销小;
// VAAPI->Vulkan 零拷贝导入留待真机验证后迭代)
// 注: WSL 无 /dev/dri, av_hwdevice_ctx_create 会失败, onVaild 返回 false
// 自动降级软解
class FFVADecoder : public FFVDecoder {
 public:
  FFVADecoder();
  virtual ~FFVADecoder();

 public:
  // 初始化
  virtual bool onVaild() override;

 protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame* avFrame, bool bDrop) override;

 protected:
  virtual void onAttachContext() override;
  virtual void onDetachContext() override;

 protected:
  AVBufferRef* hwBuffer = nullptr;
};

}

#endif
