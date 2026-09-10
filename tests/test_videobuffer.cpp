// SwVideoBuffer form/to 布局契约单测
// 契约: IImageBuffer 恒 packed (yuv420P 的 UV 物理行为 [偶 uvW | 奇 uvW | pad]),
//       YUVFrame 恒 split (逻辑行等距, stride[1] = rowPitch/2)
// 锁两个已修 bug:
//   ① form 引用带 padding 的 flat 420P 帧 → GPU 按 packed 错读奇数行 (竖条纹)
//   ② to() 产出 packed 视图 → ffmpeg/逐行读消费者奇数行左移
#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "avox/video/VideoBuffer.hpp"

namespace avox {

namespace {

// flat 连续的 420P 帧: [Y: h*stride0] [U: uvH*stride1] [V: uvH*stride1]
// w=6 h=4: uvW=3, uvH=2。uvWidthDiv=2/uvHeightDiv=2 满足 bTightlyPacked 的
// 步长比 (stride1 == stride0/2) 与地址连续性 (uSize == ySize/4)
struct FlatFrame {
  std::vector<uint8_t> mem;
  YUVFrame frame = {};
};

FlatFrame makeFlat420P(int stride0, bool bSeparatePlanes = false) {
  // stride1 严格等于 stride0/2 (bTightlyPacked 的硬性比例)
  const int w = 6, h = 4, uvH = 2;
  const int stride1 = stride0 / 2;
  FlatFrame ff;
  const int ySize = stride0 * h, uSize = stride1 * uvH;
  if (bSeparatePlanes) {
    // 三平面分离 (ffmpeg av_frame_get_buffer 风格): 地址不连续
    ff.mem.resize(ySize + 8 + 2 * uSize);
    uint8_t* u = ff.mem.data() + ySize + 8;  // 人为错开, 破坏连续性
    ff.frame.data[0] = ff.mem.data();
    ff.frame.data[1] = u;
    ff.frame.data[2] = u + uSize;
  } else {
    ff.mem.resize(ySize + 2 * uSize);
    ff.frame.data[0] = ff.mem.data();
    ff.frame.data[1] = ff.mem.data() + ySize;
    ff.frame.data[2] = ff.mem.data() + ySize + uSize;
  }
  ff.frame.stride[0] = stride0;
  ff.frame.stride[1] = stride1;
  ff.frame.stride[2] = stride1;
  ff.frame.format = {w, h, YuvType::yuv420P};
  // pattern: Y 全 0x80; U 逻辑行 0/1 = 0x11/0x22; V 逻辑行 0/1 = 0x33/0x44
  std::memset(ff.frame.data[0], 0x80, ySize);
  std::memset(ff.frame.data[1], 0x11, stride1);
  std::memset(ff.frame.data[1] + stride1, 0x22, stride1);
  std::memset(ff.frame.data[2], 0x33, stride1);
  std::memset(ff.frame.data[2] + stride1, 0x44, stride1);
  return ff;
}

// nv12 flat 连续: [Y: h*stride] [UV 交织: (h/2)*stride]
FlatFrame makeFlatNv12(int stride0) {
  const int w = 6, h = 4;
  FlatFrame ff;
  ff.mem.resize(stride0 * h + stride0 * (h / 2));
  ff.frame.data[0] = ff.mem.data();
  ff.frame.data[1] = ff.mem.data() + stride0 * h;
  ff.frame.data[2] = nullptr;
  ff.frame.stride[0] = stride0;
  ff.frame.stride[1] = stride0;
  ff.frame.format = {w, h, YuvType::nv12};
  std::memset(ff.mem.data(), 0x80, ff.mem.size());
  return ff;
}

}  // namespace

TEST_CASE("form: 带padding的flat 420P强制拷贝成packed, 奇数UV行在+uvW处") {
  FlatFrame ff = makeFlat420P(8);  // w=6 带 2 字节 padding, 连续
  SwVideoBuffer buf;
  buf.form(ff.frame, false);
  // 核心: 不允许引用 (引用会让 GPU 按 packed 错读)
  CHECK_FALSE(buf.bDataRef());
  CHECK(buf.getYuvType() == YuvType::yuv420P);
  const ImageFormat fmt = buf.getImageFormat();
  CHECK(fmt.width == 6);
  CHECK(fmt.rowPitch == 8);
  const uint8_t* p = buf.getPointer();
  // Y 行 (含源 padding 一起拷)
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(p + 8 * i, ff.mem.data() + 8 * i, 8) == 0);
  }
  // packed UV: 1 物理行 = [偶(3) | 奇(3) | pad(2)], 奇行在 +uvW=3 处
  const uint8_t* uv = p + 8 * 4;
  CHECK(uv[0] == 0x11);
  CHECK(uv[2] == 0x11);
  CHECK(uv[3] == 0x22);
  CHECK(uv[5] == 0x22);
  const uint8_t* v = uv + 8;  // V 物理段 = U 物理段 + (uvH/2)*rowPitch
  CHECK(v[0] == 0x33);
  CHECK(v[3] == 0x44);
}

