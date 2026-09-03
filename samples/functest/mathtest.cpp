#include <iostream>
#include <cmath>
#include "avox/AvoxMath.h"

using namespace avox;

// 辅助函数：比较浮点数
bool almostEqual(float a, float b, float eps = 1e-5f) {
    return std::abs(a - b) < eps;
}

bool almostEqual(double a, double b, double eps = 1e-10) {
    return std::abs(a - b) < eps;
}

int testCount = 0;
int passCount = 0;

#define TEST(name) std::cout << "Testing " << name << "..." << std::endl
#define PASS() do { testCount++; passCount++; std::cout << "  [PASS]" << std::endl; } while(0)
#define FAIL(msg) do { testCount++; std::cout << "  [FAIL] " << msg << std::endl; } while(0)

// ============================================================================
// 测试整数向量
// ============================================================================
void testVec2i() {
    TEST("vec2i");

    // 构造函数
    vec2i v1;
    if (v1.x == 0 && v1.y == 0) PASS(); else FAIL("default constructor");

    vec2i v2(3, 4);
    if (v2.x == 3 && v2.y == 4) PASS(); else FAIL("parameterized constructor");

    // equals
    vec2i v3(3, 4);
    if (v2.equals(v3)) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    vec2i v4;
    v2.copyTo(v4);
    if (v4.x == 3 && v4.y == 4) PASS(); else FAIL("copyTo");

    vec2i v5;
    v5.copyFrom(v2);
    if (v5.x == 3 && v5.y == 4) PASS(); else FAIL("copyFrom");

    // add
    vec2i v6 = v2.add(vec2i(1, 2));
    if (v6.x == 4 && v6.y == 6) PASS(); else FAIL("add");

    // subtract
    vec2i v7 = v2.subtract(vec2i(1, 2));
    if (v7.x == 2 && v7.y == 2) PASS(); else FAIL("subtract");

    // lenght (distance between two points)
    int32_t dist = vec2i(0, 0).lenght(vec2i(3, 4));
    if (dist == 5) PASS(); else FAIL("lenght");

    // zero
    if (vec2i().zero() && !vec2i(1, 0).zero()) PASS(); else FAIL("zero");
}

void testVec3i() {
    TEST("vec3i");

    vec3i v1;
    if (v1.x == 0 && v1.y == 0 && v1.z == 0) PASS(); else FAIL("default constructor");

    vec3i v2(1, 2, 3);
    if (v2.x == 1 && v2.y == 2 && v2.z == 3) PASS(); else FAIL("parameterized constructor");

    // equals
    if (v2.equals(vec3i(1, 2, 3))) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    vec3i v3;
    v2.copyTo(v3);
    if (v3.x == 1 && v3.y == 2 && v3.z == 3) PASS(); else FAIL("copyTo");

    vec3i v4;
    v4.copyFrom(v2);
    if (v4.x == 1 && v4.y == 2 && v4.z == 3) PASS(); else FAIL("copyFrom");

    // add
    vec3i v5 = v2.add(vec3i(4, 5, 6));
    if (v5.x == 5 && v5.y == 7 && v5.z == 9) PASS(); else FAIL("add");

    // subtract
    vec3i v6 = v2.subtract(vec3i(1, 1, 1));
    if (v6.x == 0 && v6.y == 1 && v6.z == 2) PASS(); else FAIL("subtract");

    // zero
    if (vec3i().zero() && !vec3i(0, 0, 1).zero()) PASS(); else FAIL("zero");
}

void testVec4i() {
    TEST("vec4i");

    vec4i v1;
    if (v1.x == 0 && v1.y == 0 && v1.z == 0 && v1.w == 0) PASS(); else FAIL("default constructor");

    vec4i v2(1, 2, 3, 4);
    if (v2.x == 1 && v2.y == 2 && v2.z == 3 && v2.w == 4) PASS(); else FAIL("parameterized constructor");

    // equals
    if (v2.equals(vec4i(1, 2, 3, 4))) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    vec4i v3;
    v2.copyTo(v3);
    if (v3.x == 1 && v3.y == 2 && v3.z == 3 && v3.w == 4) PASS(); else FAIL("copyTo");

    vec4i v4;
    v4.copyFrom(v2);
    if (v4.x == 1 && v4.y == 2 && v4.z == 3 && v4.w == 4) PASS(); else FAIL("copyFrom");

    // zero
    if (vec4i().zero() && !vec4i(0, 0, 0, 1).zero()) PASS(); else FAIL("zero");
}

