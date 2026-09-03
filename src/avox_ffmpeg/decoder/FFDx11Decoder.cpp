#include "FFDx11Decoder.hpp"

#include "avox/codec/H26XHelper.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/AVTrack.hpp"

#if _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif

namespace avox {

#if _WIN32

void regFFDx11Decoder() {
  RegFunc regFunc = {"ffmpeg dx11 video decoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H264_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoDecoder* {
                             return new FFDx11Decoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H265_DECODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vDecoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoDecoder* {
                             return new FFDx11Decoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFDx11Decoder::FFDx11Decoder() { codecTH = VCodecTh::dx11; }

FFDx11Decoder::~FFDx11Decoder() {
  onDetachContext();
}

bool FFDx11Decoder::onVaild() {
  if (codecDesc.codecId <= 0) {
    LOGFLF(LogLevel::warn, "codecId:", codecDesc.codecId);
    return false;
  }
  return true;
}

void FFDx11Decoder::onAttachContext() {
  // 重新生成,释放老的,可能分辨率变化后重置解码器了
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
  // 如果是Debug模式，添加Debug信息
  AVDictionary* opts = NULL;
#if AVOX_DEBUG
  av_dict_set(&opts, "debug", "1", 0);
#endif
  av_dict_set_int(&opts, "surfaces", 8, 0);
  // 创建 D3D11 硬件设备上下文
  int32_t ret = av_hwdevice_ctx_create(&hwBuffer, AV_HWDEVICE_TYPE_D3D11VA,
                                       NULL, opts, 0);
  av_dict_free(&opts);
  if (ret < 0) {
    LOGFLF(LogLevel::error, "av_hwdevice_ctx_create failed, ret:", ret);
    return;
  }
  // 获取 AVHWDeviceContext 指针
  AVHWDeviceContext* hwCtx = (AVHWDeviceContext*)hwBuffer->data;
  AVD3D11VADeviceContext* dx11Ctx = (AVD3D11VADeviceContext*)hwCtx->hwctx;
  // 设置 D3D11 设备上下文
  device = dx11Ctx->device;
  setDevice(device);
  codecCtx->hw_device_ctx = av_buffer_ref(hwBuffer);
  LOGFLF(LogLevel::info, "dx11 onAttachContext hwBuffer:", hwBuffer,
         " codecCtx->hw_device_ctx:", codecCtx->hw_device_ctx);
  copyTexture.Reset();
}

void FFDx11Decoder::onDetachContext() {
  if (hwBuffer) {
    av_buffer_unref(&hwBuffer);
    hwBuffer = nullptr;
  }
}

void FFDx11Decoder::onFrame(AVFrame* avFrame, bool bDrop) {
  // codecCt有可能不是用的硬件解码器
  AVPixelFormat dx11Format = (AVPixelFormat)avFrame->format;
  if (dx11Format != AV_PIX_FMT_D3D11) {
    FFVDecoder::onFrame(avFrame, bDrop);
    return;
  }
  // hwcontext_d3d11va.h
  ID3D11Texture2D* dx11Texture = (ID3D11Texture2D*)avFrame->data[0];
  if (!dx11Texture) {
    log(LogLevel::info, "dx11Texture is null");
    return;
  }
  D3D11_TEXTURE2D_DESC srcDesc;
  dx11Texture->GetDesc(&srcDesc);
  bool bCrateTexture = false;
  if (srcDesc.ArraySize > 1) {
    if (!copyTexture) {
      bCrateTexture = true;
    } else {
      D3D11_TEXTURE2D_DESC copyDesc;
      copyTexture->GetDesc(&copyDesc);
      if (copyDesc.Width != srcDesc.Width ||
          copyDesc.Height != srcDesc.Height) {
        bCrateTexture = true;
      }
    }
  }
  if (bCrateTexture) {
    device->CreateTexture2D(&srcDesc, NULL, &copyTexture);
    arraySize = srcDesc.ArraySize;
    index = 0;
  }
  int64_t queueIndex = reinterpret_cast<intptr_t>(avFrame->data[1]);
  if (srcDesc.ArraySize > 1) {
    d3dcontext->CopySubresourceRegion(
        copyTexture.Get(), D3D11CalcSubresource(0, index, srcDesc.MipLevels), 0,
        0, 0, dx11Texture,
        D3D11CalcSubresource(0, queueIndex, srcDesc.MipLevels), nullptr);
  }
  GpuFrame frame = {};
  frame.pts = avFrame->best_effort_timestamp;
  frame.dts = avFrame->pkt_dts;
  frame.format.width = avFrame->width;
  frame.format.height = avFrame->height;
  frame.format.type = getDxFormat(srcDesc.Format);
  frame.context = this;
  // 解码器并不是循环在用纹理数组，这样外面保存索引不对
  // 因为解码器可能一直在11/19/11/19二张索引上来回读写，这样就覆盖了
  if (srcDesc.ArraySize > 1) {
    frame.buffer = copyTexture.Get();
    frame.queueIndex = index;
    index = (index + 1) % arraySize;
  } else {
    frame.buffer = dx11Texture;
    frame.queueIndex = queueIndex;
  }
  // log(LogLevel::info, "onFrame pts:", frame.pts);
  // log(LogLevel::info, "array index:", queueIndex, " pts:", avFrame->pts,
  //     " dts:", avFrame->pkt_dts);
  dispatch(&IVideoDecoderOb::onDecodeGpu, frame);
}

#endif

}
