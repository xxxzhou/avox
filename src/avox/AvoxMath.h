#pragma once

#include <stdint.h>

#include "AvoxDef.h"

namespace avox {

// 主要参照
// GeometricTools/GTMathematics/Algebra(https://github.com/davideberly/GeometricTools)
// UE4 Runtime/Core/Math/UnrealMathFPU.h
// UE4 Runtime/Core/Math/UnrealMath.cpp

// 因为考虑用swig给别的语言使用,功能使用有名字的函数,重载方法调用有名字的函数
// 1 行矩阵 顺序从左向右 全以角度输入,计算内部转狐度
// 2 右手坐标系运算,Z轴手向眼为正,旋转方向逆时针为正方向,叉积从左向右.
// 3 矩阵顺序,缩放,旋转,位移,这个顺序下,可直接从矩阵中取出值表示对应信息

// 本运算库设定右手坐标系,使用行矩阵,X_RIGHT,Y_UP,从手到眼是前向
// 对照设定,别的坐标系参照设定坐标系如下.
// Unreal Engine: 左
//  X : Back
//  Y : Right
//  Z : Up
// OpenCV: 右
//  X : Right
//  Y : Down
//  Z : Back
// Redspy/HTC/FBX/MayaYUp 右(同设定坐标系)
//  X : Right
//  Y : UP
//  Z : Front
// MayaZUp 右(UE导出FBX所用坐标系)
//  X : Right
//  Y : Back
//  Z : UP
// OpenGL: 左 X-Right Y-Up Z-Back
// https://www.geometrictools.com/Documentation/ConvertingBetweenCoordinateSystems.pdf

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795028841971693993751 /* pi */
#endif

// 常量
inline constexpr double angle_radian = M_PI / 180.0;
inline constexpr double radina_angle = 180.0 / M_PI;
inline constexpr double small_num = 1.e-8;

// 轴类型枚举
enum class AxisType : int32_t { X = 0, Y, Z, Xn, Yn, Zn };

// ============================================================================
// 前向声明
// ============================================================================
struct vec2i;
struct vec3i;
struct vec4i;
struct vec2f;
struct vec3f;
struct vec4f;
struct vec2d;
struct vec3d;
struct vec4d;
struct Mat3x3f;
struct Mat3x3d;
struct Mat4x4f;
struct Mat4x4d;
struct trackPosef;
struct trackPosed;

// ============================================================================
// vec2i - 2D 整数向量
// ============================================================================
struct vec2i {
  int32_t x;
  int32_t y;

  AVOX_EXPORT vec2i();
  AVOX_EXPORT vec2i(int32_t x, int32_t y);

  AVOX_EXPORT bool equals(const vec2i& v) const;
  AVOX_EXPORT void copyTo(vec2i& dest) const;
  AVOX_EXPORT void copyFrom(const vec2i& src);
  AVOX_EXPORT vec2i add(const vec2i& v) const;
  AVOX_EXPORT vec2i subtract(const vec2i& v) const;
  AVOX_EXPORT int32_t lenght(const vec2i& v) const;
  AVOX_EXPORT bool zero() const;
};

// ============================================================================
// vec3i - 3D 整数向量
// ============================================================================
struct vec3i {
  int32_t x;
  int32_t y;
  int32_t z;

  AVOX_EXPORT vec3i();
  AVOX_EXPORT vec3i(int32_t x, int32_t y, int32_t z);

  AVOX_EXPORT bool equals(const vec3i& v) const;
  AVOX_EXPORT void copyTo(vec3i& dest) const;
  AVOX_EXPORT void copyFrom(const vec3i& src);
  AVOX_EXPORT vec3i add(const vec3i& v) const;
  AVOX_EXPORT vec3i subtract(const vec3i& v) const;
  AVOX_EXPORT bool zero() const;
};

// ============================================================================
// vec4i - 4D 整数向量
// ============================================================================
struct vec4i {
  int32_t x;
  int32_t y;
  int32_t z;
  int32_t w;