// ============================================================================
// 测试浮点向量
// ============================================================================
void testVec2f() {
    TEST("vec2f");

    vec2f v1;
    if (v1.x == 0.0f && v1.y == 0.0f) PASS(); else FAIL("default constructor");

    vec2f v2(1.5f, 2.5f);
    if (v2.x == 1.5f && v2.y == 2.5f) PASS(); else FAIL("parameterized constructor");

    // operator==
    if (v2 == vec2f(1.5f, 2.5f)) PASS(); else FAIL("operator==");

    // equals
    if (v2.equals(vec2f(1.5f, 2.5f))) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    vec2f v3;
    v2.copyTo(v3);
    if (v3.x == 1.5f && v3.y == 2.5f) PASS(); else FAIL("copyTo");

    vec2f v4;
    v4.copyFrom(v2);
    if (v4.x == 1.5f && v4.y == 2.5f) PASS(); else FAIL("copyFrom");

    // add
    vec2f v5 = v2.add(vec2f(0.5f, 0.5f));
    if (v5.x == 2.0f && v5.y == 3.0f) PASS(); else FAIL("add");

    // subtract
    vec2f v6 = v2.subtract(vec2f(0.5f, 0.5f));
    if (v6.x == 1.0f && v6.y == 2.0f) PASS(); else FAIL("subtract");

    // lenght
    float dist = vec2f(0, 0).lenght(vec2f(3, 4));
    if (almostEqual(dist, 5.0f)) PASS(); else FAIL("lenght");

    // zero
    if (vec2f().zero() && !vec2f(0.1f, 0).zero()) PASS(); else FAIL("zero");
}

void testVec2d() {
    TEST("vec2d");

    vec2d v1;
    if (v1.x == 0.0 && v1.y == 0.0) PASS(); else FAIL("default constructor");

    vec2d v2(1.5, 2.5);
    if (v2.x == 1.5 && v2.y == 2.5) PASS(); else FAIL("parameterized constructor");

    // operator==
    if (v2 == vec2d(1.5, 2.5)) PASS(); else FAIL("operator==");

    // add
    vec2d v3 = v2.add(vec2d(0.5, 0.5));
    if (v3.x == 2.0 && v3.y == 3.0) PASS(); else FAIL("add");

    // subtract
    vec2d v4 = v2.subtract(vec2d(0.5, 0.5));
    if (v4.x == 1.0 && v4.y == 2.0) PASS(); else FAIL("subtract");

    // lenght
    double dist = vec2d(0, 0).lenght(vec2d(3, 4));
    if (almostEqual(dist, 5.0)) PASS(); else FAIL("lenght");

    // zero
    if (vec2d().zero() && !vec2d(0.1, 0).zero()) PASS(); else FAIL("zero");
}

void testVec3f() {
    TEST("vec3f");

    vec3f v1;
    if (v1.x == 0.0f && v1.y == 0.0f && v1.z == 0.0f) PASS(); else FAIL("default constructor");

    vec3f v2(1.0f, 2.0f, 3.0f);
    if (v2.x == 1.0f && v2.y == 2.0f && v2.z == 3.0f) PASS(); else FAIL("parameterized constructor");

    // operator==
    if (v2 == vec3f(1.0f, 2.0f, 3.0f)) PASS(); else FAIL("operator==");

    // equals
    if (v2.equals(vec3f(1.0f, 2.0f, 3.0f))) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    vec3f v3;
    v2.copyTo(v3);
    if (v3.x == 1.0f && v3.y == 2.0f && v3.z == 3.0f) PASS(); else FAIL("copyTo");

    vec3f v4;
    v4.copyFrom(v2);
    if (v4.x == 1.0f && v4.y == 2.0f && v4.z == 3.0f) PASS(); else FAIL("copyFrom");

    // add/subtract
    vec3f v5 = v2.add(vec3f(1, 1, 1));
    if (v5.x == 2.0f && v5.y == 3.0f && v5.z == 4.0f) PASS(); else FAIL("add");

    vec3f v6 = v2.subtract(vec3f(1, 1, 1));
    if (v6.x == 0.0f && v6.y == 1.0f && v6.z == 2.0f) PASS(); else FAIL("subtract");

    // lenght
    float dist = vec3f(0, 0, 0).lenght(vec3f(1, 2, 2));
    if (almostEqual(dist, 3.0f)) PASS(); else FAIL("lenght");

    // zero
    if (vec3f().zero()) PASS(); else FAIL("zero");

    // cross
    vec3f cx = vec3f(1, 0, 0).cross(vec3f(0, 1, 0));
    if (almostEqual(cx.x, 0.0f) && almostEqual(cx.y, 0.0f) && almostEqual(cx.z, 1.0f)) PASS(); else FAIL("cross");

    // dot
    float dot = vec3f(1, 0, 0).dot(vec3f(0, 1, 0));
    if (dot == 0.0f) PASS(); else FAIL("dot");

    // scale
    vec3f v7(1, 2, 3);
    v7.scale(2.0f);
    if (v7.x == 2.0f && v7.y == 4.0f && v7.z == 6.0f) PASS(); else FAIL("scale");

    // normalize
    vec3f v8(3, 4, 0);
    float len = v8.normalize();
    if (almostEqual(len, 5.0f) && almostEqual(v8.x, 0.6f) && almostEqual(v8.y, 0.8f)) PASS(); else FAIL("normalize");

    // transform3
    Mat3x3f rotZ = makeRotatorZf(90.0f);
    vec3f v9(1, 0, 0);
    vec3f v10 = v9.transform3(rotZ);
    if (almostEqual(v10.x, 0.0f, 1e-4f) && almostEqual(v10.y, 1.0f, 1e-4f)) PASS(); else FAIL("transform3");

    // transform4
    Mat4x4f trans = identMat4x4f();
    trans.row3 = vec4f(1, 2, 3, 1);
    vec3f v11(0, 0, 0);
    vec3f v12 = v11.transform4(trans);
    if (almostEqual(v12.x, 1.0f) && almostEqual(v12.y, 2.0f) && almostEqual(v12.z, 3.0f)) PASS(); else FAIL("transform4");

    // toMat
    vec3f v13(1, 0, 0);
    Mat3x3f m = v13.toMat(0, 1, 2);
    if (almostEqual(m.row0.x, 1.0f)) PASS(); else FAIL("toMat");
}

