#include "../AvoxMath.h"

#include <assert.h>
#include <cmath>
#include <utility>

namespace avox {

// ============================================================================
// 内部模板实现
// ============================================================================
namespace detail {

template <typename T>
inline void sincos(T angle, T& sinv, T& cosv) {
  sinv = std::sin(angle * static_cast<T>(angle_radian));
  cosv = std::cos(angle * static_cast<T>(angle_radian));
}

template <typename T>
struct vec3_t {
  T x, y, z;

  vec3_t cross(const vec3_t& v) const {
    return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
  }

  T dot(const vec3_t& v) const { return x * v.x + y * v.y + z * v.z; }

  T lenght(const vec3_t& v) const {
    T dx = x - v.x, dy = y - v.y, dz = z - v.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  void scale(T s) {
    x *= s;
    y *= s;
    z *= s;
  }

  T normalize() {
    T len = std::sqrt(x * x + y * y + z * z);
    if (len > T(0)) {
      T inv = T(1) / len;
      x *= inv;
      y *= inv;
      z *= inv;
    }
    return len;
  }

  bool zero() const { return x == T(0) && y == T(0) && z == T(0); }
};

template <typename T>
struct vec4_t {
  T x, y, z, w;

  T normalize() {
    T len = std::sqrt(x * x + y * y + z * z);
    if (len > T(0)) {
      T inv = T(1) / len;
      x *= inv;
      y *= inv;
      z *= inv;
    }
    return len;
  }