  AVOX_EXPORT vec4i();
  AVOX_EXPORT vec4i(int32_t x, int32_t y, int32_t z, int32_t w);

  AVOX_EXPORT bool equals(const vec4i& v) const;
  AVOX_EXPORT void copyTo(vec4i& dest) const;
  AVOX_EXPORT void copyFrom(const vec4i& src);
  AVOX_EXPORT bool zero() const;
};

// ============================================================================
// vec2f - 2D 浮点向量
// ============================================================================
struct vec2f {
  float x;
  float y;

  AVOX_EXPORT vec2f();
  AVOX_EXPORT vec2f(float x, float y);

  bool operator==(const vec2f& v) const { return x == v.x && y == v.y; }
  AVOX_EXPORT bool equals(const vec2f& v) const;
  AVOX_EXPORT void copyTo(vec2f& dest) const;
  AVOX_EXPORT void copyFrom(const vec2f& src);
  AVOX_EXPORT vec2f add(const vec2f& v) const;
  AVOX_EXPORT vec2f subtract(const vec2f& v) const;
  AVOX_EXPORT float lenght(const vec2f& v) const;
  AVOX_EXPORT bool zero() const;
};

// ============================================================================
// vec2d - 2D 双精度向量
// ============================================================================
struct vec2d {
  double x;
  double y;

  AVOX_EXPORT vec2d();
  AVOX_EXPORT vec2d(double x, double y);

  bool operator==(const vec2d& v) const { return x == v.x && y == v.y; }
  AVOX_EXPORT bool equals(const vec2d& v) const;
  AVOX_EXPORT void copyTo(vec2d& dest) const;
  AVOX_EXPORT void copyFrom(const vec2d& src);
  AVOX_EXPORT vec2d add(const vec2d& v) const;
  AVOX_EXPORT vec2d subtract(const vec2d& v) const;
  AVOX_EXPORT double lenght(const vec2d& v) const;
  AVOX_EXPORT bool zero() const;
};

// ============================================================================
// vec3f - 3D 浮点向量
// ============================================================================
struct vec3f {
  float x;
  float y;
  float z;

  AVOX_EXPORT vec3f();
  AVOX_EXPORT vec3f(float x, float y, float z);

  // 基础方法
  bool operator==(const vec3f& v) const {
    return x == v.x && y == v.y && z == v.z;
  }
  AVOX_EXPORT bool equals(const vec3f& v) const;
  AVOX_EXPORT void copyTo(vec3f& dest) const;
  AVOX_EXPORT void copyFrom(const vec3f& src);
  AVOX_EXPORT vec3f add(const vec3f& v) const;
  AVOX_EXPORT vec3f subtract(const vec3f& v) const;
  AVOX_EXPORT float lenght(const vec3f& v) const;
  AVOX_EXPORT bool zero() const;

  // 向量操作
  AVOX_EXPORT vec3f cross(const vec3f& v) const;
  AVOX_EXPORT float dot(const vec3f& v) const;
  AVOX_EXPORT void scale(float s);
  AVOX_EXPORT float normalize();

  // 变换
  AVOX_EXPORT vec3f transform3(const Mat3x3f& mat) const;
  AVOX_EXPORT vec3f transform4(const Mat4x4f& mat, bool isVector = false) const;
  AVOX_EXPORT Mat3x3f toMat(int one, int two, int three) const;
};

// ============================================================================
// vec3d - 3D 双精度向量
// ============================================================================
struct vec3d {
  double x;
  double y;
  double z;

  AVOX_EXPORT vec3d();
  AVOX_EXPORT vec3d(double x, double y, double z);

