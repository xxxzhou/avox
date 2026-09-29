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

// tone map 峰值亮度选取(nits): DV L1 场景峰值优先(动态重映射), 退 MaxCLL,
// 退 mastering 峰值, 再退默认 1000
uint32_t hdrPeakNits(const HdrMeta& meta);

// RGBA<->YUV shader 的 UBO 布局(std140), 与 rgba2yuvV*/yuv2rgbaV*.comp 对齐
// transfer 占 3 个 int 后的槽位(旧 shader 视作隐式 padding, mat4 仍 offset 16);
// HDR 参数追加在 mat4 后(offset 80), 仅 yuv2rgbaV5.comp 声明, 旧 shader 块 80B 不受影响
// DV 整形区追加在 offset 96(vec4 展平规避 std140 数组 16B 步长浪费), 仅 V5 声明
struct DoviUboVec4 { float v[4] = {}; };
struct DoviUboIVec4 { int32_t v[4] = {}; };

struct ColorYuvUBO {
  int32_t width = 0;
  int32_t height = 0;
  int32_t yuvType = 0;
  int32_t transfer = 0;   // YuvTransfer 声明序: 0=gamma 1=linear 2=pq 3=hlg
  Mat4x4f colorMat = {};  // offset 16
  // --- V5 追加区: HDR tone map 参数 ---
  float maxLuminance = 1000.0f;  // 内容峰值亮度 nits
  float sdrWhiteNits = 100.0f;   // SDR 白点 nits
  int32_t hdrMode = 0;           // HdrMode 声明序: 0=follow 1=forceSDR 2=forceHDR
  int32_t _pad2 = {};
  // --- V5 追加区: DV 整形(offset 96) ---
  int32_t doviEnable = 0;        // 1=按 DV 区整形(doviEnable!=1 时全部走原 colorMat)
  int32_t dvPad[3] = {};
  DoviUboVec4 dvPivots[7];       // 3comp×9 pivot 归一化, flat=c*9+k
  DoviUboVec4 dvPoly[18];        // 3comp×8段×3系数, flat=(c*8+p)*3+k
  DoviUboVec4 dvMmr[132];        // 3comp×8段×(1const+3阶×7), flat=(c*8+p)*22+n
  DoviUboIVec4 dvIdc[6];         // 24段: 0=无效 1=poly 0x10+order=mmr
  DoviUboVec4 dvNumPivots;       // xyz=comp0/1/2 的 numPivots(钳位上下文)
  DoviUboVec4 dvNl[3];           // ycc_to_rgb 列主序(PQ 前)
  DoviUboVec4 dvNlOff;           // ycc_to_rgb_offset
  DoviUboVec4 dvLm[3];           // 固定 LMS2RGB × rgb_to_lms 预乘列主序(PQ 后)
};
static_assert(sizeof(ColorYuvUBO) == 2848, "UBO layout must match shader std140");

// DV 整形区打包(doviMeta → ubo offset 96 起的 DV 区): VK 层与 DX11 CS 共用
void packDoviUbo(ColorYuvUBO& ubo, const DoviMeta& meta);

}