  void scale(T s) {
    x *= s;
    y *= s;
    z *= s;
  }
};

// 矩阵元素访问辅助
template <typename Mat>
inline typename Mat::value_type& matAt(Mat& m, int row, int col) {
  return reinterpret_cast<typename Mat::value_type*>(&m)[row * Mat::cols + col];
}

template <typename Mat>
inline const typename Mat::value_type& matAt(const Mat& m, int row, int col) {
  return reinterpret_cast<const typename Mat::value_type*>(&m)[row * Mat::cols + col];
}

// 3x3 矩阵逆
template <typename T>
bool mat3x3Inverse(const T* m, T* result) {
  T c00 = m[4] * m[8] - m[5] * m[7];
  T c10 = m[5] * m[6] - m[3] * m[8];
  T c20 = m[3] * m[7] - m[4] * m[6];
  T det = m[0] * c00 + m[1] * c10 + m[2] * c20;
  if (det == T(0)) return false;
  T invDet = T(1) / det;
  result[0] = c00 * invDet;
  result[1] = (m[2] * m[7] - m[1] * m[8]) * invDet;
  result[2] = (m[1] * m[5] - m[2] * m[4]) * invDet;
  result[3] = c10 * invDet;
  result[4] = (m[0] * m[8] - m[2] * m[6]) * invDet;
  result[5] = (m[2] * m[3] - m[0] * m[5]) * invDet;
  result[6] = c20 * invDet;
  result[7] = (m[1] * m[6] - m[0] * m[7]) * invDet;
  result[8] = (m[0] * m[4] - m[1] * m[3]) * invDet;
  return true;
}

// 4x4 矩阵逆
template <typename T>
bool mat4x4Inverse(const T* M, T* result) {
  T a0 = M[0] * M[5] - M[1] * M[4];
  T a1 = M[0] * M[9] - M[2] * M[4];
  T a2 = M[0] * M[13] - M[3] * M[4];
  T a3 = M[1] * M[9] - M[2] * M[5];
  T a4 = M[1] * M[13] - M[3] * M[5];
  T a5 = M[2] * M[13] - M[3] * M[9];
  T b0 = M[8] * M[13] - M[9] * M[12];
  T b1 = M[8] * M[14] - M[10] * M[12];
  T b2 = M[8] * M[15] - M[11] * M[12];
  T b3 = M[9] * M[14] - M[10] * M[13];
  T b4 = M[9] * M[15] - M[11] * M[13];
  T b5 = M[10] * M[15] - M[11] * M[14];
  T det = a0 * b5 - a1 * b4 + a2 * b3 + a3 * b2 - a4 * b1 + a5 * b0;
  if (det == T(0)) return false;
  T invDet = T(1) / det;
  result[0] = (M[5] * b5 - M[9] * b4 + M[13] * b3) * invDet;
  result[1] = (-M[1] * b5 + M[2] * b4 - M[3] * b3) * invDet;
  result[2] = (M[13] * a5 - M[14] * a4 + M[15] * a3) * invDet;
  result[3] = (-M[9] * a5 + M[10] * a4 - M[11] * a3) * invDet;
  result[4] = (-M[4] * b5 + M[8] * b2 - M[12] * b1) * invDet;
  result[5] = (M[0] * b5 - M[2] * b2 + M[3] * b1) * invDet;
  result[6] = (-M[12] * a5 + M[14] * a2 - M[15] * a1) * invDet;
  result[7] = (M[8] * a5 - M[10] * a2 + M[11] * a1) * invDet;
  result[8] = (M[4] * b4 - M[5] * b2 + M[12] * b0) * invDet;
  result[9] = (-M[0] * b4 + M[1] * b2 - M[3] * b0) * invDet;
  result[10] = (M[12] * a4 - M[13] * a2 + M[15] * a0) * invDet;
  result[11] = (-M[8] * a4 + M[9] * a2 - M[11] * a0) * invDet;
  result[12] = (-M[4] * b3 + M[5] * b1 - M[8] * b0) * invDet;
  result[13] = (M[0] * b3 - M[1] * b1 + M[2] * b0) * invDet;
  result[14] = (-M[12] * a3 + M[13] * a1 - M[14] * a0) * invDet;
  result[15] = (M[8] * a3 - M[9] * a1 + M[10] * a0) * invDet;
  return true;
}

// 四元数转旋转矩阵
template <typename T>
void quaternion2Mat3x3(const T* quat, T* result) {
  T twoX = T(2) * quat[0];
  T twoY = T(2) * quat[1];
  T twoZ = T(2) * quat[2];
  T twoXX = twoX * quat[0];
  T twoXY = twoX * quat[1];
  T twoXZ = twoX * quat[2];
  T twoXW = twoX * quat[3];
  T twoYY = twoY * quat[1];
  T twoYZ = twoY * quat[2];
  T twoYW = twoY * quat[3];
  T twoZZ = twoZ * quat[2];
  T twoZW = twoZ * quat[3];

  result[0] = T(1) - twoYY - twoZZ;
  result[3] = twoXY - twoZW;
  result[6] = twoXZ + twoYW;
  result[1] = twoXY + twoZW;
  result[4] = T(1) - twoXX - twoZZ;
  result[7] = twoYZ - twoXW;
  result[2] = twoXZ - twoYW;
  result[5] = twoYZ + twoXW;
  result[8] = T(1) - twoXX - twoYY;
}

// 旋转矩阵转四元数
template <typename T>
void mat3x32Quaternion(const T* r, T* q) {
  T r22 = r[8];
  if (r22 <= T(0)) {
    T dif10 = r[4] - r[0];
    T omr22 = T(1) - r22;
    if (dif10 <= T(0)) {
      T fourXSqr = omr22 - dif10;
      T inv4x = T(0.5) / std::sqrt(fourXSqr);
      q[0] = fourXSqr * inv4x;
      q[1] = (r[1] + r[3]) * inv4x;
      q[2] = (r[2] + r[6]) * inv4x;
      q[3] = (r[5] - r[7]) * inv4x;
    } else {
      T fourYSqr = omr22 + dif10;
      T inv4y = T(0.5) / std::sqrt(fourYSqr);
      q[0] = (r[1] + r[3]) * inv4y;
      q[1] = fourYSqr * inv4y;
      q[2] = (r[5] + r[7]) * inv4y;
      q[3] = (r[6] - r[2]) * inv4y;
    }
  } else {
    T sum10 = r[4] + r[0];
    T opr22 = T(1) + r22;
    if (sum10 <= T(0)) {
      T fourZSqr = opr22 - sum10;
      T inv4z = T(0.5) / std::sqrt(fourZSqr);
      q[0] = (r[2] + r[6]) * inv4z;
      q[1] = (r[5] + r[7]) * inv4z;
      q[2] = fourZSqr * inv4z;
      q[3] = (r[1] - r[3]) * inv4z;
    } else {
      T fourWSqr = opr22 + sum10;
      T inv4w = T(0.5) / std::sqrt(fourWSqr);
      q[0] = (r[5] - r[7]) * inv4w;
      q[1] = (r[6] - r[2]) * inv4w;
      q[2] = (r[1] - r[3]) * inv4w;
      q[3] = fourWSqr * inv4w;
    }
  }
}

// 轴角转旋转矩阵
template <typename T>
void axisAngle2Mat3x3(const T* a, T* result) {
  T cs = std::cos(a[3] * static_cast<T>(angle_radian));
  T sn = std::sin(a[3] * static_cast<T>(angle_radian));
  T oneMinusCos = T(1) - cs;
  T x0sqr = a[0] * a[0];
  T x1sqr = a[1] * a[1];
  T x2sqr = a[2] * a[2];
  T x0x1m = a[0] * a[1] * oneMinusCos;
  T x0x2m = a[0] * a[2] * oneMinusCos;
  T x1x2m = a[1] * a[2] * oneMinusCos;
  T x0Sin = a[0] * sn;
  T x1Sin = a[1] * sn;
  T x2Sin = a[2] * sn;

  result[0] = x0sqr * oneMinusCos + cs;
  result[3] = x0x1m - x2Sin;
  result[6] = x0x2m + x1Sin;
  result[1] = x0x1m + x2Sin;
  result[4] = x1sqr * oneMinusCos + cs;
  result[7] = x1x2m - x0Sin;
  result[2] = x0x2m - x1Sin;
  result[5] = x1x2m + x0Sin;
  result[8] = x2sqr * oneMinusCos + cs;
}

// 旋转矩阵转轴角
template <typename T>
void mat3x32AxisAngle(const T* r, T* result) {
  T trace = r[0] + r[4] + r[8];
  T cs = T(0.5) * (trace - T(1));
  cs = std::fmax(std::fmin(cs, T(1)), T(-1));
  T angle = std::acos(cs);
  T axis[3] = {T(0), T(0), T(0)};
  if (angle > T(0)) {
    if (angle < static_cast<T>(M_PI)) {
      axis[0] = r[5] - r[7];
      axis[1] = r[6] - r[2];
      axis[2] = r[1] - r[3];
      T len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
      if (len > T(0)) {
        T inv = T(1) / len;
        axis[0] *= inv;
        axis[1] *= inv;
        axis[2] *= inv;
      }
    } else {
      T one = T(1);
      if (r[0] >= r[4]) {
        if (r[0] >= r[8]) {
          axis[0] = r[0] + one;
          axis[1] = T(0.5) * (r[1] + r[3]);
          axis[2] = T(0.5) * (r[2] + r[6]);
        } else {
          axis[0] = T(0.5) * (r[6] + r[2]);
          axis[1] = T(0.5) * (r[7] + r[5]);
          axis[2] = r[8] + one;
        }
      } else {
        if (r[4] >= r[8]) {
          axis[0] = T(0.5) * (r[3] + r[1]);
          axis[1] = r[4] + one;
          axis[2] = T(0.5) * (r[5] + r[7]);
        } else {
          axis[0] = T(0.5) * (r[6] + r[2]);
          axis[1] = T(0.5) * (r[7] + r[5]);
          axis[2] = r[8] + one;
        }
      }
      T len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
      if (len > T(0)) {
        T inv = T(1) / len;
        axis[0] *= inv;
        axis[1] *= inv;
        axis[2] *= inv;
      }
    }
  } else {
    axis[0] = T(1);
  }
  result[0] = axis[0];
  result[1] = axis[1];
  result[2] = axis[2];
  result[3] = angle * static_cast<T>(radina_angle);
}

// 四元数转轴角
template <typename T>
void quaternion2AxisAngle(const T* q, T* result) {
  T axisSqrLen = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
  if (axisSqrLen > T(0)) {
    T adjust = T(1) / std::sqrt(axisSqrLen);
    result[0] = q[0] * adjust;
    result[1] = q[1] * adjust;
    result[2] = q[2] * adjust;
    T cs = std::fmax(std::fmin(q[3], T(1)), T(-1));
    result[3] = T(2) * std::acos(cs) * static_cast<T>(radina_angle);
  } else {
    result[0] = T(1);
    result[1] = T(0);
    result[2] = T(0);
    result[3] = T(0);
  }
}

// 轴角转四元数
template <typename T>
void axisAngle2Quaternion(const T* a, T* quat) {
  T halfAngle = a[3] * static_cast<T>(angle_radian) * T(0.5);
  T sn = std::sin(halfAngle);
  quat[0] = sn * a[0];
  quat[1] = sn * a[1];
  quat[2] = sn * a[2];
  quat[3] = std::cos(halfAngle);
}

// 欧拉角转旋转矩阵
template <typename T>
void eulerAngle2Mat3x3(const T* angle, int one, int two, int three, T* result) {
  // 轴向量
  static const T axisVectors[6][3] = {
      {T(1), T(0), T(0)},   // X
      {T(0), T(1), T(0)},   // Y
      {T(0), T(0), T(1)},   // Z
      {T(-1), T(0), T(0)},  // -X
      {T(0), T(-1), T(0)},  // -Y
      {T(0), T(0), T(-1)}   // -Z
  };

  T temp[3][9] = {};  // 3个3x3矩阵

  // 为每个轴创建旋转矩阵
  for (int i = 0; i < 3; i++) {
    int axis = (i == 0) ? one : ((i == 1) ? two : three);
    T ang = angle[i];
    T rs = T(0), rc = T(0);
    sincos(ang, rs, rc);

    // 单位矩阵
    temp[i][0] = T(1); temp[i][4] = T(1); temp[i][8] = T(1);

    // 根据轴设置旋转
    if (axis == 0) {  // X
      temp[i][4] = rc; temp[i][5] = rs; temp[i][7] = -rs; temp[i][8] = rc;
    } else if (axis == 1) {  // Y
      temp[i][0] = rc; temp[i][2] = -rs; temp[i][6] = rs; temp[i][8] = rc;
    } else if (axis == 2) {  // Z
      temp[i][0] = rc; temp[i][1] = rs; temp[i][3] = -rs; temp[i][4] = rc;
    }
  }

  // 矩阵乘法 result = temp[0] * temp[1] * temp[2]
  T temp2[9];
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      temp2[i * 3 + j] = temp[0][i * 3 + 0] * temp[1][0 * 3 + j] +
                         temp[0][i * 3 + 1] * temp[1][1 * 3 + j] +
                         temp[0][i * 3 + 2] * temp[1][2 * 3 + j];
    }
  }
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      result[i * 3 + j] = temp2[i * 3 + 0] * temp[2][0 * 3 + j] +
                          temp2[i * 3 + 1] * temp[2][1 * 3 + j] +
                          temp2[i * 3 + 2] * temp[2][2 * 3 + j];
    }
  }
}