  // 基础方法
  bool operator==(const vec3d& v) const {
    return x == v.x && y == v.y && z == v.z;
  }
  AVOX_EXPORT bool equals(const vec3d& v) const;
  AVOX_EXPORT void copyTo(vec3d& dest) const;
  AVOX_EXPORT void copyFrom(const vec3d& src);
  AVOX_EXPORT vec3d add(const vec3d& v) const;
  AVOX_EXPORT vec3d subtract(const vec3d& v) const;
  AVOX_EXPORT double lenght(const vec3d& v) const;
  AVOX_EXPORT bool zero() const;

  // 向量操作
  AVOX_EXPORT vec3d cross(const vec3d& v) const;
  AVOX_EXPORT double dot(const vec3d& v) const;
  AVOX_EXPORT void scale(double s);
  AVOX_EXPORT double normalize();

  // 变换
  AVOX_EXPORT vec3d transform3(const Mat3x3d& mat) const;
  AVOX_EXPORT vec3d transform4(const Mat4x4d& mat, bool isVector = false) const;
  AVOX_EXPORT Mat3x3d toMat(int one, int two, int three) const;
};

// ============================================================================
// vec4f - 4D 浮点向量
// ============================================================================
struct vec4f {
  float x;
  float y;
  float z;
  float w;

  AVOX_EXPORT vec4f();
  AVOX_EXPORT vec4f(float x, float y, float z, float w);
  AVOX_EXPORT vec4f(const vec3f& v, float w);

  // 基础方法
  bool operator==(const vec4f& v) const {
    return x == v.x && y == v.y && z == v.z && w == v.w;
  }
  AVOX_EXPORT bool equals(const vec4f& v) const;
  AVOX_EXPORT void copyTo(vec4f& dest) const;
  AVOX_EXPORT void copyFrom(const vec4f& src);
  AVOX_EXPORT void copyToVec3(vec3f& dest) const;
  AVOX_EXPORT void copyFromVec3(const vec3f& src);
  AVOX_EXPORT void copyFromVec3W(const vec3f& src, float w);
  AVOX_EXPORT vec4f add(const vec4f& v) const;
  AVOX_EXPORT void scale(float s);
  AVOX_EXPORT float normalize();

  // 四元数/轴角转换
  AVOX_EXPORT vec4f toAxisAngle() const;
  AVOX_EXPORT vec4f toQuaternion() const;
  AVOX_EXPORT Mat3x3f formAxisAngle() const;
  AVOX_EXPORT Mat3x3f formQuaternion() const;
};

// ============================================================================
// vec4d - 4D 双精度向量
// ============================================================================
struct vec4d {
  double x;
  double y;
  double z;
  double w;

  AVOX_EXPORT vec4d();
  AVOX_EXPORT vec4d(double x, double y, double z, double w);
  AVOX_EXPORT vec4d(const vec3d& v, double w);

  // 基础方法
  bool operator==(const vec4d& v) const {
    return x == v.x && y == v.y && z == v.z && w == v.w;
  }
  AVOX_EXPORT bool equals(const vec4d& v) const;
  AVOX_EXPORT void copyTo(vec4d& dest) const;
  AVOX_EXPORT void copyFrom(const vec4d& src);
  AVOX_EXPORT void copyToVec3(vec3d& dest) const;
  AVOX_EXPORT void copyFromVec3(const vec3d& src);
  AVOX_EXPORT void copyFromVec3W(const vec3d& src, double w);
  AVOX_EXPORT vec4d add(const vec4d& v) const;
  AVOX_EXPORT void scale(double s);
  AVOX_EXPORT double normalize();

  // 四元数/轴角转换
  AVOX_EXPORT vec4d toAxisAngle() const;
  AVOX_EXPORT vec4d toQuaternion() const;
  AVOX_EXPORT Mat3x3d formAxisAngle() const;
  AVOX_EXPORT Mat3x3d formQuaternion() const;
};

// ============================================================================
// Mat3x3f - 3x3 浮点矩阵
// ============================================================================
struct Mat3x3f {
  vec3f row0;
  vec3f row1;
  vec3f row2;

  AVOX_EXPORT Mat3x3f();

