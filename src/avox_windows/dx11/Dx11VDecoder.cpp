#include "Dx11VDecoder.hpp"
#include "avox/codec/H26XHelper.hpp"
#include <iostream>


namespace avox {

#if AVOX_ENABLE_DX11VA

Dx11VDecoder::Dx11VDecoder() {}

Dx11VDecoder::~Dx11VDecoder() { close(); }

void Dx11VDecoder::close() {
  videoDecoder.Reset();
  videoContext.Reset();
  videoDevice.Reset();
  d3dContext.Reset();
  d3dDevice.Reset();
}

bool Dx11VDecoder::initD3D11Device() {
  HRESULT hr =
      D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                        0, D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);

  if (FAILED(hr)) {
    LOGFLF(LogLevel::error, "Failed to create D3D11 device: ", hr);
    return false;
  }
  hr = d3dDevice.As(&videoDevice);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::error, "Failed to get ID3D11VideoDevice: ", hr);
    return false;
  }
  hr = d3dContext.As(&videoContext);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::error, "Failed to get ID3D11VideoContext: ", hr);
    return false;
  }
  return true;
}

bool Dx11VDecoder::createVideoDecoder() {
  decoderDesc.Guid = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
  decoderDesc.SampleWidth = yuvFormat.width;
  decoderDesc.SampleHeight = yuvFormat.height;
  decoderDesc.OutputFormat = DXGI_FORMAT_NV12;

  UINT configCount = 0;
  HRESULT hr =
      videoDevice->GetVideoDecoderConfigCount(&decoderDesc, &configCount);
  if (FAILED(hr) || configCount == 0) {
    LOGFLF(LogLevel::error, "No valid decoder configurations found");
    return false;
  }

  for (UINT i = 0; i < configCount; i++) {
    hr = videoDevice->GetVideoDecoderConfig(&decoderDesc, i, &decoderConfig);
    if (SUCCEEDED(hr)) {
      break;
    }
  }

  hr = videoDevice->CreateVideoDecoder(&decoderDesc, &decoderConfig,
                                       &videoDecoder);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::error, "Failed to create video decoder: ", hr);
    return false;
  }

  return true;
}

bool Dx11VDecoder::onVaild() { return initD3D11Device(); }

DecodeResult Dx11VDecoder::onPreDecoder() {
  parseConfigs();
  // updateYuvFormat();
  LOGFLF(LogLevel::info, "D3D11VA decoder initialized, width:", yuvFormat.width,
         " height:", yuvFormat.height);
  return DecodeResult::success;
}

bool Dx11VDecoder::decode(const AvoxPacket & packet) {
  if (!videoDecoder || !videoContext) {
    return false;
  }
  // 解析NAL单元数据
  uint8_t nalu_type = getNalUnit(codecDesc.vcodecId,packet);

  // 准备DXVA结构体
  DXVA_PicParams_H264 picParams = {};
  DXVA_Slice_H264_Short sliceInfo = {};

  // 填充picParams基本信息（需根据SPS/PPS和当前帧实际情况完善）
  picParams.wFrameWidthInMbsMinus1 = (yuvFormat.width / 16) - 1;
  picParams.wFrameHeightInMbsMinus1 = (yuvFormat.height / 16) - 1;

  // TODO: 根据SPS/PPS和当前NAL单元填充picParams的其他字段，如参考帧、帧类型等

  // 填充sliceInfo
  sliceInfo.BSNALunitDataLocation = 0;
  sliceInfo.SliceBytesInBuffer = nalu_size;
  sliceInfo.wBadSliceChopping = 0;

  // 创建输出视图
  D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC viewDesc = {};
  viewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
  viewDesc.Texture2D.ArraySlice = 0;

//   Microsoft::WRL::ComPtr<ID3D11VideoDecoderOutputView> outputView;
//   HRESULT hr = videoDevice->CreateVideoDecoderOutputView(
//       outputTexture.Get(), &viewDesc, &outputView);
//   if (FAILED(hr)) {
//     LOGFLF(LogLevel::error, "Failed to create video decoder output view: ", hr);
//     return false;
//   }

//   // 调用DecodeFrame进行解码
//   hr = videoContext->DecodeFrame(videoDecoder.Get(), &picParams,
//                                  sizeof(DXVA_PicParams_H264), &sliceInfo, 1,
//                                  nalu, nalu_size, outputView.Get());
//   if (FAILED(hr)) {
//     LOGFLF(LogLevel::error, "Failed to decode frame: ", hr);
//     return false;
//   }

  return true;
}

void Dx11VDecoder::flush() {}

void Dx11VDecoder::onClose() {}

void Dx11VDecoder::onRender(bool bRender, int64_t queueIndex) {}

#endif

}