// 旋转矩阵转欧拉角
template <typename T>
void mat3x32EulerAngle(const T* r, int one, int two, int three, T* angle) {
  int axis0 = one;
  int axis1 = two;
  int axis2 = three;

  if (axis0 != axis2) {
    int parity = (((axis0 | (axis1 << 2)) >> axis2) & 1);
    T sgn = (parity & 1) ? T(1) : T(-1);
    T r02 = r[2];
    if (r02 < T(1)) {
      if (r02 > T(-1)) {
        angle[axis0] = std::atan2(sgn * r[5], r[8]);
        angle[axis1] = std::asin(-sgn * r02);
        angle[axis2] = std::atan2(sgn * r[1], r[0]);
      } else {
        angle[axis0] = T(0);
        angle[axis1] = sgn * static_cast<T>(M_PI / 2.0);
        angle[axis2] = std::atan2(-sgn * r[3], r[4]);
      }
    } else {
      angle[axis0] = T(0);
      angle[axis1] = -sgn * static_cast<T>(M_PI / 2.0);
      angle[axis2] = std::atan2(-sgn * r[3], r[4]);
    }
  } else {
    int b2 = 3 - axis0 - axis1;
    int parity = (((b2 | (axis1 << 2)) >> axis0) & 1);
    T sgn = (parity & 1) ? T(-1) : T(1);
    T r00 = r[0];
    if (r00 < T(1)) {
      if (r00 > T(-1)) {
        angle[0] = std::atan2(r[3], sgn * r[6]);
        angle[1] = std::acos(r00);
        angle[2] = std::atan2(r[1], -sgn * r[2]);
      } else {
        angle[0] = T(0);
        angle[1] = static_cast<T>(M_PI);
        angle[2] = std::atan2(sgn * r[5], r[4]);
      }
    } else {
      angle[0] = T(0);
      angle[1] = T(0);
      angle[2] = std::atan2(sgn * r[5], r[4]);
    }
  }

  angle[0] *= static_cast<T>(radina_angle);
  angle[1] *= static_cast<T>(radina_angle);
  angle[2] *= static_cast<T>(radina_angle);
}

// 四元数插值
template <typename T>
void slerpQuaternion(T t, const T* a, const T* b, T* result) {
  T cosA = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  T sign = T(1);
  if (cosA < T(0)) {
    cosA = -cosA;
    sign = T(-1);
  }
  T f0 = T(1) - t;
  T f1 = t;
  if (cosA < T(1)) {
    T A = std::acos(cosA);
    T invA = T(1) / std::sin(A);
    f0 = std::sin((T(1) - t) * A) * invA;
    f1 = std::sin(t * A) * invA;
  }
  result[0] = a[0] * f0 + b[0] * (sign * f1);
  result[1] = a[1] * f0 + b[1] * (sign * f1);
  result[2] = a[2] * f0 + b[2] * (sign * f1);
  result[3] = a[3] * f0 + b[3] * (sign * f1);
}

// 坐标系转换
template <typename T>
void convertCoordinate(const T* transform, int dstX, int dstY, int dstZ, T* result) {
  // 轴向量
  static const T axisVectors[6][4] = {
      {T(1), T(0), T(0), T(0)},   // X
      {T(0), T(1), T(0), T(0)},   // Y
      {T(0), T(0), T(1), T(0)},   // Z
      {T(-1), T(0), T(0), T(0)},  // -X
      {T(0), T(-1), T(0), T(0)},  // -Y
      {T(0), T(0), T(-1), T(0)}   // -Z
  };

  // 构建转换矩阵
  T conv[16] = {};
  for (int i = 0; i < 3; i++) {
    int axis = (i == 0) ? dstX : ((i == 1) ? dstY : dstZ);
    conv[i * 4 + 0] = axisVectors[axis][0];
    conv[i * 4 + 1] = axisVectors[axis][1];
    conv[i * 4 + 2] = axisVectors[axis][2];
    conv[i * 4 + 3] = T(0);
  }
  conv[15] = T(1);

  // 转置
  T convT[16];
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      convT[i * 4 + j] = conv[j * 4 + i];
    }
  }

  // temp = conv * transform
  T temp[16] = {};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      for (int k = 0; k < 4; k++) {
        temp[i * 4 + j] += conv[i * 4 + k] * transform[k * 4 + j];
      }
    }
  }

  // result = temp * convT
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      result[i * 4 + j] = T(0);
      for (int k = 0; k < 4; k++) {
        result[i * 4 + j] += temp[i * 4 + k] * convT[k * 4 + j];
      }
    }
  }
}

}  // namespace detail

// ============================================================================
// vec2i 实现
// ============================================================================
vec2i::vec2i() : x(0), y(0) {}
vec2i::vec2i(int32_t x, int32_t y) : x(x), y(y) {}
bool vec2i::equals(const vec2i& v) const { return x == v.x && y == v.y; }
void vec2i::copyTo(vec2i& dest) const { dest.x = x; dest.y = y; }
void vec2i::copyFrom(const vec2i& src) { x = src.x; y = src.y; }
vec2i vec2i::add(const vec2i& v) const { return vec2i(x + v.x, y + v.y); }
vec2i vec2i::subtract(const vec2i& v) const { return vec2i(x - v.x, y - v.y); }
int32_t vec2i::lenght(const vec2i& v) const {
  int32_t dx = x - v.x, dy = y - v.y;
  return static_cast<int32_t>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
}
bool vec2i::zero() const { return x == 0 && y == 0; }

// ============================================================================
// vec3i 实现
// ============================================================================
vec3i::vec3i() : x(0), y(0), z(0) {}
vec3i::vec3i(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}
bool vec3i::equals(const vec3i& v) const { return x == v.x && y == v.y && z == v.z; }
void vec3i::copyTo(vec3i& dest) const { dest.x = x; dest.y = y; dest.z = z; }
void vec3i::copyFrom(const vec3i& src) { x = src.x; y = src.y; z = src.z; }
vec3i vec3i::add(const vec3i& v) const { return vec3i(x + v.x, y + v.y, z + v.z); }
vec3i vec3i::subtract(const vec3i& v) const { return vec3i(x - v.x, y - v.y, z - v.z); }
bool vec3i::zero() const { return x == 0 && y == 0 && z == 0; }

