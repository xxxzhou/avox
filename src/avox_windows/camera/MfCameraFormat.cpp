#include "MfCameraFormat.hpp"

namespace avox {

YuvType mfSubtypeToYuvType(const GUID& subtype) {
  if (subtype == MFVideoFormat_NV12) {
    return YuvType::nv12;
  } else if (subtype == MFVideoFormat_YUY2) {
    return YuvType::yuv2I;
  } else if (subtype == MFVideoFormat_YVYU) {
    return YuvType::yvyuI;
  } else if (subtype == MFVideoFormat_UYVY) {
    return YuvType::uyvyI;
  } else if (subtype == MFVideoFormat_MJPG) {
    // MJPG按aoce手法把reader输出subtype改写为YUY2, 由MF内置解码器出帧
    return YuvType::yuv2I;
  }
  return YuvType::other;
}

bool getMfCameraFormats(IMFMediaSource* source,
                        std::vector<MfCameraFormat>& formats,
                        MComPtr<IMFMediaTypeHandler>& handler) {
  MComPtr<IMFPresentationDescriptor> pd = nullptr;
  MComPtr<IMFStreamDescriptor> sd = nullptr;
  BOOL selected = false;
  DWORD count = 0;
  AVOX_WIN_LOG_RETURN_FALSE(source->CreatePresentationDescriptor(&pd),
                            "mf camera create presentation descriptor failed");
  AVOX_WIN_LOG_RETURN_FALSE(pd->GetStreamDescriptorByIndex(0, &selected, &sd),
                            "mf camera get stream descriptor failed");
  AVOX_WIN_LOG_RETURN_FALSE(sd->GetMediaTypeHandler(&handler),
                            "mf camera get media type handler failed");
  AVOX_WIN_LOG_RETURN_FALSE(handler->GetMediaTypeCount(&count),
                            "mf camera get media type count failed");
  formats.clear();
  for (DWORD i = 0; i < count; i++) {
    MComPtr<IMFMediaType> type = nullptr;
    if (FAILED(handler->GetMediaTypeByIndex(i, &type))) {
      continue;
    }
    GUID subtype = {};
    UINT64 frameSize = 0;
    UINT64 frameRate = 0;
    if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
      continue;
    }
    YuvType yuvType = mfSubtypeToYuvType(subtype);
    // RGB-only等不支持的格式直接跳过, 全部跳过的设备不会进设备列表
    if (yuvType == YuvType::other) {
      continue;
    }
    if (FAILED(type->GetUINT64(MF_MT_FRAME_SIZE, &frameSize)) ||
        FAILED(type->GetUINT64(MF_MT_FRAME_RATE, &frameRate))) {
      continue;
    }
    MfCameraFormat format = {};
    format.mfIndex = i;
    format.desc.width = (int32_t)(frameSize >> 32);
    format.desc.height = (int32_t)(frameSize & 0xFFFFFFFF);
    UINT32 rateNum = (UINT32)(frameRate >> 32);
    UINT32 rateDen = (UINT32)(frameRate & 0xFFFFFFFF);
    format.desc.fps = rateDen > 0 ? (double)rateNum / rateDen : 30.0;
    format.desc.type = yuvType;
    format.subtype = subtype;
    formats.push_back(format);
  }
  return formats.size() > 0;
}

}  // namespace avox