void testVec3d() {
    TEST("vec3d");

    vec3d v1;
    if (v1.x == 0.0 && v1.y == 0.0 && v1.z == 0.0) PASS(); else FAIL("default constructor");

    vec3d v2(1.0, 2.0, 3.0);
    if (v2.x == 1.0 && v2.y == 2.0 && v2.z == 3.0) PASS(); else FAIL("parameterized constructor");

    // cross
    vec3d cx = vec3d(1, 0, 0).cross(vec3d(0, 1, 0));
    if (almostEqual(cx.x, 0.0) && almostEqual(cx.y, 0.0) && almostEqual(cx.z, 1.0)) PASS(); else FAIL("cross");

    // dot
    double dot = vec3d(1, 0, 0).dot(vec3d(1, 0, 0));
    if (dot == 1.0) PASS(); else FAIL("dot");

    // scale
    vec3d v3(1, 2, 3);
    v3.scale(2.0);
    if (v3.x == 2.0 && v3.y == 4.0 && v3.z == 6.0) PASS(); else FAIL("scale");

    // normalize
    vec3d v4(3, 4, 0);
    double len = v4.normalize();
    if (almostEqual(len, 5.0)) PASS(); else FAIL("normalize");

    // transform3
    Mat3x3d rotZ = makeRotatorZd(90.0);
    vec3d v5(1, 0, 0);
    vec3d v6 = v5.transform3(rotZ);
    if (almostEqual(v6.x, 0.0, 1e-8) && almostEqual(v6.y, 1.0, 1e-8)) PASS(); else FAIL("transform3");

    // toMat
    vec3d v7(1, 0, 0);
    Mat3x3d m = v7.toMat(0, 1, 2);
    if (almostEqual(m.row0.x, 1.0)) PASS(); else FAIL("toMat");
}

void testVec4f() {
    TEST("vec4f");

    vec4f v1;
    if (v1.x == 0.0f && v1.y == 0.0f && v1.z == 0.0f && v1.w == 0.0f) PASS(); else FAIL("default constructor");

    vec4f v2(1, 2, 3, 4);
    if (v2.x == 1.0f && v2.y == 2.0f && v2.z == 3.0f && v2.w == 4.0f) PASS(); else FAIL("parameterized constructor");

    // vec3f + w constructor
    vec3f v3(1, 2, 3);
    vec4f v4(v3, 4.0f);
    if (v4.x == 1.0f && v4.y == 2.0f && v4.z == 3.0f && v4.w == 4.0f) PASS(); else FAIL("vec3f+w constructor");

    // operator==
    if (v2 == vec4f(1, 2, 3, 4)) PASS(); else FAIL("operator==");

    // copyTo/copyFrom
    vec4f v5;
    v2.copyTo(v5);
    if (v5 == v2) PASS(); else FAIL("copyTo");

    vec4f v6;
    v6.copyFrom(v2);
    if (v6 == v2) PASS(); else FAIL("copyFrom");

    // copyToVec3
    vec3f v7;
    v2.copyToVec3(v7);
    if (v7.x == 1.0f && v7.y == 2.0f && v7.z == 3.0f) PASS(); else FAIL("copyToVec3");

    // copyFromVec3
    vec4f v8;
    v8.copyFromVec3(vec3f(1, 2, 3));
    if (v8.x == 1.0f && v8.y == 2.0f && v8.z == 3.0f && v8.w == 0.0f) PASS(); else FAIL("copyFromVec3");

    // copyFromVec3W
    vec4f v9;
    v9.copyFromVec3W(vec3f(1, 2, 3), 4.0f);
    if (v9.x == 1.0f && v9.y == 2.0f && v9.z == 3.0f && v9.w == 4.0f) PASS(); else FAIL("copyFromVec3W");

    // add
    vec4f v10 = v2.add(vec4f(1, 1, 1, 1));
    if (v10.x == 2.0f && v10.y == 3.0f && v10.z == 4.0f && v10.w == 5.0f) PASS(); else FAIL("add");

    // scale (只缩放x,y,z，不缩放w)
    vec4f v11(1, 2, 3, 4);
    v11.scale(2.0f);
    if (v11.x == 2.0f && v11.y == 4.0f && v11.z == 6.0f && v11.w == 4.0f) PASS(); else FAIL("scale");

    // normalize
    vec4f v12(3, 4, 0, 0);
    float len = v12.normalize();
    if (almostEqual(len, 5.0f) && almostEqual(v12.x, 0.6f) && almostEqual(v12.y, 0.8f)) PASS(); else FAIL("normalize");

    // toAxisAngle / toQuaternion
    vec4f axisAngle(0, 0, 1, 90);
    vec4f quat = axisAngle.toQuaternion();
    if (almostEqual(quat.x, 0.0f) && almostEqual(quat.y, 0.0f) && almostEqual(quat.z, 0.707f, 1e-3f)) PASS(); else FAIL("toQuaternion");

    vec4f axisAngle2 = quat.toAxisAngle();
    if (almostEqual(axisAngle2.z, 1.0f) && almostEqual(axisAngle2.w, 90.0f, 0.1f)) PASS(); else FAIL("toAxisAngle");

    // formAxisAngle
    Mat3x3f m = axisAngle.formAxisAngle();
    vec3f v13(1, 0, 0);
    vec3f v14 = v13.transform3(m);
    if (almostEqual(v14.y, 1.0f, 1e-4f)) PASS(); else FAIL("formAxisAngle");

    // formQuaternion
    Mat3x3f m2 = quat.formQuaternion();
    vec3f v15 = v13.transform3(m2);
    if (almostEqual(v15.y, 1.0f, 1e-4f)) PASS(); else FAIL("formQuaternion");
}