  // 基础方法
  bool operator==(const Mat3x3f& m) const {
    return row0 == m.row0 && row1 == m.row1 && row2 == m.row2;
  }
  AVOX_EXPORT bool equals(const Mat3x3f& m) const;
  AVOX_EXPORT void copyTo(Mat3x3f& dest) const;
  AVOX_EXPORT void copyFrom(const Mat3x3f& src);
  AVOX_EXPORT bool valid() const;

  // 矩阵操作
  AVOX_EXPORT Mat3x3f inverse(bool* success = nullptr) const;
  AVOX_EXPORT Mat3x3f transpose() const;
  AVOX_EXPORT Mat3x3f multiply(const Mat3x3f& m) const;

  // 转换
  AVOX_EXPORT vec4f toAxisAngle() const;
  AVOX_EXPORT vec4f toQuaternion() const;
  AVOX_EXPORT vec3f toEulerAngle(int one, int two, int three) const;
};

// ============================================================================
// Mat3x3d - 3x3 双精度矩阵
// ============================================================================
struct Mat3x3d {
  vec3d row0;
  vec3d row1;
  vec3d row2;

  AVOX_EXPORT Mat3x3d();

  // 基础方法
  bool operator==(const Mat3x3d& m) const {
    return row0 == m.row0 && row1 == m.row1 && row2 == m.row2;
  }
  AVOX_EXPORT bool equals(const Mat3x3d& m) const;
  AVOX_EXPORT void copyTo(Mat3x3d& dest) const;
  AVOX_EXPORT void copyFrom(const Mat3x3d& src);
  AVOX_EXPORT bool valid() const;

  // 矩阵操作
  AVOX_EXPORT Mat3x3d inverse(bool* success = nullptr) const;
  AVOX_EXPORT Mat3x3d transpose() const;
  AVOX_EXPORT Mat3x3d multiply(const Mat3x3d& m) const;

  // 转换
  AVOX_EXPORT vec4d toAxisAngle() const;
  AVOX_EXPORT vec4d toQuaternion() const;
  AVOX_EXPORT vec3d toEulerAngle(int one, int two, int three) const;
};

// ============================================================================
// Mat4x4f - 4x4 浮点矩阵
// ============================================================================
struct Mat4x4f {
  vec4f row0;
  vec4f row1;
  vec4f row2;
  vec4f row3;

  AVOX_EXPORT Mat4x4f();

  // 基础方法
  bool operator==(const Mat4x4f& m) const {
    return row0 == m.row0 && row1 == m.row1 && row2 == m.row2 && row3 == m.row3;
  }
  AVOX_EXPORT bool equals(const Mat4x4f& m) const;
  AVOX_EXPORT void copyTo(Mat4x4f& dest) const;
  AVOX_EXPORT void copyFrom(const Mat4x4f& src);
  AVOX_EXPORT bool valid() const;

  // 矩阵操作
  AVOX_EXPORT Mat4x4f inverse(bool* success = nullptr) const;
  AVOX_EXPORT Mat4x4f transpose() const;
  AVOX_EXPORT Mat4x4f multiply(const Mat4x4f& m) const;

  // 构建/分解
  AVOX_EXPORT void formTrackPose(const vec3f& pos, const vec4f& quat);
  AVOX_EXPORT void toTrackPose(vec3f& pos, vec4f& quat) const;
  AVOX_EXPORT void build(const Mat3x3f& rotate, const vec3f& pos);
  AVOX_EXPORT void split(Mat3x3f& rotate, vec3f& pos) const;

