#pragma once

#include <cstdint>

#include <cmath>

namespace avox {

// 字幕画布混合共享数学(字幕画布多后端渲染计划 §四): 参考白与 PQ 预编码
// 在此一处定义, 各后端(Metal/DX11/VK 16F 变体)不再各持一份。
// 画布 = SDR BT.709 gamma 域 rgba8(premultiplied); HDR·PQ 码域后端(DX11
// rgba10/Metal f16+PQ 标签)上传前按 LUT 预编码, GPU 混合代码与 SDR 完全同一条。

// BT.2408 惯例参考白: SDR 漫反射白在 PQ 域的锚点亮度(nits)。
// 单点定义勿复制(联动 HDR 方案「tone map sdrWhite 恒 100nit」挂账的修正锚)。
inline constexpr float kCanvasSdrRefWhiteNits = 203.f;

// BT.709 逆 OETF: canvas gamma 码值 [0,1] → 线性光 [0,1]
inline float canvasSdrToLinear(float v) {
  if (v <= 0.f) {
    return 0.f;
  }
  if (v >= 1.f) {
    return 1.f;
  }
  return v < 0.081f ? v / 4.5f : std::pow((v + 0.099f) / 1.099f, 1.f / 0.45f);
}

// ST 2084 PQ EOTF(编码向): 线性光(1.0 = 10000 nits) → PQ 码 [0,1]
inline float canvasPqEncode(float linear) {
  const float m1 = 0.1593017578125f;
  const float m2 = 78.84375f;
  const float c1 = 0.8359375f;
  const float c2 = 18.8515625f;
  const float c3 = 18.6875f;
  const float p = std::pow(linear, m1);
  return std::pow((c1 + c2 * p) / (1.f + c3 * p), m2);
}

// SDR 码值 → PQ 码 LUT(256 项): 索引 = 画布 premultiplied 通道码值,
// 输出 = PQ 码量化 8bit(SDR 白 255 → 203nits ≈ PQ 149)。alpha 通道不过 LUT。
inline void canvasPqEncodeLut(uint8_t lut[256]) {
  for (int32_t v = 0; v < 256; ++v) {
    const float linear = canvasSdrToLinear((float)v / 255.f);
    const float pq = canvasPqEncode(linear * kCanvasSdrRefWhiteNits / 10000.f);
    lut[v] = (uint8_t)(pq * 255.f + 0.5f);
  }
}

}