void testVec4d() {
    TEST("vec4d");

    vec4d v1;
    if (v1.x == 0.0 && v1.y == 0.0 && v1.z == 0.0 && v1.w == 0.0) PASS(); else FAIL("default constructor");

    vec4d v2(1, 2, 3, 4);
    if (v2.x == 1.0 && v2.y == 2.0 && v2.z == 3.0 && v2.w == 4.0) PASS(); else FAIL("parameterized constructor");

    // vec3d + w constructor
    vec3d v3(1, 2, 3);
    vec4d v4(v3, 4.0);
    if (v4.x == 1.0 && v4.y == 2.0 && v4.z == 3.0 && v4.w == 4.0) PASS(); else FAIL("vec3d+w constructor");

    // cross operations for quaternion
    vec4d axisAngle(0, 0, 1, 90);
    vec4d quat = axisAngle.toQuaternion();
    if (almostEqual(quat.z, 0.707, 1e-3)) PASS(); else FAIL("toQuaternion");

    vec4d axisAngle2 = quat.toAxisAngle();
    if (almostEqual(axisAngle2.z, 1.0) && almostEqual(axisAngle2.w, 90.0, 0.1)) PASS(); else FAIL("toAxisAngle");
}

// ============================================================================
// 测试矩阵
// ============================================================================
void testMat3x3f() {
    TEST("Mat3x3f");

    // 默认构造 - 单位矩阵
    Mat3x3f m1;
    if (m1.row0.x == 1.0f && m1.row1.y == 1.0f && m1.row2.z == 1.0f) PASS(); else FAIL("default constructor (identity)");

    // operator==
    if (m1 == identMat3x3f()) PASS(); else FAIL("operator==");

    // equals
    if (m1.equals(identMat3x3f())) PASS(); else FAIL("equals");

    // copyTo/copyFrom
    Mat3x3f m2;
    m1.copyTo(m2);
    if (m2 == m1) PASS(); else FAIL("copyTo");

    Mat3x3f m3;
    m3.copyFrom(m1);
    if (m3 == m1) PASS(); else FAIL("copyFrom");

    // valid (检查矩阵是否有非对角线元素，单位矩阵返回false)
    if (!m1.valid()) PASS(); else FAIL("valid (identity returns false)");
    Mat3x3f m4a;
    m4a.row0 = vec3f(1, 0.1f, 0);
    if (m4a.valid()) PASS(); else FAIL("valid (non-diagonal returns true)");

    // inverse
    Mat3x3f m4;
    m4.row0 = vec3f(2, 0, 0);
    m4.row1 = vec3f(0, 3, 0);
    m4.row2 = vec3f(0, 0, 4);
    bool success = false;
    Mat3x3f m5 = m4.inverse(&success);
    if (success && almostEqual(m5.row0.x, 0.5f) && almostEqual(m5.row1.y, 1.0f/3.0f)) PASS(); else FAIL("inverse");

    // transpose
    Mat3x3f m6;
    m6.row0 = vec3f(1, 2, 3);
    m6.row1 = vec3f(4, 5, 6);
    m6.row2 = vec3f(7, 8, 9);
    Mat3x3f m7 = m6.transpose();
    if (m7.row0.x == 1 && m7.row0.y == 4 && m7.row0.z == 7) PASS(); else FAIL("transpose");

    // multiply
    Mat3x3f m8 = identMat3x3f();
    Mat3x3f m9 = m8.multiply(m8);
    if (m9 == identMat3x3f()) PASS(); else FAIL("multiply");

    // toAxisAngle / toQuaternion
    Mat3x3f rotZ = makeRotatorZf(90.0f);
    vec4f aa = rotZ.toAxisAngle();
    if (almostEqual(aa.z, 1.0f) && almostEqual(aa.w, 90.0f, 0.1f)) PASS(); else FAIL("toAxisAngle");

    vec4f q = rotZ.toQuaternion();
    if (almostEqual(q.z, 0.707f, 1e-3f)) PASS(); else FAIL("toQuaternion");

    // toEulerAngle
    Mat3x3f rotX = makeRotatorXf(45.0f);
    vec3f euler = rotX.toEulerAngle(0, 1, 2);
    if (almostEqual(euler.x, 45.0f, 0.1f)) PASS(); else FAIL("toEulerAngle");
}

