#pragma once

#include "FFVDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"
#include <queue>

#if _WIN32
#include "avox_windows/dx11/Dx11Context.hpp"
#endif

namespace avox {

#if _WIN32

class FFDx11Decoder : public FFVDecoder, public Dx11Context {
public:
  FFDx11Decoder();
  virtual ~FFDx11Decoder();

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
  // 保存DX11纹理,B帧可能用纹理数组，但是数组又不是轮循在用
  // 导致需要保存出来，当前重新生成一个纹理数组，轮循使用索引
  MComPtr<ID3D11Texture2D> copyTexture;
  int32_t index = 0;
  int32_t arraySize = 0;
};

#endif

}
