#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>

#include <string>
#include <vector>

#include "avox/AvoxVideo.h"
#include "avox_windows/WinCommon.hpp"

namespace avox {

// 相机一条输出格式, mfIndex保留在IMFMediaTypeHandler中的原始索引
struct MfCameraFormat {
  int32_t mfIndex = 0;
  VideoDesc desc = {};
  // 设备原始MF subtype(MJPG在枚举时已映射为yuv2I, subtype保留原始值供改写判断)
  GUID subtype = {};
};

// MF subtype GUID转YuvType, 不支持的格式返回other(与aoce一致直接丢弃)
YuvType mfSubtypeToYuvType(const GUID& subtype);
// 枚举source第一个视频流的所有可用媒体类型, handler供open时设置格式
bool getMfCameraFormats(IMFMediaSource* source,
                        std::vector<MfCameraFormat>& formats,
                        MComPtr<IMFMediaTypeHandler>& handler);

}  // namespace avox
