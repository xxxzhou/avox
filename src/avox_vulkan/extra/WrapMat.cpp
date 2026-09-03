#include "WrapMat.hpp"

#include <algorithm>
#include <vector>

namespace avox {

#define RLUM (0.3f)
#define GLUM (0.59f)
#define BLUM (0.11f)

// 最小二乘法 https://www.it610.com/article/1278554388192837632.htm

// WrapMat getAffineTransform(const vec2f src[], const vec2f dst[]) {
//   WrapMat mat = {};
//   double a[36] = {};
//   double b[6] = {0};

//   // for (int i = 0; i < 3; i++) {
//   //     int j = i * 12;
//   //     int k = i * 12 + 6;
//   //     a[j] = a[k + 3] = src1[i].x;
//   //     a[j + 1] = a[k + 4] = src1[i].y;
//   //     a[j + 2] = a[k + 5] = 1;
//   //     a[j + 3] = a[j + 4] = a[j + 5] = 0;
//   //     a[k] = a[k + 1] = a[k + 2] = 0;
//   //     b[i * 2] = src2[i].x;
//   //     b[i * 2 + 1] = src2[i].y;
//   // }

//   return mat;
// }

Mat4x4f saturateMat(const Mat4x4f& mat, const float& saturate) {
  vec3f lum = {RLUM, GLUM, BLUM};
  float sat = 1.0f - saturate;
  vec4f vecSat = {sat, sat, sat, 0};
  Mat4x4f temp = {};
  temp.row0.x = vecSat.x * lum.x; temp.row0.y = vecSat.y * lum.x; temp.row0.z = vecSat.z * lum.x; temp.row0.w = 0;
  temp.row1.x = vecSat.x * lum.y; temp.row1.y = vecSat.y * lum.y; temp.row1.z = vecSat.z * lum.y; temp.row1.w = 0;
  temp.row2.x = vecSat.x * lum.z; temp.row2.y = vecSat.y * lum.z; temp.row2.z = vecSat.z * lum.z; temp.row2.w = 0;
  temp.row3 = {0.0f, 0.0f, 0.0f, 1.0f};
  temp.row0.x += saturate;
  temp.row1.y += saturate;
  temp.row2.z += saturate;
  return mat.multiply(temp);
}

Mat4x4f zshearMat(const Mat4x4f& mat, const float& dx, const float& dy) {
  Mat4x4f temp = identMat4x4f();
  temp.row0.z = dx;
  temp.row1.z = dy;
  return mat.multiply(temp);
}

Mat4x4f huerotateMat(const Mat4x4f& mat, const float& rot) {
  Mat4x4f rotx = makeMat4x4f(makeRotatorXf(45));
  Mat4x4f roty = makeMat4x4f(makeRotatorYf(120));
  Mat4x4f rotxb = makeMat4x4f(makeRotatorXf(-45));
  Mat4x4f rotyb = makeMat4x4f(makeRotatorYf(-120));
  Mat4x4f rotz = makeMat4x4f(makeRotatorZf(rot));
  vec3f rgb = {RLUM, GLUM, BLUM};
  Mat4x4f temp = rotx.multiply(roty);
  vec3f luminance = rgb.transform4(temp);
  float zsx = luminance.x / luminance.z;
  float zsy = luminance.y / luminance.z;
  temp = zshearMat(temp, zsx, zsy);
  temp = temp.multiply(rotz);
  /* unshear the space to put the luminance plane back */
  temp = zshearMat(temp, -zsx, -zsy);
  /* rotate the grey vector back into place */
  temp = temp.multiply(rotyb).multiply(rotxb);
  return temp;
}

bool bRightHanded(AxisType xAxis, AxisType yAxis, AxisType zAxis) {
  const vec3f xvec = axisVecf(xAxis);
  const vec3f yvec = axisVecf(yAxis);
  const vec3f zvec = axisVecf(zAxis);
  vec3f temp = xvec.cross(yvec);
  return temp.equals(zvec);
}

}