void testMat3x3d() {
    TEST("Mat3x3d");

    Mat3x3d m1;
    if (m1.row0.x == 1.0 && m1.row1.y == 1.0 && m1.row2.z == 1.0) PASS(); else FAIL("default constructor");

    // inverse
    Mat3x3d m2;
    m2.row0 = vec3d(2, 0, 0);
    m2.row1 = vec3d(0, 3, 0);
    m2.row2 = vec3d(0, 0, 4);
    bool success = false;
    Mat3x3d m3 = m2.inverse(&success);
    if (success && almostEqual(m3.row0.x, 0.5)) PASS(); else FAIL("inverse");

    // transpose
    Mat3x3d m4;
    m4.row0 = vec3d(1, 2, 3);
    m4.row1 = vec3d(4, 5, 6);
    m4.row2 = vec3d(7, 8, 9);
    Mat3x3d m5 = m4.transpose();
    if (m5.row0.y == 4.0) PASS(); else FAIL("transpose");

    // toAxisAngle
    Mat3x3d rotZ = makeRotatorZd(90.0);
    vec4d aa = rotZ.toAxisAngle();
    if (almostEqual(aa.z, 1.0) && almostEqual(aa.w, 90.0, 0.1)) PASS(); else FAIL("toAxisAngle");
}

void testMat4x4f() {
    TEST("Mat4x4f");

    // 默认构造 - 单位矩阵
    Mat4x4f m1;
    if (m1.row0.x == 1.0f && m1.row1.y == 1.0f && m1.row2.z == 1.0f && m1.row3.w == 1.0f) PASS(); else FAIL("default constructor");

    // copyTo/copyFrom
    Mat4x4f m2;
    m1.copyTo(m2);
    if (m2 == m1) PASS(); else FAIL("copyTo");

    // valid (检查矩阵是否有非对角线元素或平移，单位矩阵返回false)
    if (!m1.valid()) PASS(); else FAIL("valid (identity returns false)");
    Mat4x4f m4a;
    m4a.row0 = vec4f(1, 0.1f, 0, 0);
    if (m4a.valid()) PASS(); else FAIL("valid (non-diagonal returns true)");

    // inverse
    Mat4x4f m3;
    m3.row0 = vec4f(2, 0, 0, 0);
    m3.row1 = vec4f(0, 3, 0, 0);
    m3.row2 = vec4f(0, 0, 4, 0);
    m3.row3 = vec4f(0, 0, 0, 1);
    bool success = false;
    Mat4x4f m4 = m3.inverse(&success);
    if (success && almostEqual(m4.row0.x, 0.5f)) PASS(); else FAIL("inverse");

    // transpose
    Mat4x4f m5;
    m5.row0 = vec4f(1, 2, 3, 4);
    m5.row1 = vec4f(5, 6, 7, 8);
    m5.row2 = vec4f(9, 10, 11, 12);
    m5.row3 = vec4f(13, 14, 15, 16);
    Mat4x4f m6 = m5.transpose();
    if (m6.row0.y == 5.0f && m6.row0.z == 9.0f) PASS(); else FAIL("transpose");

    // multiply
    Mat4x4f m7 = identMat4x4f();
    Mat4x4f m8 = m7.multiply(m7);
    if (m8 == identMat4x4f()) PASS(); else FAIL("multiply");

    // formTrackPose / toTrackPose
    vec3f pos(1, 2, 3);
    vec4f quat(0, 0, 0, 1);
    Mat4x4f m9;
    m9.formTrackPose(pos, quat);
    if (almostEqual(m9.row3.x, 1.0f) && almostEqual(m9.row3.y, 2.0f) && almostEqual(m9.row3.z, 3.0f)) PASS(); else FAIL("formTrackPose");

    vec3f pos2;
    vec4f quat2;
    m9.toTrackPose(pos2, quat2);
    if (almostEqual(pos2.x, 1.0f) && almostEqual(pos2.y, 2.0f) && almostEqual(pos2.z, 3.0f)) PASS(); else FAIL("toTrackPose");

    // build / split
    Mat3x3f rot = makeRotatorZf(30.0f);
    vec3f trans(10, 20, 30);
    Mat4x4f m10;
    m10.build(rot, trans);
    if (almostEqual(m10.row3.x, 10.0f) && almostEqual(m10.row3.y, 20.0f)) PASS(); else FAIL("build");

    Mat3x3f rot2;
    vec3f trans2;
    m10.split(rot2, trans2);
    if (almostEqual(trans2.x, 10.0f) && almostEqual(trans2.y, 20.0f)) PASS(); else FAIL("split");
}

