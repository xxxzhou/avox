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

// RGBA<->YUV shader 的 UBO 布局(std140, 80B), 与 rgba2yuvV*/yuv2rgbaV*.comp 对齐
// mat4 在 3 个 int 后需 16B 对齐, _pad 占位使 C++ 偏移与 std140 一致
struct ColorYuvUBO {
  int32_t width = 0;
  int32_t height = 0;
  int32_t yuvType = 0;
  int32_t _pad = 0;
  Mat4x4f colorMat = {};  // offset 16
};
static_assert(sizeof(ColorYuvUBO) == 80, "UBO layout must match shader std140");

}
