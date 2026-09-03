#pragma once

#include "avox/AvoxMath.h"

namespace avox {

// 图片UV间的仿射变化
struct WrapMat {
  vec3f uvec = {};
  vec3f vvec = {};
};
// 求解是否右手坐标系
bool bRightHanded(AxisType xAxis, AxisType yAxis, AxisType zAxis);
Mat4x4f saturateMat(const Mat4x4f &mat, const float &saturate);
Mat4x4f zshearMat(const Mat4x4f &mat, const float &dx, const float &dy);
Mat4x4f huerotateMat(const Mat4x4f &mat, const float &rot);
// ACOE_EXPORT WrapMat getAffineTransform(const vec2f src[], const vec2f dst[]);

}