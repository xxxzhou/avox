// AvoxMath 单元测试: 向量/矩阵/四元数/坐标转换/工厂函数/插值
// 从 samples/functest/mathtest.cpp 迁移而来 (原 163 条断言), 与既有 test_math 合并去重。
#include <doctest.h>

#include <cmath>

#include "avox/AvoxMath.h"

namespace avox {
namespace {
// 浮点比较: 沿用原 mathtest 的绝对误差口径 (doctest::Approx 是相对误差, 语义不同)
bool almostEqual(float a, float b, float eps = 1e-5f) { return std::abs(a - b) < eps; }
bool almostEqual(double a, double b, double eps = 1e-10) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("vec2i: 构造/equals/copy/加减/lenght/zero") {
  // 构造函数
  vec2i v1;
  CHECK(v1.x == 0);  // default constructor
  CHECK(v1.y == 0);  // default constructor

  vec2i v2(3, 4);
  CHECK(v2.x == 3);  // parameterized constructor
  CHECK(v2.y == 4);  // parameterized constructor

  // equals
  vec2i v3(3, 4);
  CHECK(v2.equals(v3));  // equals

  // copyTo/copyFrom
  vec2i v4;
  v2.copyTo(v4);
  CHECK(v4.x == 3);  // copyTo
  CHECK(v4.y == 4);  // copyTo

  vec2i v5;
  v5.copyFrom(v2);
  CHECK(v5.x == 3);  // copyFrom
  CHECK(v5.y == 4);  // copyFrom

  // add
  vec2i v6 = v2.add(vec2i(1, 2));
  CHECK(v6.x == 4);  // add
  CHECK(v6.y == 6);  // add

  // subtract
  vec2i v7 = v2.subtract(vec2i(1, 2));
  CHECK(v7.x == 2);  // subtract
  CHECK(v7.y == 2);  // subtract

  // lenght (distance between two points)
  int32_t dist = vec2i(0, 0).lenght(vec2i(3, 4));
  CHECK(dist == 5);  // lenght

  // zero
  CHECK(vec2i().zero());  // zero
  CHECK(!vec2i(1, 0).zero());  // zero
}

TEST_CASE("vec3i: 构造/equals/copy/加减/zero") {
  vec3i v1;
  CHECK(v1.x == 0);  // default constructor
  CHECK(v1.y == 0);  // default constructor
  CHECK(v1.z == 0);  // default constructor

  vec3i v2(1, 2, 3);
  CHECK(v2.x == 1);  // parameterized constructor
  CHECK(v2.y == 2);  // parameterized constructor
  CHECK(v2.z == 3);  // parameterized constructor

  // equals
  CHECK(v2.equals(vec3i(1, 2, 3)));  // equals

  // copyTo/copyFrom
  vec3i v3;
  v2.copyTo(v3);
  CHECK(v3.x == 1);  // copyTo
  CHECK(v3.y == 2);  // copyTo
  CHECK(v3.z == 3);  // copyTo

  vec3i v4;
  v4.copyFrom(v2);
  CHECK(v4.x == 1);  // copyFrom
  CHECK(v4.y == 2);  // copyFrom
  CHECK(v4.z == 3);  // copyFrom

  // add
  vec3i v5 = v2.add(vec3i(4, 5, 6));
  CHECK(v5.x == 5);  // add
  CHECK(v5.y == 7);  // add
  CHECK(v5.z == 9);  // add

  // subtract
  vec3i v6 = v2.subtract(vec3i(1, 1, 1));
  CHECK(v6.x == 0);  // subtract
  CHECK(v6.y == 1);  // subtract
  CHECK(v6.z == 2);  // subtract

  // zero
  CHECK(vec3i().zero());  // zero
  CHECK(!vec3i(0, 0, 1).zero());  // zero

  // 原 tests/test_math 的 vec3i 加减: 保留其操作数组合
  vec3i a(1, 2, 3);
  vec3i b(4, 5, 6);
  CHECK(a.add(b).x == 5);
  CHECK(a.subtract(b).y == -3);
}

TEST_CASE("vec4i: 构造/equals/copy/zero") {
  vec4i v1;
  CHECK(v1.x == 0);  // default constructor
  CHECK(v1.y == 0);  // default constructor
  CHECK(v1.z == 0);  // default constructor
  CHECK(v1.w == 0);  // default constructor

  vec4i v2(1, 2, 3, 4);
  CHECK(v2.x == 1);  // parameterized constructor
  CHECK(v2.y == 2);  // parameterized constructor
  CHECK(v2.z == 3);  // parameterized constructor
  CHECK(v2.w == 4);  // parameterized constructor

  // equals
  CHECK(v2.equals(vec4i(1, 2, 3, 4)));  // equals

  // copyTo/copyFrom
  vec4i v3;
  v2.copyTo(v3);
  CHECK(v3.x == 1);  // copyTo
  CHECK(v3.y == 2);  // copyTo
  CHECK(v3.z == 3);  // copyTo
  CHECK(v3.w == 4);  // copyTo

  vec4i v4;
  v4.copyFrom(v2);
  CHECK(v4.x == 1);  // copyFrom
  CHECK(v4.y == 2);  // copyFrom
  CHECK(v4.z == 3);  // copyFrom
  CHECK(v4.w == 4);  // copyFrom

  // zero
  CHECK(vec4i().zero());  // zero
  CHECK(!vec4i(0, 0, 0, 1).zero());  // zero
}

// ============================================================================
// 测试浮点向量
// ============================================================================
TEST_CASE("vec2f: 构造/等号/加减/lenght/zero") {
  vec2f v1;
  CHECK(v1.x == 0.0f);  // default constructor
  CHECK(v1.y == 0.0f);  // default constructor

  vec2f v2(1.5f, 2.5f);
  CHECK(v2.x == 1.5f);  // parameterized constructor
  CHECK(v2.y == 2.5f);  // parameterized constructor

  // operator==
  CHECK(v2 == vec2f(1.5f, 2.5f));  // operator==

  // equals
  CHECK(v2.equals(vec2f(1.5f, 2.5f)));  // equals

  // copyTo/copyFrom
  vec2f v3;
  v2.copyTo(v3);
  CHECK(v3.x == 1.5f);  // copyTo
  CHECK(v3.y == 2.5f);  // copyTo

  vec2f v4;
  v4.copyFrom(v2);
  CHECK(v4.x == 1.5f);  // copyFrom
  CHECK(v4.y == 2.5f);  // copyFrom

  // add
  vec2f v5 = v2.add(vec2f(0.5f, 0.5f));
  CHECK(v5.x == 2.0f);  // add
  CHECK(v5.y == 3.0f);  // add

  // subtract
  vec2f v6 = v2.subtract(vec2f(0.5f, 0.5f));
  CHECK(v6.x == 1.0f);  // subtract
  CHECK(v6.y == 2.0f);  // subtract

  // lenght
  float dist = vec2f(0, 0).lenght(vec2f(3, 4));
  CHECK(almostEqual(dist, 5.0f));  // lenght

  // zero
  CHECK(vec2f().zero());  // zero
  CHECK(!vec2f(0.1f, 0).zero());  // zero
}

TEST_CASE("vec2d: 构造/等号/加减/lenght/zero") {
  vec2d v1;
  CHECK(v1.x == 0.0);  // default constructor
  CHECK(v1.y == 0.0);  // default constructor

  vec2d v2(1.5, 2.5);
  CHECK(v2.x == 1.5);  // parameterized constructor
  CHECK(v2.y == 2.5);  // parameterized constructor

  // operator==
  CHECK(v2 == vec2d(1.5, 2.5));  // operator==

  // add
  vec2d v3 = v2.add(vec2d(0.5, 0.5));
  CHECK(v3.x == 2.0);  // add
  CHECK(v3.y == 3.0);  // add

  // subtract
  vec2d v4 = v2.subtract(vec2d(0.5, 0.5));
  CHECK(v4.x == 1.0);  // subtract
  CHECK(v4.y == 2.0);  // subtract

  // lenght
  double dist = vec2d(0, 0).lenght(vec2d(3, 4));
  CHECK(almostEqual(dist, 5.0));  // lenght

  // zero
  CHECK(vec2d().zero());  // zero
  CHECK(!vec2d(0.1, 0).zero());  // zero
}

TEST_CASE("vec3f: 构造/加减/点叉积/scale/normalize/transform3-4/toMat") {
  vec3f v1;
  CHECK(v1.x == 0.0f);  // default constructor
  CHECK(v1.y == 0.0f);  // default constructor
  CHECK(v1.z == 0.0f);  // default constructor

  vec3f v2(1.0f, 2.0f, 3.0f);
  CHECK(v2.x == 1.0f);  // parameterized constructor
  CHECK(v2.y == 2.0f);  // parameterized constructor
  CHECK(v2.z == 3.0f);  // parameterized constructor

  // operator==
  CHECK(v2 == vec3f(1.0f, 2.0f, 3.0f));  // operator==

  // equals
  CHECK(v2.equals(vec3f(1.0f, 2.0f, 3.0f)));  // equals

  // copyTo/copyFrom
  vec3f v3;
  v2.copyTo(v3);
  CHECK(v3.x == 1.0f);  // copyTo
  CHECK(v3.y == 2.0f);  // copyTo
  CHECK(v3.z == 3.0f);  // copyTo

  vec3f v4;
  v4.copyFrom(v2);
  CHECK(v4.x == 1.0f);  // copyFrom
  CHECK(v4.y == 2.0f);  // copyFrom
  CHECK(v4.z == 3.0f);  // copyFrom

  // add/subtract
  vec3f v5 = v2.add(vec3f(1, 1, 1));
  CHECK(v5.x == 2.0f);  // add
  CHECK(v5.y == 3.0f);  // add
  CHECK(v5.z == 4.0f);  // add

  vec3f v6 = v2.subtract(vec3f(1, 1, 1));
  CHECK(v6.x == 0.0f);  // subtract
  CHECK(v6.y == 1.0f);  // subtract
  CHECK(v6.z == 2.0f);  // subtract

  // lenght
  float dist = vec3f(0, 0, 0).lenght(vec3f(1, 2, 2));
  CHECK(almostEqual(dist, 3.0f));  // lenght

  // zero
  CHECK(vec3f().zero());  // zero

  // cross
  vec3f cx = vec3f(1, 0, 0).cross(vec3f(0, 1, 0));
  CHECK(almostEqual(cx.x, 0.0f));  // cross
  CHECK(almostEqual(cx.y, 0.0f));  // cross
  CHECK(almostEqual(cx.z, 1.0f));  // cross

  // dot
  float dot = vec3f(1, 0, 0).dot(vec3f(0, 1, 0));
  CHECK(dot == 0.0f);  // dot

  // scale
  vec3f v7(1, 2, 3);
  v7.scale(2.0f);
  CHECK(v7.x == 2.0f);  // scale
  CHECK(v7.y == 4.0f);  // scale
  CHECK(v7.z == 6.0f);  // scale

  // normalize
  vec3f v8(3, 4, 0);
  float len = v8.normalize();
  CHECK(almostEqual(len, 5.0f));  // normalize
  CHECK(almostEqual(v8.x, 0.6f));  // normalize
  CHECK(almostEqual(v8.y, 0.8f));  // normalize

  // transform3
  Mat3x3f rotZ = makeRotatorZf(90.0f);
  vec3f v9(1, 0, 0);
  vec3f v10 = v9.transform3(rotZ);
  CHECK(almostEqual(v10.x, 0.0f, 1e-4f));  // transform3
  CHECK(almostEqual(v10.y, 1.0f, 1e-4f));  // transform3

  // transform4
  Mat4x4f trans = identMat4x4f();
  trans.row3 = vec4f(1, 2, 3, 1);
  vec3f v11(0, 0, 0);
  vec3f v12 = v11.transform4(trans);
  CHECK(almostEqual(v12.x, 1.0f));  // transform4
  CHECK(almostEqual(v12.y, 2.0f));  // transform4
  CHECK(almostEqual(v12.z, 3.0f));  // transform4

  // toMat
  vec3f v13(1, 0, 0);
  Mat3x3f m = v13.toMat(0, 1, 2);
  CHECK(almostEqual(m.row0.x, 1.0f));  // toMat

  // 原 tests/test_math: 自点积为 1, 单位阵下点与向量都不变 (w=1 平移参与 / w=0 只转旋转)
  CHECK(almostEqual(vec3f(1, 0, 0).dot(vec3f(1, 0, 0)), 1.0f));  // dot(self)
  vec3f pt = vec3f(1.0f, 2.0f, 3.0f).transform4(identMat4x4f(), false);  // 点 (w=1)
  CHECK(almostEqual(pt.x, 1.0f));
  CHECK(almostEqual(pt.z, 3.0f));
  vec3f dir = vec3f(1.0f, 2.0f, 3.0f).transform4(identMat4x4f(), true);  // 向量 (w=0)
  CHECK(almostEqual(dir.x, 1.0f));
  CHECK(almostEqual(dir.z, 3.0f));
}

TEST_CASE("vec3d: cross/dot/scale/normalize/transform3/toMat") {
  vec3d v1;
  CHECK(v1.x == 0.0);  // default constructor
  CHECK(v1.y == 0.0);  // default constructor
  CHECK(v1.z == 0.0);  // default constructor

  vec3d v2(1.0, 2.0, 3.0);
  CHECK(v2.x == 1.0);  // parameterized constructor
  CHECK(v2.y == 2.0);  // parameterized constructor
  CHECK(v2.z == 3.0);  // parameterized constructor

  // cross
  vec3d cx = vec3d(1, 0, 0).cross(vec3d(0, 1, 0));
  CHECK(almostEqual(cx.x, 0.0));  // cross
  CHECK(almostEqual(cx.y, 0.0));  // cross
  CHECK(almostEqual(cx.z, 1.0));  // cross

  // dot
  double dot = vec3d(1, 0, 0).dot(vec3d(1, 0, 0));
  CHECK(dot == 1.0);  // dot

  // scale
  vec3d v3(1, 2, 3);
  v3.scale(2.0);
  CHECK(v3.x == 2.0);  // scale
  CHECK(v3.y == 4.0);  // scale
  CHECK(v3.z == 6.0);  // scale

  // normalize
  vec3d v4(3, 4, 0);
  double len = v4.normalize();
  CHECK(almostEqual(len, 5.0));  // normalize

  // transform3
  Mat3x3d rotZ = makeRotatorZd(90.0);
  vec3d v5(1, 0, 0);
  vec3d v6 = v5.transform3(rotZ);
  CHECK(almostEqual(v6.x, 0.0, 1e-8));  // transform3
  CHECK(almostEqual(v6.y, 1.0, 1e-8));  // transform3

  // toMat
  vec3d v7(1, 0, 0);
  Mat3x3d m = v7.toMat(0, 1, 2);
  CHECK(almostEqual(m.row0.x, 1.0));  // toMat
}

TEST_CASE("vec4f: 构造/vec3 互转/四元数/formAxisAngle") {
  vec4f v1;
  CHECK(v1.x == 0.0f);  // default constructor
  CHECK(v1.y == 0.0f);  // default constructor
  CHECK(v1.z == 0.0f);  // default constructor
  CHECK(v1.w == 0.0f);  // default constructor

  vec4f v2(1, 2, 3, 4);
  CHECK(v2.x == 1.0f);  // parameterized constructor
  CHECK(v2.y == 2.0f);  // parameterized constructor
  CHECK(v2.z == 3.0f);  // parameterized constructor
  CHECK(v2.w == 4.0f);  // parameterized constructor

  // vec3f + w constructor
  vec3f v3(1, 2, 3);
  vec4f v4(v3, 4.0f);
  CHECK(v4.x == 1.0f);  // vec3f+w constructor
  CHECK(v4.y == 2.0f);  // vec3f+w constructor
  CHECK(v4.z == 3.0f);  // vec3f+w constructor
  CHECK(v4.w == 4.0f);  // vec3f+w constructor

  // operator==
  CHECK(v2 == vec4f(1, 2, 3, 4));  // operator==

  // copyTo/copyFrom
  vec4f v5;
  v2.copyTo(v5);
  CHECK(v5 == v2);  // copyTo

  vec4f v6;
  v6.copyFrom(v2);
  CHECK(v6 == v2);  // copyFrom

  // copyToVec3
  vec3f v7;
  v2.copyToVec3(v7);
  CHECK(v7.x == 1.0f);  // copyToVec3
  CHECK(v7.y == 2.0f);  // copyToVec3
  CHECK(v7.z == 3.0f);  // copyToVec3

  // copyFromVec3
  vec4f v8;
  v8.copyFromVec3(vec3f(1, 2, 3));
  CHECK(v8.x == 1.0f);  // copyFromVec3
  CHECK(v8.y == 2.0f);  // copyFromVec3
  CHECK(v8.z == 3.0f);  // copyFromVec3
  CHECK(v8.w == 0.0f);  // copyFromVec3

  // copyFromVec3W
  vec4f v9;
  v9.copyFromVec3W(vec3f(1, 2, 3), 4.0f);
  CHECK(v9.x == 1.0f);  // copyFromVec3W
  CHECK(v9.y == 2.0f);  // copyFromVec3W
  CHECK(v9.z == 3.0f);  // copyFromVec3W
  CHECK(v9.w == 4.0f);  // copyFromVec3W

  // add
  vec4f v10 = v2.add(vec4f(1, 1, 1, 1));
  CHECK(v10.x == 2.0f);  // add
  CHECK(v10.y == 3.0f);  // add
  CHECK(v10.z == 4.0f);  // add
  CHECK(v10.w == 5.0f);  // add

  // scale (只缩放x,y,z，不缩放w)
  vec4f v11(1, 2, 3, 4);
  v11.scale(2.0f);
  CHECK(v11.x == 2.0f);  // scale
  CHECK(v11.y == 4.0f);  // scale
  CHECK(v11.z == 6.0f);  // scale
  CHECK(v11.w == 4.0f);  // scale

  // normalize
  vec4f v12(3, 4, 0, 0);
  float len = v12.normalize();
  CHECK(almostEqual(len, 5.0f));  // normalize
  CHECK(almostEqual(v12.x, 0.6f));  // normalize
  CHECK(almostEqual(v12.y, 0.8f));  // normalize

  // toAxisAngle / toQuaternion
  vec4f axisAngle(0, 0, 1, 90);
  vec4f quat = axisAngle.toQuaternion();
  CHECK(almostEqual(quat.x, 0.0f));  // toQuaternion
  CHECK(almostEqual(quat.y, 0.0f));  // toQuaternion
  CHECK(almostEqual(quat.z, 0.707f, 1e-3f));  // toQuaternion

  vec4f axisAngle2 = quat.toAxisAngle();
  CHECK(almostEqual(axisAngle2.z, 1.0f));  // toAxisAngle
  CHECK(almostEqual(axisAngle2.w, 90.0f, 0.1f));  // toAxisAngle

  // formAxisAngle
  Mat3x3f m = axisAngle.formAxisAngle();
  vec3f v13(1, 0, 0);
  vec3f v14 = v13.transform3(m);
  CHECK(almostEqual(v14.y, 1.0f, 1e-4f));  // formAxisAngle

  // formQuaternion
  Mat3x3f m2 = quat.formQuaternion();
  vec3f v15 = v13.transform3(m2);
  CHECK(almostEqual(v15.y, 1.0f, 1e-4f));  // formQuaternion
}

TEST_CASE("vec4d: 构造/四元数互转") {
  vec4d v1;
  CHECK(v1.x == 0.0);  // default constructor
  CHECK(v1.y == 0.0);  // default constructor
  CHECK(v1.z == 0.0);  // default constructor
  CHECK(v1.w == 0.0);  // default constructor

  vec4d v2(1, 2, 3, 4);
  CHECK(v2.x == 1.0);  // parameterized constructor
  CHECK(v2.y == 2.0);  // parameterized constructor
  CHECK(v2.z == 3.0);  // parameterized constructor
  CHECK(v2.w == 4.0);  // parameterized constructor

  // vec3d + w constructor
  vec3d v3(1, 2, 3);
  vec4d v4(v3, 4.0);
  CHECK(v4.x == 1.0);  // vec3d+w constructor
  CHECK(v4.y == 2.0);  // vec3d+w constructor
  CHECK(v4.z == 3.0);  // vec3d+w constructor
  CHECK(v4.w == 4.0);  // vec3d+w constructor

  // cross operations for quaternion
  vec4d axisAngle(0, 0, 1, 90);
  vec4d quat = axisAngle.toQuaternion();
  CHECK(almostEqual(quat.z, 0.707, 1e-3));  // toQuaternion

  vec4d axisAngle2 = quat.toAxisAngle();
  CHECK(almostEqual(axisAngle2.z, 1.0));  // toAxisAngle
  CHECK(almostEqual(axisAngle2.w, 90.0, 0.1));  // toAxisAngle
}

// ============================================================================
// 测试矩阵
// ============================================================================
TEST_CASE("Mat3x3f: 单位阵/inverse/transpose/乘/欧拉角") {
  // 默认构造 - 单位矩阵
  Mat3x3f m1;
  CHECK(m1.row0.x == 1.0f);  // default constructor (identity)
  CHECK(m1.row1.y == 1.0f);  // default constructor (identity)
  CHECK(m1.row2.z == 1.0f);  // default constructor (identity)

  // operator==
  CHECK(m1 == identMat3x3f());  // operator==

  // equals
  CHECK(m1.equals(identMat3x3f()));  // equals

  // copyTo/copyFrom
  Mat3x3f m2;
  m1.copyTo(m2);
  CHECK(m2 == m1);  // copyTo

  Mat3x3f m3;
  m3.copyFrom(m1);
  CHECK(m3 == m1);  // copyFrom

  // valid (检查矩阵是否有非对角线元素，单位矩阵返回false)
  CHECK(!m1.valid());  // valid (identity returns false)
  Mat3x3f m4a;
  m4a.row0 = vec3f(1, 0.1f, 0);
  CHECK(m4a.valid());  // valid (non-diagonal returns true)

  // inverse
  Mat3x3f m4;
  m4.row0 = vec3f(2, 0, 0);
  m4.row1 = vec3f(0, 3, 0);
  m4.row2 = vec3f(0, 0, 4);
  bool success = false;
  Mat3x3f m5 = m4.inverse(&success);
  CHECK(success);  // inverse
  CHECK(almostEqual(m5.row0.x, 0.5f));  // inverse
  CHECK(almostEqual(m5.row1.y, 1.0f/3.0f));  // inverse

  // transpose
  Mat3x3f m6;
  m6.row0 = vec3f(1, 2, 3);
  m6.row1 = vec3f(4, 5, 6);
  m6.row2 = vec3f(7, 8, 9);
  Mat3x3f m7 = m6.transpose();
  CHECK(m7.row0.x == 1);  // transpose
  CHECK(m7.row0.y == 4);  // transpose
  CHECK(m7.row0.z == 7);  // transpose

  // multiply
  Mat3x3f m8 = identMat3x3f();
  Mat3x3f m9 = m8.multiply(m8);
  CHECK(m9 == identMat3x3f());  // multiply

  // toAxisAngle / toQuaternion
  Mat3x3f rotZ = makeRotatorZf(90.0f);
  vec4f aa = rotZ.toAxisAngle();
  CHECK(almostEqual(aa.z, 1.0f));  // toAxisAngle
  CHECK(almostEqual(aa.w, 90.0f, 0.1f));  // toAxisAngle

  vec4f q = rotZ.toQuaternion();
  CHECK(almostEqual(q.z, 0.707f, 1e-3f));  // toQuaternion

  // toEulerAngle
  Mat3x3f rotX = makeRotatorXf(45.0f);
  vec3f euler = rotX.toEulerAngle(0, 1, 2);
  CHECK(almostEqual(euler.x, 45.0f, 0.1f));  // toEulerAngle
}

TEST_CASE("Mat3x3d: 构造/inverse/transpose/toAxisAngle") {
  Mat3x3d m1;
  CHECK(m1.row0.x == 1.0);  // default constructor
  CHECK(m1.row1.y == 1.0);  // default constructor
  CHECK(m1.row2.z == 1.0);  // default constructor

  // inverse
  Mat3x3d m2;
  m2.row0 = vec3d(2, 0, 0);
  m2.row1 = vec3d(0, 3, 0);
  m2.row2 = vec3d(0, 0, 4);
  bool success = false;
  Mat3x3d m3 = m2.inverse(&success);
  CHECK(success);  // inverse
  CHECK(almostEqual(m3.row0.x, 0.5));  // inverse

  // transpose
  Mat3x3d m4;
  m4.row0 = vec3d(1, 2, 3);
  m4.row1 = vec3d(4, 5, 6);
  m4.row2 = vec3d(7, 8, 9);
  Mat3x3d m5 = m4.transpose();
  CHECK(m5.row0.y == 4.0);  // transpose

  // toAxisAngle
  Mat3x3d rotZ = makeRotatorZd(90.0);
  vec4d aa = rotZ.toAxisAngle();
  CHECK(almostEqual(aa.z, 1.0));  // toAxisAngle
  CHECK(almostEqual(aa.w, 90.0, 0.1));  // toAxisAngle
}

TEST_CASE("Mat4x4f: 单位阵/inverse/transpose/位姿/build-split") {
  // 默认构造 - 单位矩阵
  Mat4x4f m1;
  CHECK(m1.row0.x == 1.0f);  // default constructor
  CHECK(m1.row1.y == 1.0f);  // default constructor
  CHECK(m1.row2.z == 1.0f);  // default constructor
  CHECK(m1.row3.w == 1.0f);  // default constructor

  // copyTo/copyFrom
  Mat4x4f m2;
  m1.copyTo(m2);
  CHECK(m2 == m1);  // copyTo

  // valid (检查矩阵是否有非对角线元素或平移，单位矩阵返回false)
  CHECK(!m1.valid());  // valid (identity returns false)
  Mat4x4f m4a;
  m4a.row0 = vec4f(1, 0.1f, 0, 0);
  CHECK(m4a.valid());  // valid (non-diagonal returns true)

  // inverse
  Mat4x4f m3;
  m3.row0 = vec4f(2, 0, 0, 0);
  m3.row1 = vec4f(0, 3, 0, 0);
  m3.row2 = vec4f(0, 0, 4, 0);
  m3.row3 = vec4f(0, 0, 0, 1);
  bool success = false;
  Mat4x4f m4 = m3.inverse(&success);
  CHECK(success);  // inverse
  CHECK(almostEqual(m4.row0.x, 0.5f));  // inverse

  // transpose
  Mat4x4f m5;
  m5.row0 = vec4f(1, 2, 3, 4);
  m5.row1 = vec4f(5, 6, 7, 8);
  m5.row2 = vec4f(9, 10, 11, 12);
  m5.row3 = vec4f(13, 14, 15, 16);
  Mat4x4f m6 = m5.transpose();
  CHECK(m6.row0.y == 5.0f);  // transpose
  CHECK(m6.row0.z == 9.0f);  // transpose

  // multiply
  Mat4x4f m7 = identMat4x4f();
  Mat4x4f m8 = m7.multiply(m7);
  CHECK(m8 == identMat4x4f());  // multiply

  // formTrackPose / toTrackPose
  vec3f pos(1, 2, 3);
  vec4f quat(0, 0, 0, 1);
  Mat4x4f m9;
  m9.formTrackPose(pos, quat);
  CHECK(almostEqual(m9.row3.x, 1.0f));  // formTrackPose
  CHECK(almostEqual(m9.row3.y, 2.0f));  // formTrackPose
  CHECK(almostEqual(m9.row3.z, 3.0f));  // formTrackPose

  vec3f pos2;
  vec4f quat2;
  m9.toTrackPose(pos2, quat2);
  CHECK(almostEqual(pos2.x, 1.0f));  // toTrackPose
  CHECK(almostEqual(pos2.y, 2.0f));  // toTrackPose
  CHECK(almostEqual(pos2.z, 3.0f));  // toTrackPose

  // build / split
  Mat3x3f rot = makeRotatorZf(30.0f);
  vec3f trans(10, 20, 30);
  Mat4x4f m10;
  m10.build(rot, trans);
  CHECK(almostEqual(m10.row3.x, 10.0f));  // build
  CHECK(almostEqual(m10.row3.y, 20.0f));  // build

  Mat3x3f rot2;
  vec3f trans2;
  m10.split(rot2, trans2);
  CHECK(almostEqual(trans2.x, 10.0f));  // split
  CHECK(almostEqual(trans2.y, 20.0f));  // split

  // 原 tests/test_math: 单位阵的逆/转置/乘法不变, 单位旋转下 build/split 往返
  Mat4x4f unit = identMat4x4f();
  CHECK(unit.inverse() == unit);
  CHECK(unit.transpose() == unit);
  CHECK(unit.multiply(unit) == unit);

  Mat3x3f rot0 = identMat3x3f();
  Mat4x4f m11;
  m11.build(rot0, vec3f(1.0f, 2.0f, 3.0f));
  Mat3x3f rotOut;
  vec3f posOut;
  m11.split(rotOut, posOut);
  CHECK(almostEqual(posOut.x, 1.0f));
  CHECK(almostEqual(posOut.y, 2.0f));
  CHECK(almostEqual(posOut.z, 3.0f));
  CHECK(rotOut.equals(rot0));
}

TEST_CASE("Mat4x4d: 构造/inverse/位姿往返") {
  Mat4x4d m1;
  CHECK(m1.row0.x == 1.0);  // default constructor
  CHECK(m1.row1.y == 1.0);  // default constructor
  CHECK(m1.row2.z == 1.0);  // default constructor
  CHECK(m1.row3.w == 1.0);  // default constructor

  // inverse
  Mat4x4d m2;
  m2.row0 = vec4d(2, 0, 0, 0);
  m2.row1 = vec4d(0, 3, 0, 0);
  m2.row2 = vec4d(0, 0, 4, 0);
  m2.row3 = vec4d(0, 0, 0, 1);
  bool success = false;
  Mat4x4d m3 = m2.inverse(&success);
  CHECK(success);  // inverse
  CHECK(almostEqual(m3.row0.x, 0.5));  // inverse

  // formTrackPose / toTrackPose
  vec3d pos(1, 2, 3);
  vec4d quat(0, 0, 0, 1);
  Mat4x4d m4;
  m4.formTrackPose(pos, quat);
  CHECK(almostEqual(m4.row3.x, 1.0));  // formTrackPose

  vec3d pos2;
  vec4d quat2;
  m4.toTrackPose(pos2, quat2);
  CHECK(almostEqual(pos2.x, 1.0));  // toTrackPose
}

// ============================================================================
// 测试坐标系转换
// ============================================================================
TEST_CASE("坐标系转换: UE4/OpenCV/Common/Maya 往返") {
  Mat4x4f identity = identMat4x4f();

  // UE4 <-> OpenCV
  Mat4x4f ue4ToCv = identity.convertUE4ToOpenCV();
  Mat4x4f cvToUe4 = ue4ToCv.convertOpenCVToUE4();
  CHECK(almostEqual(cvToUe4.row0.x, 1.0f));  // UE4 <-> OpenCV roundtrip
  CHECK(almostEqual(cvToUe4.row3.w, 1.0f));  // UE4 <-> OpenCV roundtrip

  // Common <-> OpenCV
  Mat4x4f commonToCv = identity.convertCommonToOpenCV();
  Mat4x4f cvToCommon = commonToCv.convertOpenCVToCommon();
  CHECK(almostEqual(cvToCommon.row0.x, 1.0f));  // Common <-> OpenCV roundtrip

  // Common <-> UE4
  Mat4x4f commonToUe4 = identity.convertCommonToUE4();
  Mat4x4f ue4ToCommon = commonToUe4.convertUE4ToCommon();
  CHECK(almostEqual(ue4ToCommon.row0.x, 1.0f));  // Common <-> UE4 roundtrip

  // UE <-> MayaZUp
  Mat4x4f ueToMaya = identity.convertUEToMayaZUp();
  Mat4x4f mayaToUe = ueToMaya.convertMayaZUpToUE();
  CHECK(almostEqual(mayaToUe.row0.x, 1.0f));  // UE <-> Maya roundtrip
}

// ============================================================================
// 测试工厂函数
// ============================================================================
TEST_CASE("工厂函数: ident/scale/rotator/transform/break") {
  // identMat
  Mat3x3f m3f = identMat3x3f();
  CHECK(m3f.row0.x == 1.0f);  // identMat3x3f
  CHECK(m3f.row1.y == 1.0f);  // identMat3x3f
  CHECK(m3f.row2.z == 1.0f);  // identMat3x3f

  Mat3x3d m3d = identMat3x3d();
  CHECK(m3d.row0.x == 1.0);  // identMat3x3d

  Mat4x4f m4f = identMat4x4f();
  CHECK(m4f.row0.x == 1.0f);  // identMat4x4f
  CHECK(m4f.row3.w == 1.0f);  // identMat4x4f

  Mat4x4d m4d = identMat4x4d();
  CHECK(m4d.row0.x == 1.0);  // identMat4x4d

  // scaleIdentMat
  Mat4x4f scaleF = scaleIdentMatf(vec3f(2, 3, 4));
  CHECK(scaleF.row0.x == 2.0f);  // scaleIdentMatf
  CHECK(scaleF.row1.y == 3.0f);  // scaleIdentMatf
  CHECK(scaleF.row2.z == 4.0f);  // scaleIdentMatf

  Mat4x4d scaleD = scaleIdentMatd(vec3d(2, 3, 4));
  CHECK(scaleD.row0.x == 2.0);  // scaleIdentMatd

  // makeRotatorX/Y/Z
  Mat3x3f rotX = makeRotatorXf(90.0f);
  vec3f v1(0, 1, 0);
  vec3f v2 = v1.transform3(rotX);
  CHECK(almostEqual(v2.z, 1.0f, 1e-4f));  // makeRotatorXf

  Mat3x3f rotY = makeRotatorYf(90.0f);
  vec3f v3(1, 0, 0);
  vec3f v4 = v3.transform3(rotY);
  CHECK(almostEqual(v4.z, -1.0f, 1e-4f));  // makeRotatorYf

  Mat3x3f rotZ = makeRotatorZf(90.0f);
  vec3f v5(1, 0, 0);
  vec3f v6 = v5.transform3(rotZ);
  CHECK(almostEqual(v6.y, 1.0f, 1e-4f));  // makeRotatorZf

  // double versions
  Mat3x3d rotXd = makeRotatorXd(90.0);
  Mat3x3d rotYd = makeRotatorYd(90.0);
  Mat3x3d rotZd = makeRotatorZd(90.0);
  CHECK(rotXd.row0.x == 1.0);  // makeRotatorXd/Yd/Zd
  CHECK(rotYd.row1.y == 1.0);  // makeRotatorXd/Yd/Zd
  CHECK(rotZd.row2.z == 1.0);  // makeRotatorXd/Yd/Zd

  // makeTransform
  Mat3x3f rot = identMat3x3f();
  vec3f trans(10, 20, 30);
  Mat4x4f tf = makeTransformf(rot, trans);
  CHECK(almostEqual(tf.row3.x, 10.0f));  // makeTransformf
  CHECK(almostEqual(tf.row3.y, 20.0f));  // makeTransformf

  Mat4x4f tfScale = makeTransformScalef(rot, trans, vec3f(2, 2, 2));
  CHECK(almostEqual(tfScale.row0.x, 2.0f));  // makeTransformScalef

  // transformBreak
  Mat3x3f rot2;
  vec3f trans2;
  transformBreakf(tf, rot2, trans2);
  CHECK(almostEqual(trans2.x, 10.0f));  // transformBreakf

  Mat3x3f rot3;
  vec3f trans3;
  vec3f scale3;
  transformBreakScalef(tfScale, rot3, trans3, scale3);
  CHECK(almostEqual(scale3.x, 2.0f));  // transformBreakScalef
}

// ============================================================================
// 测试类型转换函数
// ============================================================================
TEST_CASE("类型转换: makeVec3/makeMat4x4/copy*") {
  // makeVec3f/d
  vec4f v4f(1, 2, 3, 4);
  vec3f v3f = makeVec3f(v4f);
  CHECK(v3f.x == 1.0f);  // makeVec3f
  CHECK(v3f.y == 2.0f);  // makeVec3f
  CHECK(v3f.z == 3.0f);  // makeVec3f

  vec4d v4d(1, 2, 3, 4);
  vec3d v3d = makeVec3d(v4d);
  CHECK(v3d.x == 1.0);  // makeVec3d

  // makeMat4x4f/d
  Mat3x3f m3f = identMat3x3f();
  Mat4x4f m4f = makeMat4x4f(m3f);
  CHECK(m4f.row0.x == 1.0f);  // makeMat4x4f
  CHECK(m4f.row3.w == 1.0f);  // makeMat4x4f

  // makeMat3x3f/d
  Mat4x4f m4f2 = identMat4x4f();
  Mat3x3f m3f2 = makeMat3x3f(m4f2);
  CHECK(m3f2.row0.x == 1.0f);  // makeMat3x3f

  // copyVec4ToVec3
  vec4f v1(1, 2, 3, 4);
  vec3f v2;
  copyVec4ToVec3f(v1, v2);
  CHECK(v2.x == 1.0f);  // copyVec4ToVec3f
  CHECK(v2.y == 2.0f);  // copyVec4ToVec3f
  CHECK(v2.z == 3.0f);  // copyVec4ToVec3f

  // copyMat3ToMat4
  Mat3x3f m1 = identMat3x3f();
  Mat4x4f m2;
  copyMat3ToMat4f(m1, m2);
  CHECK(m2.row0.x == 1.0f);  // copyMat3ToMat4f
  CHECK(m2.row3.w == 1.0f);  // copyMat3ToMat4f

  // copyMat3VecToMat4
  Mat3x3f m3 = identMat3x3f();
  vec3f v3(10, 20, 30);
  Mat4x4f m4;
  copyMat3VecToMat4f(m3, v3, m4);
  CHECK(almostEqual(m4.row3.x, 10.0f));  // copyMat3VecToMat4f
}

// ============================================================================
// 测试插值函数
// ============================================================================
TEST_CASE("插值: lerpVec3/slerp 四元数与矩阵") {
  // lerpVec3f
  vec3f v1(0, 0, 0);
  vec3f v2(10, 20, 30);
  vec3f v3 = lerpVec3f(0.5f, v1, v2);
  CHECK(almostEqual(v3.x, 5.0f));  // lerpVec3f
  CHECK(almostEqual(v3.y, 10.0f));  // lerpVec3f
  CHECK(almostEqual(v3.z, 15.0f));  // lerpVec3f

  // lerpVec3d
  vec3d v4(0, 0, 0);
  vec3d v5(10, 20, 30);
  vec3d v6 = lerpVec3d(0.5, v4, v5);
  CHECK(almostEqual(v6.x, 5.0));  // lerpVec3d

  // slerpQuaternionf
  vec4f qa(0, 0, 0, 1);
  vec4f qb(0, 0, 0.707f, 0.707f);  // 90 degree rotation around Z
  vec4f qc = slerpQuaternionf(0.5f, qa, qb);
  CHECK(qc.w > 0);  // slerpQuaternionf
  CHECK(qc.z > 0);  // slerpQuaternionf

  // slerpQuaterniond
  vec4d qd1(0, 0, 0, 1);
  vec4d qd2(0, 0, 0.707, 0.707);
  vec4d qd3 = slerpQuaterniond(0.5, qd1, qd2);
  CHECK(qd3.w > 0);  // slerpQuaterniond

  // slerpMat3x3f
  Mat3x3f ma = identMat3x3f();
  Mat3x3f mb = makeRotatorZf(90.0f);
  Mat3x3f mc = slerpMat3x3f(0.5f, ma, mb);
  vec3f vx(1, 0, 0);
  vec3f vy = vx.transform3(mc);
  CHECK(vy.y > 0);  // slerpMat3x3f
}

// ============================================================================
// 测试工具函数
// ============================================================================
TEST_CASE("工具函数: nearlyEqual/axisVec") {
  // nearlyEqualf
  CHECK(nearlyEqualf(1.0f, 1.0f + 1e-9f));  // nearlyEqualf (small diff)
  CHECK(!nearlyEqualf(1.0f, 1.1f));  // nearlyEqualf (large diff)

  // nearlyEquald
  CHECK(nearlyEquald(1.0, 1.0 + 1e-9));  // nearlyEquald (small diff)
  CHECK(!nearlyEquald(1.0, 1.1));  // nearlyEquald (large diff)

  // axisVecf
  vec3f axX = axisVecf(AxisType::X);
  vec3f axY = axisVecf(AxisType::Y);
  vec3f axZ = axisVecf(AxisType::Z);
  vec3f axXn = axisVecf(AxisType::Xn);
  vec3f axYn = axisVecf(AxisType::Yn);
  vec3f axZn = axisVecf(AxisType::Zn);

  CHECK(axX.x == 1.0f);  // axisVecf X
  CHECK(axX.y == 0.0f);  // axisVecf X
  CHECK(axX.z == 0.0f);  // axisVecf X
  CHECK(axY.y == 1.0f);  // axisVecf Y
  CHECK(axZ.z == 1.0f);  // axisVecf Z
  CHECK(axXn.x == -1.0f);  // axisVecf Xn
  CHECK(axYn.y == -1.0f);  // axisVecf Yn
  CHECK(axZn.z == -1.0f);  // axisVecf Zn

  // axisVecd
  vec3d axXd = axisVecd(AxisType::X);
  CHECK(axXd.x == 1.0);  // axisVecd
}

// ============================================================================
// 测试 trackPose
// ============================================================================
TEST_CASE("trackPosef: Mat4x4 往返") {
  trackPosef pose;
  pose.pos = vec3f(10, 20, 30);
  pose.quat = vec4f(0, 0, 0, 1);

  Mat4x4f mat;
  pose.toMat4x4(mat);
  CHECK(almostEqual(mat.row3.x, 10.0f));  // toMat4x4
  CHECK(almostEqual(mat.row3.y, 20.0f));  // toMat4x4

  trackPosef pose2;
  pose2.fromMat4x4(mat);
  CHECK(almostEqual(pose2.pos.x, 10.0f));  // fromMat4x4

}

TEST_CASE("trackPosed: Mat4x4 往返") {
  trackPosed posed;
  posed.pos = vec3d(10, 20, 30);
  posed.quat = vec4d(0, 0, 0, 1);

  Mat4x4d matd;
  posed.toMat4x4(matd);
  CHECK(almostEqual(matd.row3.x, 10.0));  // toMat4x4

  trackPosed posed2;
  posed2.fromMat4x4(matd);
  CHECK(almostEqual(posed2.pos.x, 10.0));  // fromMat4x4
}
}  // namespace avox
