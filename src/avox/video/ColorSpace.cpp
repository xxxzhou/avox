#include "ColorSpace.hpp"

namespace avox {

// row{i} 在内存上等于 GLSL col{i}(行优先 memcpy→列优先), 配合 shader vec*M: col{j}=输出通道j的系数
// 即 row{j} = 输出通道 j 的系数向量 (各输入分量对该输出的贡献)
namespace {
// full-range 正向矩阵
Mat4x4f rgbToYuvFull(YuvStandard s) {
  Mat4x4f m = {};
  if (s == YuvStandard::bt709) {
    // Y = 0.2126R + 0.7152G + 0.0722B ; Cb = -0.11457R - 0.38543G + 0.5B + 0.5 ; Cr = 0.5R - 0.45415G - 0.04585B + 0.5
    m.row0 = vec4f(0.2126f, 0.7152f, 0.0722f, 0.0f);          // Y: 各输入对 Y 的贡献
    m.row1 = vec4f(-0.11457f, -0.38543f, 0.5f, 0.5f);         // Cb: 各输入对 Cb 的贡献 (UV 居中 +0.5)
    m.row2 = vec4f(0.5f, -0.45415f, -0.04585f, 0.5f);         // Cr: 各输入对 Cr 的贡献 (UV 居中 +0.5)
    m.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  } else {
    // bt601(默认, bt2020 暂按 601 兜底, 预留)
    // Y = 0.299R + 0.587G + 0.114B ; Cb = -0.16874R - 0.33126G + 0.5B + 0.5 ; Cr = 0.5R - 0.41869G - 0.08131B + 0.5
    m.row0 = vec4f(0.299f, 0.587f, 0.114f, 0.0f);             // Y: 各输入对 Y 的贡献
    m.row1 = vec4f(-0.16874f, -0.33126f, 0.5f, 0.5f);         // Cb: 各输入对 Cb 的贡献 (UV 居中 +0.5)
    m.row2 = vec4f(0.5f, -0.41869f, -0.08131f, 0.5f);         // Cr: 各输入对 Cr 的贡献 (UV 居中 +0.5)
    m.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }
  return m;
}

// full-range 反向矩阵, UV 的 -0.5 已折进 row3 偏移列
Mat4x4f yuvToRgbFull(YuvStandard s) {
  Mat4x4f m = {};
  if (s == YuvStandard::bt709) {
    // R=Y+1.5748Cr ; G=Y-0.18733Cb-0.46812Cr ; B=Y+1.8556Cb ; 偏移: R-0.7874, G+0.32773, B-0.9278
    m.row0 = vec4f(1.0f, 0.0f, 1.5748f, -0.7874f);            // R: Y*1 + U*0 + V*1.5748 + (-0.5*1.5748)
    m.row1 = vec4f(1.0f, -0.18733f, -0.46812f, 0.32773f);     // G: Y*1 + U*(-0.18733) + V*(-0.46812) + ...
    m.row2 = vec4f(1.0f, 1.8556f, 0.0f, -0.9278f);            // B: Y*1 + U*1.8556 + V*0 + (-0.5*1.8556)
    m.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  } else {
    // bt601: R=Y+1.402Cr ; G=Y-0.34414Cb-0.71414Cr ; B=Y+1.772Cb ; 偏移: R-0.701, G+0.52914, B-0.886
    m.row0 = vec4f(1.0f, 0.0f, 1.402f, -0.701f);              // R: Y*1 + U*0 + V*1.402 + (-0.5*1.402)
    m.row1 = vec4f(1.0f, -0.34414f, -0.71414f, 0.52914f);     // G: Y*1 + U*(-0.34414) + V*(-0.71414) + ...
    m.row2 = vec4f(1.0f, 1.772f, 0.0f, -0.886f);              // B: Y*1 + U*1.772 + V*0 + (-0.5*1.772)
    m.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }
  return m;
}

// limited: 在 full 基础上, Y 输出 *= 219/255 + 16/255; UV 输出 *= 224/255 + 16/255
// 新约定: row j = 输出通道 j, 缩放整行并偏移写进 w 分量
void applyLimitedForward(Mat4x4f& m) {
  const float yK = 219.0f / 255.0f;
  const float cK = 224.0f / 255.0f;
  const float yOff = 16.0f / 255.0f;
  // row0 = Y 输出, Y_limited = Y_full * 219/255 + 16/255
  m.row0.x *= yK;  m.row0.y *= yK;  m.row0.z *= yK;  m.row0.w *= yK;  m.row0.w += yOff;
  // row1 = Cb 输出, Cb_limited = Cb_full * 224/255 + 16/255
  m.row1.x *= cK;  m.row1.y *= cK;  m.row1.z *= cK;  m.row1.w *= cK;  m.row1.w += yOff;
  // row2 = Cr 输出, Cr_limited = Cr_full * 224/255 + 16/255
  m.row2.x *= cK;  m.row2.y *= cK;  m.row2.z *= cK;  m.row2.w *= cK;  m.row2.w += yOff;
}

// limited 反向: 输入先做 limited->full (Y*(255/219)-16/219, UV*(255/224)-16/224), 再 full 反向
// in' = in * d, 结果 = d.multiply(mFull) (行向量约定)
Mat4x4f applyLimitedInverse(const Mat4x4f& mFull) {
  const float yK = 255.0f / 219.0f;
  const float cK = 255.0f / 224.0f;
  Mat4x4f d = {};
  d.row0 = vec4f(yK, 0.0f, 0.0f, -16.0f / 219.0f);
  d.row1 = vec4f(0.0f, cK, 0.0f, -16.0f / 224.0f);
  d.row2 = vec4f(0.0f, 0.0f, cK, -16.0f / 224.0f);
  d.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  return d.multiply(mFull);
}
}  // namespace

Mat4x4f buildRgbToYuv(const ColorSpaceDesc& cs) {
  Mat4x4f m = rgbToYuvFull(cs.standard);
  if (cs.range == YuvRange::limited) {
    applyLimitedForward(m);
  }
  return m;
}

Mat4x4f buildYuvToRgb(const ColorSpaceDesc& cs) {
  Mat4x4f m = yuvToRgbFull(cs.standard);
  if (cs.range == YuvRange::limited) {
    m = applyLimitedInverse(m);
  }
  return m;
}

}
