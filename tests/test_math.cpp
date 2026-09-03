// AvoxMath 单元测试: 向量基运算 / 单位矩阵 / build-split 往返
#include <doctest.h>

#include "Avox.hpp"

namespace avox {

TEST_CASE("vec3f dot/cross 基础运算") {
  vec3f x(1.0f, 0.0f, 0.0f);
  vec3f y(0.0f, 1.0f, 0.0f);

  CHECK(x.dot(y) == doctest::Approx(0.0f));
  CHECK(x.dot(x) == doctest::Approx(1.0f));

  vec3f z = x.cross(y);
  CHECK(z.x == doctest::Approx(0.0f));
  CHECK(z.y == doctest::Approx(0.0f));
  CHECK(z.z == doctest::Approx(1.0f));
}

TEST_CASE("vec3i 加减") {
  vec3i a(1, 2, 3);
  vec3i b(4, 5, 6);
  // 按实际 API: add/subtract 返回新向量
  CHECK(a.add(b).x == 5);
  CHECK(a.subtract(b).y == -3);
}

TEST_CASE("identMat4x4f 单位阵: 逆/转置/乘法不变") {
  Mat4x4f id = identMat4x4f();

  CHECK(id.inverse() == id);
  CHECK(id.transpose() == id);
  CHECK(id.multiply(id) == id);
}

TEST_CASE("transform4: 单位阵下点与向量都不变") {
  Mat4x4f id = identMat4x4f();
  vec3f v(1.0f, 2.0f, 3.0f);

  vec3f p = v.transform4(id, false);  // 点 (w=1)
  CHECK(p.x == doctest::Approx(1.0f));
  CHECK(p.y == doctest::Approx(2.0f));
  CHECK(p.z == doctest::Approx(3.0f));

  vec3f d = v.transform4(id, true);  // 向量 (w=0)
  CHECK(d.x == doctest::Approx(1.0f));
  CHECK(d.z == doctest::Approx(3.0f));
}

TEST_CASE("Mat4x4f build/split 往返: 单位旋转 + 平移") {
  Mat3x3f rot = identMat3x3f();
  vec3f pos(1.0f, 2.0f, 3.0f);

  Mat4x4f m;
  m.build(rot, pos);

  Mat3x3f rotOut;
  vec3f posOut;
  m.split(rotOut, posOut);

  CHECK(posOut.x == doctest::Approx(1.0f));
  CHECK(posOut.y == doctest::Approx(2.0f));
  CHECK(posOut.z == doctest::Approx(3.0f));
  CHECK(rotOut.equals(rot));
}

}  // namespace avox
