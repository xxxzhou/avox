#pragma once

#include "../AvoxMath.h"
#include "../AvoxVideo.h"

namespace avox {

// 构造归一化 [0,1] 的 4x4 仿射矩阵(平移在第4列)。
// 约定(对齐 colorMatrix.comp): shader 以 vec*M(向量在左)使用, C++ 直传不转置。
// 正向: vec4(R,G,B,1) * M -> (Y,U,V,_), 已含 range 缩放与 UV 居中偏移
Mat4x4f buildRgbToYuv(const ColorSpaceDesc& cs);
// 反向: vec4(Y,U,V,1) * M -> (R,G,B,_), UV 的 -0.5 已折叠进偏移列, shader 无需再减
Mat4x4f buildYuvToRgb(const ColorSpaceDesc& cs);

// RGBA<->YUV shader 的 UBO 布局(std140, 96B), 与 rgba2yuvV*/yuv2rgbaV*.comp 对齐
// transfer 占 3 个 int 后的槽位(旧 shader 视作隐式 padding, mat4 仍 offset 16);
// HDR 参数追加在 mat4 后(offset 80), 仅 yuv2rgbaV5.comp 声明, 旧 shader 块 80B 不受影响
struct ColorYuvUBO {
  int32_t width = 0;
  int32_t height = 0;
  int32_t yuvType = 0;
  int32_t transfer = 0;   // YuvTransfer 声明序: 0=gamma 1=linear 2=pq 3=hlg
  Mat4x4f colorMat = {};  // offset 16
  // --- V5 追加区: HDR tone map 参数 ---
  float maxLuminance = 1000.0f;  // 内容峰值亮度 nits
  float sdrWhiteNits = 100.0f;   // SDR 白点 nits
  int32_t _pad2[2] = {};
};
static_assert(sizeof(ColorYuvUBO) == 96, "UBO layout must match shader std140");

}
