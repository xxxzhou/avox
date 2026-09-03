#pragma once

#include <vector>

#include "avox/AvoxMath.h"

namespace avox {

// 图元类型
enum class GeoType { point = 0, line = 1, rect = 2, circle = 3 };

// 单个图元（内部用，CPU 光栅化到 canvas）
//   point  : a=center, radius=半径(帧px)
//   line   : a=p0, b=p1
//   rect   : a=topleft, b=bottomright, fill
//   circle : a=center, radius=半径(帧px), fill
struct GeoShape {
  GeoType type = GeoType::point;
  vec2f a;
  vec2f b;
  float radius = 0.f;
  bool fill = false;
};

// render 线程快照（含图元 + 全局颜色/阈值）
struct GeoSnapshot {
  std::vector<GeoShape> shapes;
  float r = 1.f;
  float g = 1.f;
  float b = 1.f;
  float threshold = 0.5f;
};

}