  // 坐标系转换
  AVOX_EXPORT Mat4x4f convertUE4ToOpenCV() const;
  AVOX_EXPORT Mat4x4f convertOpenCVToUE4() const;
  AVOX_EXPORT Mat4x4f convertCommonToOpenCV() const;
  AVOX_EXPORT Mat4x4f convertOpenCVToCommon() const;
  AVOX_EXPORT Mat4x4f convertCommonToUE4() const;
  AVOX_EXPORT Mat4x4f convertUE4ToCommon() const;
  AVOX_EXPORT Mat4x4f convertUEToMayaZUp() const;
  AVOX_EXPORT Mat4x4f convertMayaZUpToUE() const;
};

// ============================================================================
// Mat4x4d - 4x4 双精度矩阵
// ============================================================================
struct Mat4x4d {
  vec4d row0;
  vec4d row1;
  vec4d row2;
  vec4d row3;

  AVOX_EXPORT Mat4x4d();

  // 基础方法
  bool operator==(const Mat4x4d& m) const {
    return row0 == m.row0 && row1 == m.row1 && row2 == m.row2 && row3 == m.row3;
  }
  AVOX_EXPORT bool equals(const Mat4x4d& m) const;
  AVOX_EXPORT void copyTo(Mat4x4d& dest) const;
  AVOX_EXPORT void copyFrom(const Mat4x4d& src);
  AVOX_EXPORT bool valid() const;

  // 矩阵操作
  AVOX_EXPORT Mat4x4d inverse(bool* success = nullptr) const;
  AVOX_EXPORT Mat4x4d transpose() const;
  AVOX_EXPORT Mat4x4d multiply(const Mat4x4d& m) const;

  // 构建/分解
  AVOX_EXPORT void formTrackPose(const vec3d& pos, const vec4d& quat);
  AVOX_EXPORT void toTrackPose(vec3d& pos, vec4d& quat) const;
  AVOX_EXPORT void build(const Mat3x3d& rotate, const vec3d& pos);
  AVOX_EXPORT void split(Mat3x3d& rotate, vec3d& pos) const;

  // 坐标系转换
  AVOX_EXPORT Mat4x4d convertUE4ToOpenCV() const;
  AVOX_EXPORT Mat4x4d convertOpenCVToUE4() const;
  AVOX_EXPORT Mat4x4d convertCommonToOpenCV() const;
  AVOX_EXPORT Mat4x4d convertOpenCVToCommon() const;
  AVOX_EXPORT Mat4x4d convertCommonToUE4() const;
  AVOX_EXPORT Mat4x4d convertUE4ToCommon() const;
  AVOX_EXPORT Mat4x4d convertUEToMayaZUp() const;
  AVOX_EXPORT Mat4x4d convertMayaZUpToUE() const;
};

// ============================================================================
// trackPosef - 浮点位姿
// ============================================================================
struct trackPosef {
  vec3f pos;
  vec4f quat;

  AVOX_EXPORT void toMat4x4(Mat4x4f& mat) const;
  AVOX_EXPORT void fromMat4x4(const Mat4x4f& mat);
};

// ============================================================================
// trackPosed - 双精度位姿
// ============================================================================
struct trackPosed {
  vec3d pos;
  vec4d quat;