// ============================================================================
// vec4i 实现
// ============================================================================
vec4i::vec4i() : x(0), y(0), z(0), w(0) {}
vec4i::vec4i(int32_t x, int32_t y, int32_t z, int32_t w) : x(x), y(y), z(z), w(w) {}
bool vec4i::equals(const vec4i& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
void vec4i::copyTo(vec4i& dest) const { dest.x = x; dest.y = y; dest.z = z; dest.w = w; }
void vec4i::copyFrom(const vec4i& src) { x = src.x; y = src.y; z = src.z; w = src.w; }
bool vec4i::zero() const { return x == 0 && y == 0 && z == 0 && w == 0; }

// ============================================================================
// vec2f 实现
// ============================================================================
vec2f::vec2f() : x(0), y(0) {}
vec2f::vec2f(float x, float y) : x(x), y(y) {}
bool vec2f::equals(const vec2f& v) const { return x == v.x && y == v.y; }
void vec2f::copyTo(vec2f& dest) const { dest.x = x; dest.y = y; }
void vec2f::copyFrom(const vec2f& src) { x = src.x; y = src.y; }
vec2f vec2f::add(const vec2f& v) const { return vec2f(x + v.x, y + v.y); }
vec2f vec2f::subtract(const vec2f& v) const { return vec2f(x - v.x, y - v.y); }
float vec2f::lenght(const vec2f& v) const {
  float dx = x - v.x, dy = y - v.y;
  return std::sqrt(dx * dx + dy * dy);
}
bool vec2f::zero() const { return x == 0 && y == 0; }

// ============================================================================
// vec2d 实现
// ============================================================================
vec2d::vec2d() : x(0), y(0) {}
vec2d::vec2d(double x, double y) : x(x), y(y) {}
bool vec2d::equals(const vec2d& v) const { return x == v.x && y == v.y; }
void vec2d::copyTo(vec2d& dest) const { dest.x = x; dest.y = y; }
void vec2d::copyFrom(const vec2d& src) { x = src.x; y = src.y; }
vec2d vec2d::add(const vec2d& v) const { return vec2d(x + v.x, y + v.y); }
vec2d vec2d::subtract(const vec2d& v) const { return vec2d(x - v.x, y - v.y); }
double vec2d::lenght(const vec2d& v) const {
  double dx = x - v.x, dy = y - v.y;
  return std::sqrt(dx * dx + dy * dy);
}
bool vec2d::zero() const { return x == 0 && y == 0; }

// ============================================================================
// vec3f 实现
// ============================================================================
vec3f::vec3f() : x(0), y(0), z(0) {}
vec3f::vec3f(float x, float y, float z) : x(x), y(y), z(z) {}
bool vec3f::equals(const vec3f& v) const { return x == v.x && y == v.y && z == v.z; }
void vec3f::copyTo(vec3f& dest) const { dest.x = x; dest.y = y; dest.z = z; }
void vec3f::copyFrom(const vec3f& src) { x = src.x; y = src.y; z = src.z; }
vec3f vec3f::add(const vec3f& v) const { return vec3f(x + v.x, y + v.y, z + v.z); }
vec3f vec3f::subtract(const vec3f& v) const { return vec3f(x - v.x, y - v.y, z - v.z); }
float vec3f::lenght(const vec3f& v) const {
  float dx = x - v.x, dy = y - v.y, dz = z - v.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool vec3f::zero() const { return x == 0 && y == 0 && z == 0; }
vec3f vec3f::cross(const vec3f& v) const {
  return vec3f(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
}
float vec3f::dot(const vec3f& v) const { return x * v.x + y * v.y + z * v.z; }
void vec3f::scale(float s) { x *= s; y *= s; z *= s; }
float vec3f::normalize() {
  float len = std::sqrt(x * x + y * y + z * z);
  if (len > 0) {
    float inv = 1.0f / len;
    x *= inv; y *= inv; z *= inv;
  }
  return len;
}
vec3f vec3f::transform3(const Mat3x3f& mat) const {
  vec3f result;
  result.x = x * mat.row0.x + y * mat.row1.x + z * mat.row2.x;
  result.y = x * mat.row0.y + y * mat.row1.y + z * mat.row2.y;
  result.z = x * mat.row0.z + y * mat.row1.z + z * mat.row2.z;
  return result;
}
vec3f vec3f::transform4(const Mat4x4f& mat, bool isVector) const {
  vec3f result;
  result.x = x * mat.row0.x + y * mat.row1.x + z * mat.row2.x;
  result.y = x * mat.row0.y + y * mat.row1.y + z * mat.row2.y;
  result.z = x * mat.row0.z + y * mat.row1.z + z * mat.row2.z;
  if (!isVector) {
    result.x += mat.row3.x;
    result.y += mat.row3.y;
    result.z += mat.row3.z;
  }
  return result;
}
Mat3x3f vec3f::toMat(int one, int two, int three) const {
  Mat3x3f result;
  detail::eulerAngle2Mat3x3(&x, one, two, three, &result.row0.x);
  return result;
}

// ============================================================================
// vec3d 实现
// ============================================================================
vec3d::vec3d() : x(0), y(0), z(0) {}
vec3d::vec3d(double x, double y, double z) : x(x), y(y), z(z) {}
bool vec3d::equals(const vec3d& v) const { return x == v.x && y == v.y && z == v.z; }
void vec3d::copyTo(vec3d& dest) const { dest.x = x; dest.y = y; dest.z = z; }
void vec3d::copyFrom(const vec3d& src) { x = src.x; y = src.y; z = src.z; }
vec3d vec3d::add(const vec3d& v) const { return vec3d(x + v.x, y + v.y, z + v.z); }
vec3d vec3d::subtract(const vec3d& v) const { return vec3d(x - v.x, y - v.y, z - v.z); }
double vec3d::lenght(const vec3d& v) const {
  double dx = x - v.x, dy = y - v.y, dz = z - v.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool vec3d::zero() const { return x == 0 && y == 0 && z == 0; }
vec3d vec3d::cross(const vec3d& v) const {
  return vec3d(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
}
double vec3d::dot(const vec3d& v) const { return x * v.x + y * v.y + z * v.z; }
void vec3d::scale(double s) { x *= s; y *= s; z *= s; }
double vec3d::normalize() {
  double len = std::sqrt(x * x + y * y + z * z);
  if (len > 0) {
    double inv = 1.0 / len;
    x *= inv; y *= inv; z *= inv;
  }
  return len;
}
vec3d vec3d::transform3(const Mat3x3d& mat) const {
  vec3d result;
  result.x = x * mat.row0.x + y * mat.row1.x + z * mat.row2.x;
  result.y = x * mat.row0.y + y * mat.row1.y + z * mat.row2.y;
  result.z = x * mat.row0.z + y * mat.row1.z + z * mat.row2.z;
  return result;
}
vec3d vec3d::transform4(const Mat4x4d& mat, bool isVector) const {
  vec3d result;
  result.x = x * mat.row0.x + y * mat.row1.x + z * mat.row2.x;
  result.y = x * mat.row0.y + y * mat.row1.y + z * mat.row2.y;
  result.z = x * mat.row0.z + y * mat.row1.z + z * mat.row2.z;
  if (!isVector) {
    result.x += mat.row3.x;
    result.y += mat.row3.y;
    result.z += mat.row3.z;
  }
  return result;
}
Mat3x3d vec3d::toMat(int one, int two, int three) const {
  Mat3x3d result;
  detail::eulerAngle2Mat3x3(&x, one, two, three, &result.row0.x);
  return result;
}

// ============================================================================
// vec4f 实现
// ============================================================================
vec4f::vec4f() : x(0), y(0), z(0), w(0) {}
vec4f::vec4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
vec4f::vec4f(const vec3f& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
bool vec4f::equals(const vec4f& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
void vec4f::copyTo(vec4f& dest) const { dest.x = x; dest.y = y; dest.z = z; dest.w = w; }
void vec4f::copyFrom(const vec4f& src) { x = src.x; y = src.y; z = src.z; w = src.w; }
void vec4f::copyToVec3(vec3f& dest) const { dest.x = x; dest.y = y; dest.z = z; }
void vec4f::copyFromVec3(const vec3f& src) { x = src.x; y = src.y; z = src.z; }
void vec4f::copyFromVec3W(const vec3f& src, float w_) { x = src.x; y = src.y; z = src.z; w = w_; }
vec4f vec4f::add(const vec4f& v) const { return vec4f(x + v.x, y + v.y, z + v.z, w + v.w); }
void vec4f::scale(float s) { x *= s; y *= s; z *= s; }
float vec4f::normalize() {
  float len = std::sqrt(x * x + y * y + z * z);
  if (len > 0) {
    float inv = 1.0f / len;
    x *= inv; y *= inv; z *= inv;
  }
  return len;
}
vec4f vec4f::toAxisAngle() const {
  vec4f result;
  detail::quaternion2AxisAngle(&x, &result.x);
  return result;
}
vec4f vec4f::toQuaternion() const {
  vec4f result;
  detail::axisAngle2Quaternion(&x, &result.x);
  return result;
}
Mat3x3f vec4f::formAxisAngle() const {
  Mat3x3f result;
  detail::axisAngle2Mat3x3(&x, &result.row0.x);
  return result;
}
Mat3x3f vec4f::formQuaternion() const {
  Mat3x3f result;
  detail::quaternion2Mat3x3(&x, &result.row0.x);
  return result;
}

// ============================================================================
// vec4d 实现
// ============================================================================
vec4d::vec4d() : x(0), y(0), z(0), w(0) {}
vec4d::vec4d(double x, double y, double z, double w) : x(x), y(y), z(z), w(w) {}
vec4d::vec4d(const vec3d& v, double w) : x(v.x), y(v.y), z(v.z), w(w) {}
bool vec4d::equals(const vec4d& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
void vec4d::copyTo(vec4d& dest) const { dest.x = x; dest.y = y; dest.z = z; dest.w = w; }
void vec4d::copyFrom(const vec4d& src) { x = src.x; y = src.y; z = src.z; w = src.w; }
void vec4d::copyToVec3(vec3d& dest) const { dest.x = x; dest.y = y; dest.z = z; }
void vec4d::copyFromVec3(const vec3d& src) { x = src.x; y = src.y; z = src.z; }
void vec4d::copyFromVec3W(const vec3d& src, double w_) { x = src.x; y = src.y; z = src.z; w = w_; }
vec4d vec4d::add(const vec4d& v) const { return vec4d(x + v.x, y + v.y, z + v.z, w + v.w); }
void vec4d::scale(double s) { x *= s; y *= s; z *= s; }
double vec4d::normalize() {
  double len = std::sqrt(x * x + y * y + z * z);
  if (len > 0) {
    double inv = 1.0 / len;
    x *= inv; y *= inv; z *= inv;
  }
  return len;
}
vec4d vec4d::toAxisAngle() const {
  vec4d result;
  detail::quaternion2AxisAngle(&x, &result.x);
  return result;
}
vec4d vec4d::toQuaternion() const {
  vec4d result;
  detail::axisAngle2Quaternion(&x, &result.x);
  return result;
}
Mat3x3d vec4d::formAxisAngle() const {
  Mat3x3d result;
  detail::axisAngle2Mat3x3(&x, &result.row0.x);
  return result;
}
Mat3x3d vec4d::formQuaternion() const {
  Mat3x3d result;
  detail::quaternion2Mat3x3(&x, &result.row0.x);
  return result;
}

// ============================================================================
// Mat3x3f 实现
// ============================================================================
Mat3x3f::Mat3x3f() : row0(1, 0, 0), row1(0, 1, 0), row2(0, 0, 1) {}
bool Mat3x3f::equals(const Mat3x3f& m) const {
  return row0.equals(m.row0) && row1.equals(m.row1) && row2.equals(m.row2);
}
void Mat3x3f::copyTo(Mat3x3f& dest) const { dest.row0 = row0; dest.row1 = row1; dest.row2 = row2; }
void Mat3x3f::copyFrom(const Mat3x3f& src) { row0 = src.row0; row1 = src.row1; row2 = src.row2; }
bool Mat3x3f::valid() const {
  return row0.y != 0 || row0.z != 0 || row1.x != 0 || row1.z != 0 || row2.x != 0 || row2.y != 0;
}
Mat3x3f Mat3x3f::inverse(bool* success) const {
  Mat3x3f result;
  bool ok = detail::mat3x3Inverse<float>(&row0.x, &result.row0.x);
  if (success) *success = ok;
  if (!ok) result = Mat3x3f();
  return result;
}
Mat3x3f Mat3x3f::transpose() const {
  Mat3x3f result;
  result.row0.x = row0.x; result.row0.y = row1.x; result.row0.z = row2.x;
  result.row1.x = row0.y; result.row1.y = row1.y; result.row1.z = row2.y;
  result.row2.x = row0.z; result.row2.y = row1.z; result.row2.z = row2.z;
  return result;
}
Mat3x3f Mat3x3f::multiply(const Mat3x3f& m) const {
  Mat3x3f result;
  for (int y = 0; y < 3; y++) {
    const vec3f& r = (y == 0) ? row0 : ((y == 1) ? row1 : row2);
    vec3f& out = (y == 0) ? result.row0 : ((y == 1) ? result.row1 : result.row2);
    for (int x = 0; x < 3; x++) {
      (&out.x)[x] = r.x * (&m.row0.x)[x] + r.y * (&m.row1.x)[x] + r.z * (&m.row2.x)[x];
    }
  }
  return result;
}
vec4f Mat3x3f::toAxisAngle() const {
  vec4f result;
  detail::mat3x32AxisAngle(&row0.x, &result.x);
  return result;
}
vec4f Mat3x3f::toQuaternion() const {
  vec4f result;
  detail::mat3x32Quaternion(&row0.x, &result.x);
  return result;
}
vec3f Mat3x3f::toEulerAngle(int one, int two, int three) const {
  vec3f result;
  detail::mat3x32EulerAngle(&row0.x, one, two, three, &result.x);
  return result;
}

// ============================================================================
// Mat3x3d 实现
// ============================================================================
Mat3x3d::Mat3x3d() : row0(1, 0, 0), row1(0, 1, 0), row2(0, 0, 1) {}
bool Mat3x3d::equals(const Mat3x3d& m) const {
  return row0.equals(m.row0) && row1.equals(m.row1) && row2.equals(m.row2);
}
void Mat3x3d::copyTo(Mat3x3d& dest) const { dest.row0 = row0; dest.row1 = row1; dest.row2 = row2; }
void Mat3x3d::copyFrom(const Mat3x3d& src) { row0 = src.row0; row1 = src.row1; row2 = src.row2; }
bool Mat3x3d::valid() const {
  return row0.y != 0 || row0.z != 0 || row1.x != 0 || row1.z != 0 || row2.x != 0 || row2.y != 0;
}
Mat3x3d Mat3x3d::inverse(bool* success) const {
  Mat3x3d result;
  bool ok = detail::mat3x3Inverse<double>(&row0.x, &result.row0.x);
  if (success) *success = ok;
  if (!ok) result = Mat3x3d();
  return result;
}
Mat3x3d Mat3x3d::transpose() const {
  Mat3x3d result;
  result.row0.x = row0.x; result.row0.y = row1.x; result.row0.z = row2.x;
  result.row1.x = row0.y; result.row1.y = row1.y; result.row1.z = row2.y;
  result.row2.x = row0.z; result.row2.y = row1.z; result.row2.z = row2.z;
  return result;
}
Mat3x3d Mat3x3d::multiply(const Mat3x3d& m) const {
  Mat3x3d result;
  for (int y = 0; y < 3; y++) {
    const vec3d& r = (y == 0) ? row0 : ((y == 1) ? row1 : row2);
    vec3d& out = (y == 0) ? result.row0 : ((y == 1) ? result.row1 : result.row2);
    for (int x = 0; x < 3; x++) {
      (&out.x)[x] = r.x * (&m.row0.x)[x] + r.y * (&m.row1.x)[x] + r.z * (&m.row2.x)[x];
    }
  }
  return result;
}
vec4d Mat3x3d::toAxisAngle() const {
  vec4d result;
  detail::mat3x32AxisAngle(&row0.x, &result.x);
  return result;
}
vec4d Mat3x3d::toQuaternion() const {
  vec4d result;
  detail::mat3x32Quaternion(&row0.x, &result.x);
  return result;
}
vec3d Mat3x3d::toEulerAngle(int one, int two, int three) const {
  vec3d result;
  detail::mat3x32EulerAngle(&row0.x, one, two, three, &result.x);
  return result;
}

// ============================================================================
// Mat4x4f 实现
// ============================================================================
Mat4x4f::Mat4x4f() : row0(1, 0, 0, 0), row1(0, 1, 0, 0), row2(0, 0, 1, 0), row3(0, 0, 0, 1) {}
bool Mat4x4f::equals(const Mat4x4f& m) const {
  return row0.equals(m.row0) && row1.equals(m.row1) && row2.equals(m.row2) && row3.equals(m.row3);
}
void Mat4x4f::copyTo(Mat4x4f& dest) const { dest.row0 = row0; dest.row1 = row1; dest.row2 = row2; dest.row3 = row3; }
void Mat4x4f::copyFrom(const Mat4x4f& src) { row0 = src.row0; row1 = src.row1; row2 = src.row2; row3 = src.row3; }
bool Mat4x4f::valid() const {
  if (row0.y != 0 || row0.z != 0 || row1.x != 0 || row1.z != 0 || row2.x != 0 || row2.y != 0) return true;
  if (row3.x != 0 || row3.y != 0 || row3.z != 0) return true;
  return false;
}
Mat4x4f Mat4x4f::inverse(bool* success) const {
  Mat4x4f result;
  bool ok = detail::mat4x4Inverse<float>(&row0.x, &result.row0.x);
  if (success) *success = ok;
  if (!ok) result = Mat4x4f();
  return result;
}
Mat4x4f Mat4x4f::transpose() const {
  Mat4x4f result;
  result.row0.x = row0.x; result.row0.y = row1.x; result.row0.z = row2.x; result.row0.w = row3.x;
  result.row1.x = row0.y; result.row1.y = row1.y; result.row1.z = row2.y; result.row1.w = row3.y;
  result.row2.x = row0.z; result.row2.y = row1.z; result.row2.z = row2.z; result.row2.w = row3.z;
  result.row3.x = row0.w; result.row3.y = row1.w; result.row3.z = row2.w; result.row3.w = row3.w;
  return result;
}
Mat4x4f Mat4x4f::multiply(const Mat4x4f& m) const {
  Mat4x4f result;
  for (int y = 0; y < 4; y++) {
    const vec4f& r = (y == 0) ? row0 : ((y == 1) ? row1 : ((y == 2) ? row2 : row3));
    vec4f& out = (y == 0) ? result.row0 : ((y == 1) ? result.row1 : ((y == 2) ? result.row2 : result.row3));
    for (int x = 0; x < 4; x++) {
      (&out.x)[x] = r.x * (&m.row0.x)[x] + r.y * (&m.row1.x)[x] + r.z * (&m.row2.x)[x] + r.w * (&m.row3.x)[x];
    }
  }
  return result;
}
void Mat4x4f::formTrackPose(const vec3f& pos, const vec4f& quat) {
  Mat3x3f rot = quat.formQuaternion();
  build(rot, pos);
}
void Mat4x4f::toTrackPose(vec3f& pos, vec4f& quat) const {
  Mat3x3f rot;
  split(rot, pos);
  quat = rot.toQuaternion();
}
void Mat4x4f::build(const Mat3x3f& rotate, const vec3f& pos) {
  row0.copyFromVec3W(rotate.row0, 0);
  row1.copyFromVec3W(rotate.row1, 0);
  row2.copyFromVec3W(rotate.row2, 0);
  row3.copyFromVec3W(pos, 1);
}
void Mat4x4f::split(Mat3x3f& rotate, vec3f& pos) const {
  row0.copyToVec3(rotate.row0);
  row1.copyToVec3(rotate.row1);
  row2.copyToVec3(rotate.row2);
  row3.copyToVec3(pos);
}
Mat4x4f Mat4x4f::convertUE4ToOpenCV() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::Zn), static_cast<int>(AxisType::X), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertOpenCVToUE4() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Z), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertCommonToOpenCV() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), static_cast<int>(AxisType::Zn), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertOpenCVToCommon() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), static_cast<int>(AxisType::Zn), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertCommonToUE4() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Zn), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Y), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertUE4ToCommon() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::Z), static_cast<int>(AxisType::Xn), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertUEToMayaZUp() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Z), &result.row0.x);
  return result;
}
Mat4x4f Mat4x4f::convertMayaZUpToUE() const {
  Mat4x4f result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Z), &result.row0.x);
  return result;
}

