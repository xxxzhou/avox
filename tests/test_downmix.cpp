// 播放下混系数/对白增强单元测试: 参数面契约 + 预设→线性系数解析
//
// 解析式 dB→线性 = 10^(dB/20); 与音频库(swresample)的换算一致(其 -3dB 常量取
// M_SQRT1_2=0.70711, 本式 0.70795, 差 0.01dB; none 档不干预才能逐位等价历史行为)。
// 实际下混矩阵由 swresample 按线性系数构建, 端到端 RMS 走查见 samples 探针。
#include <doctest.h>

#include <cmath>

#include "audio/DownmixLevel.hpp"

using namespace avox;

static bool nearEq(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}

TEST_CASE("downmix: 枚举与默认参数契约") {
  // 声明序即下滑混链路的取值(写进探针/宿主的整数约定)
  CHECK((int32_t)DownmixPreset::none == 0);
  CHECK((int32_t)DownmixPreset::itu == 1);
  CHECK((int32_t)DownmixPreset::dolby == 2);
  CHECK((int32_t)DownmixPreset::custom == 3);
  DownmixParamet d;
  CHECK(d.preset == DownmixPreset::none);
  CHECK(nearEq(d.centerGainDb, -3.0f));
  CHECK(nearEq(d.surroundGainDb, -3.0f));
  CHECK(nearEq(d.lfeGainDb, 0.0f));
  CHECK(nearEq(d.volume, 1.0f));
  // 判等按字段全比(热切判据: 相等不重建 swr)
  DownmixParamet a, b;
  CHECK(a == b);
  b.centerGainDb = 0.0f;
  CHECK(a != b);
  a.centerGainDb = 0.0f;
  CHECK(a == b);
}

TEST_CASE("downmix: none 档不干预(历史行为零变化)") {
  float c = 0.0f, s = 0.0f, l = 0.0f, v = 0.0f;
  DownmixParamet d;  // 默认 none
  CHECK(resolveDownmixLevels(d, c, s, l, v) == false);
  // 即使显式填了增益, none 也不生效(避免"设了没反应"以外的歧义: 必须切 custom)
  d.centerGainDb = 3.0f;
  CHECK(resolveDownmixLevels(d, c, s, l, v) == false);
}

TEST_CASE("downmix: ITU 预设(LFE 丢弃)") {
  float c = 0.0f, s = 0.0f, l = 0.0f, v = 1.0f;
  DownmixParamet d;
  d.preset = DownmixPreset::itu;
  CHECK(resolveDownmixLevels(d, c, s, l, v) == true);
  CHECK(nearEq(c, 0.70795f));   // -3dB
  CHECK(nearEq(s, 0.70795f));
  CHECK(nearEq(l, 0.0f));       // LFE 丢弃
  CHECK(nearEq(v, 1.0f));
}

TEST_CASE("downmix: Dolby 预设保留 LFE") {
  float c = 0.0f, s = 0.0f, l = 0.0f, v = 1.0f;
  DownmixParamet d;
  d.preset = DownmixPreset::dolby;
  CHECK(resolveDownmixLevels(d, c, s, l, v) == true);
  CHECK(nearEq(c, 0.70795f));
  CHECK(nearEq(s, 0.70795f));
  CHECK(nearEq(l, 0.70795f));   // LFE -3dB(保留)
}

TEST_CASE("downmix: custom 对白增强 = 中置抬升 3dB") {
  float c = 0.0f, s = 0.0f, l = 0.0f, v = 1.0f;
  DownmixParamet d;
  d.preset = DownmixPreset::custom;
  d.centerGainDb = 0.0f;   // 硬件/库默认口径是 -3dB
  CHECK(resolveDownmixLevels(d, c, s, l, v) == true);
  CHECK(nearEq(c, 1.0f));
  // 对白增强量 = 与默认 -3dB 之比
  CHECK(nearEq(c / 0.70795f, 1.41254f, 1e-3f));  // +3dB
}

TEST_CASE("downmix: custom 负值/丢弃/音量钳位") {
  float c = 0.0f, s = 0.0f, l = 0.0f, v = 0.0f;
  DownmixParamet d;
  d.preset = DownmixPreset::custom;
  d.centerGainDb = -6.0f;
  d.surroundGainDb = -60.0f;  // <= -60 视为丢弃
  d.lfeGainDb = -60.0f;
  d.volume = -1.0f;           // 非法音量钳到 0
  CHECK(resolveDownmixLevels(d, c, s, l, v) == true);
  CHECK(nearEq(c, 0.50119f, 1e-3f));  // -6dB
  CHECK(nearEq(s, 0.0f));
  CHECK(nearEq(l, 0.0f));
  CHECK(nearEq(v, 0.0f));
}