  AVOX_EXPORT void toMat4x4(Mat4x4d& mat) const;
  AVOX_EXPORT void fromMat4x4(const Mat4x4d& mat);
};

extern "C" {
// ============================================================================
// 独立函数 - 工厂函数
// ============================================================================

// 单位矩阵
AVOX_EXPORT Mat3x3f identMat3x3f();
AVOX_EXPORT Mat3x3d identMat3x3d();
AVOX_EXPORT Mat4x4f identMat4x4f();
AVOX_EXPORT Mat4x4d identMat4x4d();

// 缩放矩阵
AVOX_EXPORT Mat4x4f scaleIdentMatf(const vec3f& scale);
AVOX_EXPORT Mat4x4d scaleIdentMatd(const vec3d& scale);

// 旋转矩阵
AVOX_EXPORT Mat3x3f makeRotatorXf(float angle);
AVOX_EXPORT Mat3x3f makeRotatorYf(float angle);
AVOX_EXPORT Mat3x3f makeRotatorZf(float angle);
AVOX_EXPORT Mat3x3d makeRotatorXd(double angle);
AVOX_EXPORT Mat3x3d makeRotatorYd(double angle);
AVOX_EXPORT Mat3x3d makeRotatorZd(double angle);

// 变换矩阵
AVOX_EXPORT Mat4x4f makeTransformf(const Mat3x3f& rotation,
                                  const vec3f& translation);
AVOX_EXPORT Mat4x4f makeTransformScalef(const Mat3x3f& rotation,
                                       const vec3f& translation,
                                       const vec3f& scale);
AVOX_EXPORT Mat4x4d makeTransformd(const Mat3x3d& rotation,
                                  const vec3d& translation);
AVOX_EXPORT Mat4x4d makeTransformScaled(const Mat3x3d& rotation,
                                       const vec3d& translation,
                                       const vec3d& scale);

// 变换分解
AVOX_EXPORT void transformBreakf(const Mat4x4f& transform, Mat3x3f& rotation,
                                vec3f& translation);
AVOX_EXPORT void transformBreakScalef(const Mat4x4f& transform,
                                     Mat3x3f& rotation, vec3f& translation,
                                     vec3f& scale);
AVOX_EXPORT void transformBreakd(const Mat4x4d& transform, Mat3x3d& rotation,
                                vec3d& translation);
AVOX_EXPORT void transformBreakScaled(const Mat4x4d& transform,
                                     Mat3x3d& rotation, vec3d& translation,
                                     vec3d& scale);

// ============================================================================
// 独立函数 - 类型转换
// ============================================================================

AVOX_EXPORT vec3f makeVec3f(const vec4f& v);
AVOX_EXPORT vec3d makeVec3d(const vec4d& v);
AVOX_EXPORT Mat4x4f makeMat4x4f(const Mat3x3f& m);
AVOX_EXPORT Mat4x4d makeMat4x4d(const Mat3x3d& m);
AVOX_EXPORT Mat3x3f makeMat3x3f(const Mat4x4f& m);
AVOX_EXPORT Mat3x3d makeMat3x3d(const Mat4x4d& m);

AVOX_EXPORT void copyVec4ToVec3f(const vec4f& src, vec3f& dest);
AVOX_EXPORT void copyVec4ToVec3d(const vec4d& src, vec3d& dest);
AVOX_EXPORT void copyMat3ToMat4f(const Mat3x3f& src, Mat4x4f& dest);
AVOX_EXPORT void copyMat3ToMat4d(const Mat3x3d& src, Mat4x4d& dest);
AVOX_EXPORT void copyMat3VecToMat4f(const Mat3x3f& mat, const vec3f& vec,
                                   Mat4x4f& dest);
AVOX_EXPORT void copyMat3VecToMat4d(const Mat3x3d& mat, const vec3d& vec,
                                   Mat4x4d& dest);

// ============================================================================
// 独立函数 - 插值
// ============================================================================

AVOX_EXPORT vec4f slerpQuaternionf(float t, const vec4f& a, const vec4f& b);
AVOX_EXPORT vec4d slerpQuaterniond(double t, const vec4d& a, const vec4d& b);
AVOX_EXPORT Mat3x3f slerpMat3x3f(float t, const Mat3x3f& a, const Mat3x3f& b);
AVOX_EXPORT Mat3x3d slerpMat3x3d(double t, const Mat3x3d& a, const Mat3x3d& b);
AVOX_EXPORT vec3f lerpVec3f(float t, const vec3f& a, const vec3f& b);
AVOX_EXPORT vec3d lerpVec3d(double t, const vec3d& a, const vec3d& b);

// ============================================================================
// 独立函数 - 工具
// ============================================================================

AVOX_EXPORT bool nearlyEqualf(float a, float b, float tolerance = 1.e-8f);
AVOX_EXPORT bool nearlyEquald(double a, double b, double tolerance = 1.e-8);

AVOX_EXPORT vec3f axisVecf(AxisType axisType);
AVOX_EXPORT vec3d axisVecd(AxisType axisType);
}

}