// ============================================================================
// Mat4x4d 实现
// ============================================================================
Mat4x4d::Mat4x4d() : row0(1, 0, 0, 0), row1(0, 1, 0, 0), row2(0, 0, 1, 0), row3(0, 0, 0, 1) {}
bool Mat4x4d::equals(const Mat4x4d& m) const {
  return row0.equals(m.row0) && row1.equals(m.row1) && row2.equals(m.row2) && row3.equals(m.row3);
}
void Mat4x4d::copyTo(Mat4x4d& dest) const { dest.row0 = row0; dest.row1 = row1; dest.row2 = row2; dest.row3 = row3; }
void Mat4x4d::copyFrom(const Mat4x4d& src) { row0 = src.row0; row1 = src.row1; row2 = src.row2; row3 = src.row3; }
bool Mat4x4d::valid() const {
  if (row0.y != 0 || row0.z != 0 || row1.x != 0 || row1.z != 0 || row2.x != 0 || row2.y != 0) return true;
  if (row3.x != 0 || row3.y != 0 || row3.z != 0) return true;
  return false;
}
Mat4x4d Mat4x4d::inverse(bool* success) const {
  Mat4x4d result;
  bool ok = detail::mat4x4Inverse<double>(&row0.x, &result.row0.x);
  if (success) *success = ok;
  if (!ok) result = Mat4x4d();
  return result;
}
Mat4x4d Mat4x4d::transpose() const {
  Mat4x4d result;
  result.row0.x = row0.x; result.row0.y = row1.x; result.row0.z = row2.x; result.row0.w = row3.x;
  result.row1.x = row0.y; result.row1.y = row1.y; result.row1.z = row2.y; result.row1.w = row3.y;
  result.row2.x = row0.z; result.row2.y = row1.z; result.row2.z = row2.z; result.row2.w = row3.z;
  result.row3.x = row0.w; result.row3.y = row1.w; result.row3.z = row2.w; result.row3.w = row3.w;
  return result;
}
Mat4x4d Mat4x4d::multiply(const Mat4x4d& m) const {
  Mat4x4d result;
  for (int y = 0; y < 4; y++) {
    const vec4d& r = (y == 0) ? row0 : ((y == 1) ? row1 : ((y == 2) ? row2 : row3));
    vec4d& out = (y == 0) ? result.row0 : ((y == 1) ? result.row1 : ((y == 2) ? result.row2 : result.row3));
    for (int x = 0; x < 4; x++) {
      (&out.x)[x] = r.x * (&m.row0.x)[x] + r.y * (&m.row1.x)[x] + r.z * (&m.row2.x)[x] + r.w * (&m.row3.x)[x];
    }
  }
  return result;
}
void Mat4x4d::formTrackPose(const vec3d& pos, const vec4d& quat) {
  Mat3x3d rot = quat.formQuaternion();
  build(rot, pos);
}
void Mat4x4d::toTrackPose(vec3d& pos, vec4d& quat) const {
  Mat3x3d rot;
  split(rot, pos);
  quat = rot.toQuaternion();
}
void Mat4x4d::build(const Mat3x3d& rotate, const vec3d& pos) {
  row0.copyFromVec3W(rotate.row0, 0);
  row1.copyFromVec3W(rotate.row1, 0);
  row2.copyFromVec3W(rotate.row2, 0);
  row3.copyFromVec3W(pos, 1);
}
void Mat4x4d::split(Mat3x3d& rotate, vec3d& pos) const {
  row0.copyToVec3(rotate.row0);
  row1.copyToVec3(rotate.row1);
  row2.copyToVec3(rotate.row2);
  row3.copyToVec3(pos);
}
Mat4x4d Mat4x4d::convertUE4ToOpenCV() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::Zn), static_cast<int>(AxisType::X), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertOpenCVToUE4() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Z), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertCommonToOpenCV() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), static_cast<int>(AxisType::Zn), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertOpenCVToCommon() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::X), static_cast<int>(AxisType::Yn), static_cast<int>(AxisType::Zn), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertCommonToUE4() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Zn), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Y), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertUE4ToCommon() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::Z), static_cast<int>(AxisType::Xn), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertUEToMayaZUp() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Z), &result.row0.x);
  return result;
}
Mat4x4d Mat4x4d::convertMayaZUpToUE() const {
  Mat4x4d result;
  detail::convertCoordinate(&row0.x, static_cast<int>(AxisType::Y), static_cast<int>(AxisType::X), static_cast<int>(AxisType::Z), &result.row0.x);
  return result;
}