void testMat4x4d() {
    TEST("Mat4x4d");

    Mat4x4d m1;
    if (m1.row0.x == 1.0 && m1.row1.y == 1.0 && m1.row2.z == 1.0 && m1.row3.w == 1.0) PASS(); else FAIL("default constructor");

    // inverse
    Mat4x4d m2;
    m2.row0 = vec4d(2, 0, 0, 0);
    m2.row1 = vec4d(0, 3, 0, 0);
    m2.row2 = vec4d(0, 0, 4, 0);
    m2.row3 = vec4d(0, 0, 0, 1);
    bool success = false;
    Mat4x4d m3 = m2.inverse(&success);
    if (success && almostEqual(m3.row0.x, 0.5)) PASS(); else FAIL("inverse");

    // formTrackPose / toTrackPose
    vec3d pos(1, 2, 3);
    vec4d quat(0, 0, 0, 1);
    Mat4x4d m4;
    m4.formTrackPose(pos, quat);
    if (almostEqual(m4.row3.x, 1.0)) PASS(); else FAIL("formTrackPose");

    vec3d pos2;
    vec4d quat2;
    m4.toTrackPose(pos2, quat2);
    if (almostEqual(pos2.x, 1.0)) PASS(); else FAIL("toTrackPose");
}

// ============================================================================
// 测试坐标系转换
// ============================================================================
void testCoordinateConvert() {
    TEST("Coordinate Conversion");

    Mat4x4f identity = identMat4x4f();

    // UE4 <-> OpenCV
    Mat4x4f ue4ToCv = identity.convertUE4ToOpenCV();
    Mat4x4f cvToUe4 = ue4ToCv.convertOpenCVToUE4();
    if (almostEqual(cvToUe4.row0.x, 1.0f) && almostEqual(cvToUe4.row3.w, 1.0f)) PASS(); else FAIL("UE4 <-> OpenCV roundtrip");

    // Common <-> OpenCV
    Mat4x4f commonToCv = identity.convertCommonToOpenCV();
    Mat4x4f cvToCommon = commonToCv.convertOpenCVToCommon();
    if (almostEqual(cvToCommon.row0.x, 1.0f)) PASS(); else FAIL("Common <-> OpenCV roundtrip");

    // Common <-> UE4
    Mat4x4f commonToUe4 = identity.convertCommonToUE4();
    Mat4x4f ue4ToCommon = commonToUe4.convertUE4ToCommon();
    if (almostEqual(ue4ToCommon.row0.x, 1.0f)) PASS(); else FAIL("Common <-> UE4 roundtrip");

    // UE <-> MayaZUp
    Mat4x4f ueToMaya = identity.convertUEToMayaZUp();
    Mat4x4f mayaToUe = ueToMaya.convertMayaZUpToUE();
    if (almostEqual(mayaToUe.row0.x, 1.0f)) PASS(); else FAIL("UE <-> Maya roundtrip");
}