TEST_CASE("form: 紧凑flat 420P直接引用不拷贝") {
  FlatFrame ff = makeFlat420P(6);  // stride == width, 无 padding
  SwVideoBuffer buf;
  buf.form(ff.frame, false);
  CHECK(buf.bDataRef());
  CHECK(buf.getPointer() == ff.frame.data[0]);
}

TEST_CASE("form: nv12带padding仍可引用 (split与packed字节重合)") {
  FlatFrame ff = makeFlatNv12(8);
  SwVideoBuffer buf;
  buf.form(ff.frame, false);
  CHECK(buf.bDataRef());
  CHECK(buf.getPointer() == ff.frame.data[0]);
}

TEST_CASE("form: 三平面分离的420P走拷贝") {
  FlatFrame ff = makeFlat420P(6, true);
  SwVideoBuffer buf;
  buf.form(ff.frame, false);
  CHECK_FALSE(buf.bDataRef());
}

TEST_CASE("to: 紧凑buffer零拷贝产出split视图") {
  FlatFrame ff = makeFlat420P(6);
  SwVideoBuffer buf;
  buf.form(ff.frame, true);  // 拷成 packed (== 源, 无 padding)
  YUVFrame out = {};
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  CHECK(out.data[0] == buf.getPointer());  // 视图指向自身, 无副本
  CHECK(out.stride[0] == 6);
  CHECK(out.stride[1] == 3);
  CHECK(out.stride[2] == 3);
  CHECK(out.data[1] == buf.getPointer() + 6 * 4);
  CHECK(out.data[2] == buf.getPointer() + 6 * 4 + 6);
}

TEST_CASE("to: 带padding必须给tmp, split奇数行等距可读") {
  FlatFrame ff = makeFlat420P(8);
  SwVideoBuffer buf;
  buf.form(ff.frame, true);
  YUVFrame out = {};
  // 无 tmp 且需要重排 → 拒绝
  CHECK_FALSE(buf.to(out));
  // 有 tmp → split 逻辑行等距 (stripe 回归锁): stride[1] = rowPitch/2 = 4,
  // 逻辑行 0/1 从行首逐字节读, 奇数行不再左移
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  CHECK(out.stride[0] == 8);
  CHECK(out.stride[1] == 4);
  CHECK(out.stride[2] == 4);
  CHECK(out.data[1][0] == 0x11);
  CHECK(out.data[1][2] == 0x11);
  CHECK(out.data[1][4] == 0x22);  // 逻辑行 1 在 4 处 (重排后)
  CHECK(out.data[1][6] == 0x22);
  CHECK(out.data[2][0] == 0x33);
  CHECK(out.data[2][4] == 0x44);
  // Y 逐字节等于源
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(out.data[0] + 8 * i, ff.mem.data() + 8 * i, 8) == 0);
  }
  // 原 buffer 未被修改 (packed 奇行仍在 +3)
  CHECK(buf.getPointer()[8 * 4 + 3] == 0x22);
}

TEST_CASE("to: form-to往返还原全部逻辑字节") {
  FlatFrame ff = makeFlat420P(8);
  SwVideoBuffer buf;
  buf.form(ff.frame, true);
  YUVFrame out = {};
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  // Y
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(out.data[0] + i * out.stride[0],
                      ff.frame.data[0] + i * ff.frame.stride[0], 6) == 0);
  }
  // U/V 逻辑行
  for (int p = 0; p < 2; ++p) {
    for (int lu = 0; lu < 2; ++lu) {
      CHECK(std::memcmp(out.data[p + 1] + lu * out.stride[1],
                        ff.frame.data[p + 1] + lu * ff.frame.stride[1], 3) ==
            0);
    }
  }
}

TEST_CASE("to: nv12带padding零拷贝视图") {
  FlatFrame ff = makeFlatNv12(8);
  SwVideoBuffer buf;
  buf.form(ff.frame, false);
  YUVFrame out = {};
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  CHECK(out.data[0] == buf.getPointer());  // nv12 无二合一打包, 布局重合
  CHECK(out.stride[0] == 8);
  CHECK(out.stride[1] == 8);
  CHECK(out.data[1] == buf.getPointer() + 8 * 4);
}

}  // namespace avox