// ============================================================================
// trackPosef 实现
// ============================================================================
void trackPosef::toMat4x4(Mat4x4f& mat) const { mat.formTrackPose(pos, quat); }
void trackPosef::fromMat4x4(const Mat4x4f& mat) { mat.toTrackPose(pos, quat); }

// ============================================================================
// trackPosed 实现
// ============================================================================
void trackPosed::toMat4x4(Mat4x4d& mat) const { mat.formTrackPose(pos, quat); }
void trackPosed::fromMat4x4(const Mat4x4d& mat) { mat.toTrackPose(pos, quat); }

// ============================================================================
// 独立函数实现
// ============================================================================
Mat3x3f identMat3x3f() { return Mat3x3f(); }
Mat3x3d identMat3x3d() { return Mat3x3d(); }
Mat4x4f identMat4x4f() { return Mat4x4f(); }
Mat4x4d identMat4x4d() { return Mat4x4d(); }

Mat4x4f scaleIdentMatf(const vec3f& scale) {
  Mat4x4f result;
  result.row0.x = scale.x;
  result.row1.y = scale.y;
  result.row2.z = scale.z;
  return result;
}
Mat4x4d scaleIdentMatd(const vec3d& scale) {
  Mat4x4d result;
  result.row0.x = scale.x;
  result.row1.y = scale.y;
  result.row2.z = scale.z;
  return result;
}

