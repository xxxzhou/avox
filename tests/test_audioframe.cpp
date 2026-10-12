// AudioFrame::writeBytes 边界契约: Release 下越界写入钳位到剩余空间
// (不再裸 memcpy); Debug 下越界由 assert 拦(设计如此, 钳位分支仅在 Release 可测)
#include <doctest.h>

#include "avox/audio/AudioFrame.hpp"

TEST_CASE("AudioFrame: writeBytes 精确写满与分次追加") {
  avox::AudioFrame f;
  f.setSize(6);
  const uint8_t a[4] = {1, 2, 3, 4};
  const uint8_t b[4] = {5, 6, 7, 8};
  f.writeBytes(a, 4);
  CHECK(f.spaceLeft() == 2);
  f.writeBytes(b, 4);  // 只落 2 字节
  CHECK(f.spaceLeft() == 0);
  CHECK(f.point()[0] == 1);
  CHECK(f.point()[3] == 4);
  CHECK(f.point()[4] == 5);
  CHECK(f.point()[5] == 6);
}

TEST_CASE("AudioFrame: writeBytes 空指针/非正长度被忽略") {
  avox::AudioFrame f;
  f.setSize(2);
  const uint8_t d[2] = {9, 9};
  f.writeBytes(nullptr, 2);
  CHECK(f.spaceLeft() == 2);
  f.writeBytes(d, 0);
  f.writeBytes(d, -1);
  CHECK(f.spaceLeft() == 2);
}

#ifdef NDEBUG
TEST_CASE("AudioFrame: writeBytes 越界钳位不越写(Release)") {
  avox::AudioFrame f;
  f.setSize(4);
  const uint8_t data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  f.writeBytes(data, 10);
  CHECK(f.getSize() == 4);
  CHECK(f.spaceLeft() == 0);
  CHECK(f.point()[0] == 0);
  CHECK(f.point()[3] == 3);
}
#endif
