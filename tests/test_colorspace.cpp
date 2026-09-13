// ColorSpace 单元测试: 601/709/2020 矩阵已知系数逐点对拍 + 正反往返一致性
// BT.2020 为 HDR 前置(此前按 601 兜底), 重点防回归。
#include <doctest.h>

#include <cmath>
#include <cstddef>

#include "avox/video/ColorSpace.hpp"

namespace avox {
namespace {
// ColorSpace 约定: row j = 输出通道 j 的系数, 结果 = dot(row, (in0,in1,in2,1))
float applyRow(const Mat4x4f& m, int row, float a, float b, float c) {
  const vec4f& r = row == 0 ? m.row0 : (row == 1 ? m.row1 : m.row2);
  return r.x * a + r.y * b + r.z * c + r.w;
}
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("yuvToRgb: bt2020 已知系数 (白/红条)") {
  ColorSpaceDesc cs{};
  cs.standard = YuvStandard::bt2020;
  cs.range = YuvRange::full;
  Mat4x4f m = buildYuvToRgb(cs);
  // 白: Y=1, UV=0.5 -> (1,1,1)
  CHECK(near(applyRow(m, 0, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m, 1, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m, 2, 1.0f, 0.5f, 0.5f), 1.0f));
  // 红条: Y=Kr=0.2627, Cb=0.5-0.13963, Cr=1.0 -> (1,0,0)
  const float y = 0.2627f, cb = 0.5f - 0.13963f, cr = 1.0f;
  CHECK(near(applyRow(m, 0, y, cb, cr), 1.0f));
  CHECK(near(applyRow(m, 1, y, cb, cr), 0.0f, 1e-3f));
  CHECK(near(applyRow(m, 2, y, cb, cr), 0.0f, 1e-3f));
  // 蓝条: Y=Kb=0.0593, Cb=1.0, Cr=0.5-0.04021 -> (0,0,1)
  const float by = 0.0593f, bcb = 1.0f, bcr = 0.5f - 0.04021f;
  CHECK(near(applyRow(m, 0, by, bcb, bcr), 0.0f, 1e-3f));
  CHECK(near(applyRow(m, 1, by, bcb, bcr), 0.0f, 1e-3f));
  CHECK(near(applyRow(m, 2, by, bcb, bcr), 1.0f));
}

TEST_CASE("yuvToRgb: bt709/bt601 白点不回归") {
  ColorSpaceDesc cs{};
  cs.standard = YuvStandard::bt709;
  cs.range = YuvRange::full;
  Mat4x4f m709 = buildYuvToRgb(cs);
  CHECK(near(applyRow(m709, 0, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m709, 1, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m709, 2, 1.0f, 0.5f, 0.5f), 1.0f));
  cs.standard = YuvStandard::bt601;
  Mat4x4f m601 = buildYuvToRgb(cs);
  CHECK(near(applyRow(m601, 0, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m601, 1, 1.0f, 0.5f, 0.5f), 1.0f));
  CHECK(near(applyRow(m601, 2, 1.0f, 0.5f, 0.5f), 1.0f));
  // 601 红条: Y=0.299, Cb=0.33126, Cr=1.0
  CHECK(near(applyRow(m601, 0, 0.299f, 0.5f - 0.16874f, 1.0f), 1.0f));
  CHECK(near(applyRow(m601, 2, 0.299f, 0.5f - 0.16874f, 1.0f), 0.0f, 1e-3f));
}

TEST_CASE("rgbToYuv->yuvToRgb: 全制式 full/limited 往返一致") {
  const float colors[][3] = {
      {0.5f, 0.5f, 0.5f}, {0.7f, 0.2f, 0.4f}, {0.1f, 0.9f, 0.3f},
      {0.33f, 0.51f, 0.72f}, {0.85f, 0.4f, 0.05f}, {0.05f, 0.6f, 0.95f},
  };
  const YuvStandard standards[] = {YuvStandard::bt601, YuvStandard::bt709,
                                   YuvStandard::bt2020};
  // 系数按 4~5 位舍入, 往返误差放宽到 2e-3
  for (YuvStandard s : standards) {
    for (int r = 0; r < 2; ++r) {
      ColorSpaceDesc cs{};
      cs.standard = s;
      cs.range = r == 0 ? YuvRange::full : YuvRange::limited;
      Mat4x4f fwd = buildRgbToYuv(cs);
      Mat4x4f inv = buildYuvToRgb(cs);
      for (const auto& rgb : colors) {
        float y = applyRow(fwd, 0, rgb[0], rgb[1], rgb[2]);
        float u = applyRow(fwd, 1, rgb[0], rgb[1], rgb[2]);
        float v = applyRow(fwd, 2, rgb[0], rgb[1], rgb[2]);
        CHECK(near(applyRow(inv, 0, y, u, v), rgb[0], 2e-3f));
        CHECK(near(applyRow(inv, 1, y, u, v), rgb[1], 2e-3f));
        CHECK(near(applyRow(inv, 2, y, u, v), rgb[2], 2e-3f));
      }
    }
  }
}

TEST_CASE("UBO 布局: transfer 槽位与 96B 尺寸 (3a 定稿契约)") {
  // offset 12 的 transfer 不得挤动 colorMat(offset 16), 追加区 offset 80 起
  CHECK(sizeof(ColorYuvUBO) == 96);
  CHECK(offsetof(ColorYuvUBO, colorMat) == 16);
  CHECK(offsetof(ColorYuvUBO, maxLuminance) == 80);
  CHECK(offsetof(ColorYuvUBO, transfer) == 12);
  // 枚举序即 UBO int 值, shader 按 2=pq 3=hlg 分支
  CHECK((int32_t)YuvTransfer::gamma == 0);
  CHECK((int32_t)YuvTransfer::linear == 1);
  CHECK((int32_t)YuvTransfer::pq == 2);
  CHECK((int32_t)YuvTransfer::hlg == 3);
}