Mat3x3f makeRotatorXf(float angle) {
  Mat3x3f result;
  float rs, rc;
  detail::sincos(angle, rs, rc);
  result.row1.y = rc; result.row1.z = rs;
  result.row2.y = -rs; result.row2.z = rc;
  return result;
}
Mat3x3f makeRotatorYf(float angle) {
  Mat3x3f result;
  float rs, rc;
  detail::sincos(angle, rs, rc);
  result.row0.x = rc; result.row0.z = -rs;
  result.row2.x = rs; result.row2.z = rc;
  return result;
}
Mat3x3f makeRotatorZf(float angle) {
  Mat3x3f result;
  float rs, rc;
  detail::sincos(angle, rs, rc);
  result.row0.x = rc; result.row0.y = rs;
  result.row1.x = -rs; result.row1.y = rc;
  return result;
}
Mat3x3d makeRotatorXd(double angle) {
  Mat3x3d result;
  double rs, rc;
  detail::sincos(angle, rs, rc);
  result.row1.y = rc; result.row1.z = rs;
  result.row2.y = -rs; result.row2.z = rc;
  return result;
}
Mat3x3d makeRotatorYd(double angle) {
  Mat3x3d result;
  double rs, rc;
  detail::sincos(angle, rs, rc);
  result.row0.x = rc; result.row0.z = -rs;
  result.row2.x = rs; result.row2.z = rc;
  return result;
}
Mat3x3d makeRotatorZd(double angle) {
  Mat3x3d result;
  double rs, rc;
  detail::sincos(angle, rs, rc);
  result.row0.x = rc; result.row0.y = rs;
  result.row1.x = -rs; result.row1.y = rc;
  return result;
}

Mat4x4f makeTransformf(const Mat3x3f& rotation, const vec3f& translation) {
  vec3f unitScale(1.0f, 1.0f, 1.0f);
  return makeTransformScalef(rotation, translation, unitScale);
}
Mat4x4f makeTransformScalef(const Mat3x3f& rotation, const vec3f& translation, const vec3f& scale) {
  Mat4x4f result;
  result.row0.copyFromVec3W(rotation.row0, 0);
  result.row1.copyFromVec3W(rotation.row1, 0);
  result.row2.copyFromVec3W(rotation.row2, 0);
  result.row0.x *= scale.x; result.row0.y *= scale.x; result.row0.z *= scale.x;
  result.row1.x *= scale.y; result.row1.y *= scale.y; result.row1.z *= scale.y;
  result.row2.x *= scale.z; result.row2.y *= scale.z; result.row2.z *= scale.z;
  result.row3.copyFromVec3W(translation, 1);
  return result;
}
Mat4x4d makeTransformd(const Mat3x3d& rotation, const vec3d& translation) {
  vec3d unitScale(1.0, 1.0, 1.0);
  return makeTransformScaled(rotation, translation, unitScale);
}
Mat4x4d makeTransformScaled(const Mat3x3d& rotation, const vec3d& translation, const vec3d& scale) {
  Mat4x4d result;
  result.row0.copyFromVec3W(rotation.row0, 0);
  result.row1.copyFromVec3W(rotation.row1, 0);
  result.row2.copyFromVec3W(rotation.row2, 0);
  result.row0.x *= scale.x; result.row0.y *= scale.x; result.row0.z *= scale.x;
  result.row1.x *= scale.y; result.row1.y *= scale.y; result.row1.z *= scale.y;
  result.row2.x *= scale.z; result.row2.y *= scale.z; result.row2.z *= scale.z;
  result.row3.copyFromVec3W(translation, 1);
  return result;
}

