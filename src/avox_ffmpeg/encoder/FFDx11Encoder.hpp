#pragma once

#include "FFVEncoder.hpp"
#if _WIN32
#include "avox_windows/dx11/DX11VideoYUV.hpp"
#include "avox_windows/dx11/Dx11Context.hpp"
#include "avox_windows/dx11/Dx11Resource.hpp"
#endif

namespace avox {

#if _WIN32

// D3D11VA不支持编码，暂时不实现，后续再看
class FFDx11Encoder : public FFVEncoder, public Dx11Context {
 public:
  FFDx11Encoder();
  virtual ~FFDx11Encoder();

 protected:
  AVBufferRef* hwBuffer = nullptr;
  // 输入的RGBA纹理
  std::unique_ptr<Dx11Texture> inTexture = nullptr;
  // RGBA转NV12
  std::unique_ptr<Dx11VideoYuv> r2y = nullptr;

 public:
  virtual DecodeResult encode(const GpuFrame& frame) override;

 protected:
  virtual void onAttachContext() override;
};
#endif

}