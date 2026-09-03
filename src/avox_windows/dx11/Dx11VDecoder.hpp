#pragma once

#include "avox/video/VideoDecoder.hpp"
#if AVOX_ENABLE_DX11VA
#include <d3d11.h>
#include <dxva.h>
#endif
#include "Dx11Helper.hpp"


namespace avox {

#if AVOX_ENABLE_DX11VA
// 和vulkan解码一样，太复杂，后期有时间再完善
class Dx11VDecoder : public VideoDecoder {
public:
  Dx11VDecoder();
  virtual ~Dx11VDecoder();

private:
  MComPtr<ID3D11Device> d3dDevice;
  MComPtr<ID3D11DeviceContext> d3dContext;
  MComPtr<ID3D11VideoDevice> videoDevice;
  MComPtr<ID3D11VideoContext> videoContext;
  MComPtr<ID3D11VideoDecoder> videoDecoder;

  D3D11_VIDEO_DECODER_DESC decoderDesc = {};
  D3D11_VIDEO_DECODER_CONFIG decoderConfig = {};

  bool initD3D11Device();
  bool createVideoDecoder();

public:
  // VideoDecoder接口实现
  virtual bool onVaild() override;
  virtual DecodeResult onPreDecoder() override;
  virtual bool decode(const AvoxPacket & packet) override;
  virtual void flush() override;
  virtual void onClose() override;

private:
  void close();
  void updateYuvFormat();
};

#endif

}