void transformBreakf(const Mat4x4f& transform, Mat3x3f& rotation, vec3f& translation) {
  transform.row0.copyToVec3(rotation.row0);
  transform.row1.copyToVec3(rotation.row1);
  transform.row2.copyToVec3(rotation.row2);
  transform.row3.copyToVec3(translation);
}
void transformBreakScalef(const Mat4x4f& transform, Mat3x3f& rotation, vec3f& translation, vec3f& scale) {
  transform.row0.copyToVec3(rotation.row0);
  transform.row1.copyToVec3(rotation.row1);
  transform.row2.copyToVec3(rotation.row2);
  transform.row3.copyToVec3(translation);
  vec3f r0 = rotation.row0, r1 = rotation.row1, r2 = rotation.row2;
  scale.x = r0.normalize();
  scale.y = r1.normalize();
  scale.z = r2.normalize();
  rotation.row0 = r0; rotation.row1 = r1; rotation.row2 = r2;
}
void transformBreakd(const Mat4x4d& transform, Mat3x3d& rotation, vec3d& translation) {
  transform.row0.copyToVec3(rotation.row0);
  transform.row1.copyToVec3(rotation.row1);
  transform.row2.copyToVec3(rotation.row2);
  transform.row3.copyToVec3(translation);
}
void transformBreakScaled(const Mat4x4d& transform, Mat3x3d& rotation, vec3d& translation, vec3d& scale) {
  transform.row0.copyToVec3(rotation.row0);
  transform.row1.copyToVec3(rotation.row1);
  transform.row2.copyToVec3(rotation.row2);
  transform.row3.copyToVec3(translation);
  vec3d r0 = rotation.row0, r1 = rotation.row1, r2 = rotation.row2;
  scale.x = r0.normalize();
  scale.y = r1.normalize();
  scale.z = r2.normalize();
  rotation.row0 = r0; rotation.row1 = r1; rotation.row2 = r2;
}

vec3f makeVec3f(const vec4f& v) { return vec3f(v.x, v.y, v.z); }
vec3d makeVec3d(const vec4d& v) { return vec3d(v.x, v.y, v.z); }
Mat4x4f makeMat4x4f(const Mat3x3f& m) {
  Mat4x4f result;
  result.row0.copyFromVec3W(m.row0, 0);
  result.row1.copyFromVec3W(m.row1, 0);
  result.row2.copyFromVec3W(m.row2, 0);
  return result;
}
Mat4x4d makeMat4x4d(const Mat3x3d& m) {
  Mat4x4d result;
  result.row0.copyFromVec3W(m.row0, 0);
  result.row1.copyFromVec3W(m.row1, 0);
  result.row2.copyFromVec3W(m.row2, 0);
  return result;
}
Mat3x3f makeMat3x3f(const Mat4x4f& m) {
  Mat3x3f result;
  result.row0 = vec3f(m.row0.x, m.row0.y, m.row0.z);
  result.row1 = vec3f(m.row1.x, m.row1.y, m.row1.z);
  result.row2 = vec3f(m.row2.x, m.row2.y, m.row2.z);
  return result;
}
Mat3x3d makeMat3x3d(const Mat4x4d& m) {
  Mat3x3d result;
  result.row0 = vec3d(m.row0.x, m.row0.y, m.row0.z);
  result.row1 = vec3d(m.row1.x, m.row1.y, m.row1.z);
  result.row2 = vec3d(m.row2.x, m.row2.y, m.row2.z);
  return result;
}

void copyVec4ToVec3f(const vec4f& src, vec3f& dest) { dest.x = src.x; dest.y = src.y; dest.z = src.z; }
void copyVec4ToVec3d(const vec4d& src, vec3d& dest) { dest.x = src.x; dest.y = src.y; dest.z = src.z; }
void copyMat3ToMat4f(const Mat3x3f& src, Mat4x4f& dest) {
  dest.row0.copyFromVec3W(src.row0, dest.row0.w);
  dest.row1.copyFromVec3W(src.row1, dest.row1.w);
  dest.row2.copyFromVec3W(src.row2, dest.row2.w);
}
void copyMat3ToMat4d(const Mat3x3d& src, Mat4x4d& dest) {
  dest.row0.copyFromVec3W(src.row0, dest.row0.w);
  dest.row1.copyFromVec3W(src.row1, dest.row1.w);
  dest.row2.copyFromVec3W(src.row2, dest.row2.w);
}
void copyMat3VecToMat4f(const Mat3x3f& mat, const vec3f& vec, Mat4x4f& dest) {
  dest.row0.copyFromVec3W(mat.row0, 0);
  dest.row1.copyFromVec3W(mat.row1, 0);
  dest.row2.copyFromVec3W(mat.row2, 0);
  dest.row3.copyFromVec3W(vec, 1);
}
void copyMat3VecToMat4d(const Mat3x3d& mat, const vec3d& vec, Mat4x4d& dest) {
  dest.row0.copyFromVec3W(mat.row0, 0);
  dest.row1.copyFromVec3W(mat.row1, 0);
  dest.row2.copyFromVec3W(mat.row2, 0);
  dest.row3.copyFromVec3W(vec, 1);
}

vec4f slerpQuaternionf(float t, const vec4f& a, const vec4f& b) {
  vec4f result;
  detail::slerpQuaternion(t, &a.x, &b.x, &result.x);
  return result;
}
vec4d slerpQuaterniond(double t, const vec4d& a, const vec4d& b) {
  vec4d result;
  detail::slerpQuaternion(t, &a.x, &b.x, &result.x);
  return result;
}
Mat3x3f slerpMat3x3f(float t, const Mat3x3f& a, const Mat3x3f& b) {
  vec4f qa = a.toQuaternion();
  vec4f qb = b.toQuaternion();
  vec4f qt = slerpQuaternionf(t, qa, qb);
  return qt.formQuaternion();
}
Mat3x3d slerpMat3x3d(double t, const Mat3x3d& a, const Mat3x3d& b) {
  vec4d qa = a.toQuaternion();
  vec4d qb = b.toQuaternion();
  vec4d qt = slerpQuaterniond(t, qa, qb);
  return qt.formQuaternion();
}
vec3f lerpVec3f(float t, const vec3f& a, const vec3f& b) {
  return vec3f(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
vec3d lerpVec3d(double t, const vec3d& a, const vec3d& b) {
  return vec3d(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

bool nearlyEqualf(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }
bool nearlyEquald(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

vec3f axisVecf(AxisType axisType) {
  static const vec3f unitVectors[] = {
    vec3f(1, 0, 0), vec3f(0, 1, 0), vec3f(0, 0, 1),
    vec3f(-1, 0, 0), vec3f(0, -1, 0), vec3f(0, 0, -1)
  };
  return unitVectors[static_cast<int>(axisType)];
}
vec3d axisVecd(AxisType axisType) {
  static const vec3d unitVectors[] = {
    vec3d(1, 0, 0), vec3d(0, 1, 0), vec3d(0, 0, 1),
    vec3d(-1, 0, 0), vec3d(0, -1, 0), vec3d(0, 0, -1)
  };
  return unitVectors[static_cast<int>(axisType)];
}

}
