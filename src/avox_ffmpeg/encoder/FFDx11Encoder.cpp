#include "FFDx11Encoder.hpp"
#include "avox/module/AvoxManager.hpp"

#if _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif

namespace avox {
#if _WIN32

void regFFDx11Encoder() {
  RegFunc regFunc = {"ffmpeg dx11 video encoder init", []() {
                       // H264
                       VCodecDesc codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H264_ENCODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H264;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h264;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h264, codecDesc, []() -> VideoEncoder * {
                             return new FFDx11Encoder();
                           });
                       // H265
                       codecDesc = {};
                       codecDesc.name = AVOX_FFDX11_H265_ENCODER;
                       codecDesc.codecId = AVCodecID::AV_CODEC_ID_H265;
                       codecDesc.bHardware = true;
                       codecDesc.vcodecId = VCodecId::h265;
                       AvoxManager::Get().vEncoders.regInitFunc(
                           VCodecId::h265, codecDesc, []() -> VideoEncoder * {
                             return new FFDx11Encoder();
                           });
                     }};
  // AvoxManager::Get().initFuncs.push_back(regFunc);
}

FFDx11Encoder::FFDx11Encoder() { bHwEncode = true; }

FFDx11Encoder::~FFDx11Encoder() {}

void FFDx11Encoder::onAttachContext() {
  // 如果是Debug模式，添加Debug信息
  AVDictionary *opts = NULL;
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
  AVHWDeviceContext *hwCtx = (AVHWDeviceContext *)hwBuffer->data;
  AVD3D11VADeviceContext *dx11Ctx = (AVD3D11VADeviceContext *)hwCtx->hwctx;
  // 设置 D3D11 设备上下文
  device = dx11Ctx->device;
  setDevice(device);
  codecCtx->hw_device_ctx = hwBuffer;
  LOGFLF(LogLevel::info, "dx11 onAttachContext hwBuffer:", hwBuffer,
         " codecCtx->hw_device_ctx:", codecCtx->hw_device_ctx);
  codecCtx->pix_fmt = AV_PIX_FMT_D3D11;
  // 创建帧上下文
  AVBufferRef *frames_ctx = av_hwframe_ctx_alloc(hwBuffer);
  AVHWFramesContext *frames_ctx_data = (AVHWFramesContext *)frames_ctx->data;
  frames_ctx_data->format = AV_PIX_FMT_D3D11;
  frames_ctx_data->sw_format = AV_PIX_FMT_NV12;
  frames_ctx_data->width = desc.desc.width;
  frames_ctx_data->height = desc.desc.height;
  av_hwframe_ctx_init(frames_ctx);
  codecCtx->hw_frames_ctx = av_buffer_ref(frames_ctx);
  // 设置纹理
  inTexture = std::make_unique<Dx11Texture>();
  inTexture->setTextureSize(desc.desc.width, desc.desc.height);
  inTexture->initResource(device);
  // Dx11Context中与外部对接的纹理
  setTexture(inTexture->texture.Get());
  //
  r2y = std::make_unique<Dx11VideoYuv>();
}

DecodeResult FFDx11Encoder::encode(const GpuFrame &gframe) {
  if (!codecCtx) {
    DecodeResult result = onPreEncoder();
    if (result != DecodeResult::success) {
      return result;
    }
  }
  // 把RGBA纹理转成NV12纹理
  r2y->renderFrame(gframe);
  // GPU帧
  frame->format = AV_PIX_FMT_D3D11;
  frame->width = gframe.format.width;
  frame->height = gframe.format.height;
  frame->data[0] = (uint8_t *)r2y->getNv12Texture();
  frame->pts = gframe.pts;
  frame->hw_frames_ctx = av_buffer_ref(codecCtx->hw_frames_ctx);
  int32_t ret = avcodec_send_frame(codecCtx.get(), frame.get());
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      return DecodeResult::complete;
    }
    if (ret == AVERROR(EAGAIN)) {
      return DecodeResult::dataNoReady;
    } else {
      AVOX_FFMEPG_LOG(ret, "avcodec_send_frame failed");
      return DecodeResult::dataError;
    }
  }
  while (true) {
    AVPacket *packet = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx.get(), packet);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        break;
      } else {
        AVOX_FFMEPG_LOG(ret, "avcodec_receive_packet failed");
        return DecodeResult::dataError;
      }
    }
    // 时间戳转为毫秒 time_base本身是ms可以不需要
    av_packet_rescale_ts(packet, codecCtx->time_base, {1, 1000});
    AvoxPacket cpacket = ffAvoxPacket(packet);
    cpacket.packtype = (int32_t)PackType::video;
    // log(LogLevel::info, "onPacket pts:", cpacket.pts, " size:",
    // cpacket.data.size);
    dispatch(&IEncoderOb::onPacket, cpacket);
    av_packet_unref(packet);
  }
  // 把RGBA纹理转成NV12纹理
  return DecodeResult::success;
}

#endif

}