// ============================================================================
// 测试工厂函数
// ============================================================================
void testFactoryFunctions() {
    TEST("Factory Functions");

    // identMat
    Mat3x3f m3f = identMat3x3f();
    if (m3f.row0.x == 1.0f && m3f.row1.y == 1.0f && m3f.row2.z == 1.0f) PASS(); else FAIL("identMat3x3f");

    Mat3x3d m3d = identMat3x3d();
    if (m3d.row0.x == 1.0) PASS(); else FAIL("identMat3x3d");

    Mat4x4f m4f = identMat4x4f();
    if (m4f.row0.x == 1.0f && m4f.row3.w == 1.0f) PASS(); else FAIL("identMat4x4f");

    Mat4x4d m4d = identMat4x4d();
    if (m4d.row0.x == 1.0) PASS(); else FAIL("identMat4x4d");

    // scaleIdentMat
    Mat4x4f scaleF = scaleIdentMatf(vec3f(2, 3, 4));
    if (scaleF.row0.x == 2.0f && scaleF.row1.y == 3.0f && scaleF.row2.z == 4.0f) PASS(); else FAIL("scaleIdentMatf");

    Mat4x4d scaleD = scaleIdentMatd(vec3d(2, 3, 4));
    if (scaleD.row0.x == 2.0) PASS(); else FAIL("scaleIdentMatd");

    // makeRotatorX/Y/Z
    Mat3x3f rotX = makeRotatorXf(90.0f);
    vec3f v1(0, 1, 0);
    vec3f v2 = v1.transform3(rotX);
    if (almostEqual(v2.z, 1.0f, 1e-4f)) PASS(); else FAIL("makeRotatorXf");

    Mat3x3f rotY = makeRotatorYf(90.0f);
    vec3f v3(1, 0, 0);
    vec3f v4 = v3.transform3(rotY);
    if (almostEqual(v4.z, -1.0f, 1e-4f)) PASS(); else FAIL("makeRotatorYf");

    Mat3x3f rotZ = makeRotatorZf(90.0f);
    vec3f v5(1, 0, 0);
    vec3f v6 = v5.transform3(rotZ);
    if (almostEqual(v6.y, 1.0f, 1e-4f)) PASS(); else FAIL("makeRotatorZf");

    // double versions
    Mat3x3d rotXd = makeRotatorXd(90.0);
    Mat3x3d rotYd = makeRotatorYd(90.0);
    Mat3x3d rotZd = makeRotatorZd(90.0);
    if (rotXd.row0.x == 1.0 && rotYd.row1.y == 1.0 && rotZd.row2.z == 1.0) PASS(); else FAIL("makeRotatorXd/Yd/Zd");

    // makeTransform
    Mat3x3f rot = identMat3x3f();
    vec3f trans(10, 20, 30);
    Mat4x4f tf = makeTransformf(rot, trans);
    if (almostEqual(tf.row3.x, 10.0f) && almostEqual(tf.row3.y, 20.0f)) PASS(); else FAIL("makeTransformf");

    Mat4x4f tfScale = makeTransformScalef(rot, trans, vec3f(2, 2, 2));
    if (almostEqual(tfScale.row0.x, 2.0f)) PASS(); else FAIL("makeTransformScalef");

    // transformBreak
    Mat3x3f rot2;
    vec3f trans2;
    transformBreakf(tf, rot2, trans2);
    if (almostEqual(trans2.x, 10.0f)) PASS(); else FAIL("transformBreakf");

    Mat3x3f rot3;
    vec3f trans3;
    vec3f scale3;
    transformBreakScalef(tfScale, rot3, trans3, scale3);
    if (almostEqual(scale3.x, 2.0f)) PASS(); else FAIL("transformBreakScalef");
}

// ============================================================================
// 测试类型转换函数
// ============================================================================
void testTypeConversion() {
    TEST("Type Conversion");

    // makeVec3f/d
    vec4f v4f(1, 2, 3, 4);
    vec3f v3f = makeVec3f(v4f);
    if (v3f.x == 1.0f && v3f.y == 2.0f && v3f.z == 3.0f) PASS(); else FAIL("makeVec3f");

    vec4d v4d(1, 2, 3, 4);
    vec3d v3d = makeVec3d(v4d);
    if (v3d.x == 1.0) PASS(); else FAIL("makeVec3d");

    // makeMat4x4f/d
    Mat3x3f m3f = identMat3x3f();
    Mat4x4f m4f = makeMat4x4f(m3f);
    if (m4f.row0.x == 1.0f && m4f.row3.w == 1.0f) PASS(); else FAIL("makeMat4x4f");

    // makeMat3x3f/d
    Mat4x4f m4f2 = identMat4x4f();
    Mat3x3f m3f2 = makeMat3x3f(m4f2);
    if (m3f2.row0.x == 1.0f) PASS(); else FAIL("makeMat3x3f");

    // copyVec4ToVec3
    vec4f v1(1, 2, 3, 4);
    vec3f v2;
    copyVec4ToVec3f(v1, v2);
    if (v2.x == 1.0f && v2.y == 2.0f && v2.z == 3.0f) PASS(); else FAIL("copyVec4ToVec3f");

    // copyMat3ToMat4
    Mat3x3f m1 = identMat3x3f();
    Mat4x4f m2;
    copyMat3ToMat4f(m1, m2);
    if (m2.row0.x == 1.0f && m2.row3.w == 1.0f) PASS(); else FAIL("copyMat3ToMat4f");

    // copyMat3VecToMat4
    Mat3x3f m3 = identMat3x3f();
    vec3f v3(10, 20, 30);
    Mat4x4f m4;
    copyMat3VecToMat4f(m3, v3, m4);
    if (almostEqual(m4.row3.x, 10.0f)) PASS(); else FAIL("copyMat3VecToMat4f");
}

// ============================================================================
// 测试插值函数
// ============================================================================
void testInterpolation() {
    TEST("Interpolation");

    // lerpVec3f
    vec3f v1(0, 0, 0);
    vec3f v2(10, 20, 30);
    vec3f v3 = lerpVec3f(0.5f, v1, v2);
    if (almostEqual(v3.x, 5.0f) && almostEqual(v3.y, 10.0f) && almostEqual(v3.z, 15.0f)) PASS(); else FAIL("lerpVec3f");

    // lerpVec3d
    vec3d v4(0, 0, 0);
    vec3d v5(10, 20, 30);
    vec3d v6 = lerpVec3d(0.5, v4, v5);
    if (almostEqual(v6.x, 5.0)) PASS(); else FAIL("lerpVec3d");

    // slerpQuaternionf
    vec4f qa(0, 0, 0, 1);
    vec4f qb(0, 0, 0.707f, 0.707f);  // 90 degree rotation around Z
    vec4f qc = slerpQuaternionf(0.5f, qa, qb);
    if (qc.w > 0 && qc.z > 0) PASS(); else FAIL("slerpQuaternionf");

    // slerpQuaterniond
    vec4d qd1(0, 0, 0, 1);
    vec4d qd2(0, 0, 0.707, 0.707);
    vec4d qd3 = slerpQuaterniond(0.5, qd1, qd2);
    if (qd3.w > 0) PASS(); else FAIL("slerpQuaterniond");

    // slerpMat3x3f
    Mat3x3f ma = identMat3x3f();
    Mat3x3f mb = makeRotatorZf(90.0f);
    Mat3x3f mc = slerpMat3x3f(0.5f, ma, mb);
    vec3f vx(1, 0, 0);
    vec3f vy = vx.transform3(mc);
    if (vy.y > 0) PASS(); else FAIL("slerpMat3x3f");
}

// ============================================================================
// 测试工具函数
// ============================================================================
void testUtilities() {
    TEST("Utilities");

    // nearlyEqualf
    if (nearlyEqualf(1.0f, 1.0f + 1e-9f)) PASS(); else FAIL("nearlyEqualf (small diff)");
    if (!nearlyEqualf(1.0f, 1.1f)) PASS(); else FAIL("nearlyEqualf (large diff)");

    // nearlyEquald
    if (nearlyEquald(1.0, 1.0 + 1e-9)) PASS(); else FAIL("nearlyEquald (small diff)");
    if (!nearlyEquald(1.0, 1.1)) PASS(); else FAIL("nearlyEquald (large diff)");

    // axisVecf
    vec3f axX = axisVecf(AxisType::X);
    vec3f axY = axisVecf(AxisType::Y);
    vec3f axZ = axisVecf(AxisType::Z);
    vec3f axXn = axisVecf(AxisType::Xn);
    vec3f axYn = axisVecf(AxisType::Yn);
    vec3f axZn = axisVecf(AxisType::Zn);

    if (axX.x == 1.0f && axX.y == 0.0f && axX.z == 0.0f) PASS(); else FAIL("axisVecf X");
    if (axY.y == 1.0f) PASS(); else FAIL("axisVecf Y");
    if (axZ.z == 1.0f) PASS(); else FAIL("axisVecf Z");
    if (axXn.x == -1.0f) PASS(); else FAIL("axisVecf Xn");
    if (axYn.y == -1.0f) PASS(); else FAIL("axisVecf Yn");
    if (axZn.z == -1.0f) PASS(); else FAIL("axisVecf Zn");

    // axisVecd
    vec3d axXd = axisVecd(AxisType::X);
    if (axXd.x == 1.0) PASS(); else FAIL("axisVecd");
}

// ============================================================================
// 测试 trackPose
// ============================================================================
void testTrackPose() {
    TEST("trackPosef");

    trackPosef pose;
    pose.pos = vec3f(10, 20, 30);
    pose.quat = vec4f(0, 0, 0, 1);

    Mat4x4f mat;
    pose.toMat4x4(mat);
    if (almostEqual(mat.row3.x, 10.0f) && almostEqual(mat.row3.y, 20.0f)) PASS(); else FAIL("toMat4x4");

    trackPosef pose2;
    pose2.fromMat4x4(mat);
    if (almostEqual(pose2.pos.x, 10.0f)) PASS(); else FAIL("fromMat4x4");

    TEST("trackPosed");

    trackPosed posed;
    posed.pos = vec3d(10, 20, 30);
    posed.quat = vec4d(0, 0, 0, 1);

    Mat4x4d matd;
    posed.toMat4x4(matd);
    if (almostEqual(matd.row3.x, 10.0)) PASS(); else FAIL("toMat4x4");

    trackPosed posed2;
    posed2.fromMat4x4(matd);
    if (almostEqual(posed2.pos.x, 10.0)) PASS(); else FAIL("fromMat4x4");
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "          AvoxMath Complete Test Suite        " << std::endl;
    std::cout << "============================================" << std::endl << std::endl;

    // 整数向量
    testVec2i();
    testVec3i();
    testVec4i();

    // 浮点向量
    testVec2f();
    testVec2d();
    testVec3f();
    testVec3d();
    testVec4f();
    testVec4d();

    // 矩阵
    testMat3x3f();
    testMat3x3d();
    testMat4x4f();
    testMat4x4d();

    // 坐标转换
    testCoordinateConvert();

    // 工厂函数
    testFactoryFunctions();

    // 类型转换
    testTypeConversion();

    // 插值
    testInterpolation();

    // 工具函数
    testUtilities();

    // 位姿
    testTrackPose();

    std::cout << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Tests passed: " << passCount << " / " << testCount << std::endl;
    std::cout << "============================================" << std::endl;

    return (passCount == testCount) ? 0 : 1;
}
