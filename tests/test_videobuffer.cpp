// SwVideoBuffer form/to 布局契约单测
// 契约: IImageBuffer 恒 packed (色度按 width 宽纹理线性摆放, 与 shader
//       yuv2rgbaV1/rgba2yuvV1 的线性寻址互逆; 奇数色度行时 V 平面起点在半行上),
//       YUVFrame 恒 split (tight rowPitch=width, U/V 连续平面, 逻辑行等距)
// 锁已修 bug:
//   ① form 引用带 padding 的 flat 420P 帧 → GPU 按 packed 错读奇数行 (竖条纹)
//   ② 旧打包按物理行成对摆放且 physRows=uvH/2 丢末行 → 720x406 底部 4 行绿
//     (shader 行对齐 V 基址差半行 + split 视图 V 基址错一条 stride 行, 双缺陷)
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

TEST_CASE("to: 带padding必须给tmp, split紧缩成tight等距可读") {
  FlatFrame ff = makeFlat420P(8);
  SwVideoBuffer buf;
  buf.form(ff.frame, true);
  YUVFrame out = {};
  // 无 tmp 且需要重排 → 拒绝
  CHECK_FALSE(buf.to(out));
  // 有 tmp → unpack 紧缩为 rowPitch=width 的连续 split: stride = width/2 = 3,
  // U/V 各占 uvSize 字节连续摆放, 逻辑行等距
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  CHECK(out.stride[0] == 6);
  CHECK(out.stride[1] == 3);
  CHECK(out.stride[2] == 3);
  CHECK(out.data[1][0] == 0x11);
  CHECK(out.data[1][2] == 0x11);
  CHECK(out.data[1][3] == 0x22);  // 逻辑行 1 紧随 (tight, 无 pad 夹层)
  CHECK(out.data[1][5] == 0x22);
  CHECK(out.data[2][0] == 0x33);
  CHECK(out.data[2][3] == 0x44);
  // Y 逐字节等于源
  for (int i = 0; i < 4; ++i) {
    CHECK(std::memcmp(out.data[0] + 6 * i, ff.mem.data() + 8 * i, 6) == 0);
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

namespace {

// 奇数色度行 flat 420P: w=8 h=6 → uvW=4, uvH=3 (奇), stride1 = stride0/2
struct OddFlatFrame {
  std::vector<uint8_t> mem;
  YUVFrame frame = {};
};

OddFlatFrame makeOddFlat420P(int stride0) {
  const int w = 8, h = 6, uvH = 3;
  const int stride1 = stride0 / 2;
  OddFlatFrame ff;
  const int ySize = stride0 * h, uSize = stride1 * uvH;
  ff.mem.resize(ySize + 2 * uSize);
  ff.frame.data[0] = ff.mem.data();
  ff.frame.data[1] = ff.mem.data() + ySize;
  ff.frame.data[2] = ff.frame.data[1] + uSize;
  ff.frame.stride[0] = stride0;
  ff.frame.stride[1] = stride1;
  ff.frame.stride[2] = stride1;
  ff.frame.format = {w, h, YuvType::yuv420P};
  std::memset(ff.frame.data[0], 0x80, ySize);
  // U 逻辑行 0/1/2 = 0x11/0x22/0x33, V 逻辑行 0/1/2 = 0x44/0x55/0x66
  const uint8_t uPat[3] = {0x11, 0x22, 0x33};
  const uint8_t vPat[3] = {0x44, 0x55, 0x66};
  for (int r = 0; r < uvH; ++r) {
    std::memset(ff.frame.data[1] + r * stride1, uPat[r], stride1);
    std::memset(ff.frame.data[2] + r * stride1, vPat[r], stride1);
  }
  return ff;
}

}  // namespace

TEST_CASE("form: 奇数色度行packed按线性布局, 末行不丢") {
  OddFlatFrame ff = makeOddFlat420P(12);  // w=8 带 4 字节 padding
  SwVideoBuffer buf;
  buf.form(ff.frame, true);
  const ImageFormat fmt = buf.getImageFormat();
  CHECK(fmt.width == 8);
  CHECK(fmt.height == 9);  // 6*3/2
  CHECK(fmt.rowPitch == 12);
  // shader 线性寻址镜像: lin = h*w + p*uvSize + r*uvW, 字节 = lin/w*pitch + lin%w
  // U 行落在 72/76/84, V 行落在 88/96/100 (V 起点 88 在半行上)
  const uint8_t* p = buf.getPointer();
  const int lin2byte[2][3] = {{72, 76, 84}, {88, 96, 100}};
  const uint8_t pat[2][3] = {{0x11, 0x22, 0x33}, {0x44, 0x55, 0x66}};
  for (int p1 = 0; p1 < 2; ++p1) {
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 4; ++c) {
        CHECK(p[lin2byte[p1][r] + c] == pat[p1][r]);
      }
    }
  }
  CHECK(p[104] == 0);  // V 末行后仅余尾 padding
}

TEST_CASE("to: 奇数色度行split视图含末行, V基址=uvSize") {
  OddFlatFrame ff = makeOddFlat420P(12);
  SwVideoBuffer buf;
  buf.form(ff.frame, true);
  YUVFrame out = {};
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  CHECK(tmp.getImageFormat().rowPitch == 8);  // unpack 紧缩为 tight
  CHECK(out.stride[0] == 8);
  CHECK(out.stride[1] == 4);
  CHECK(out.stride[2] == 4);
  CHECK(out.data[2] - out.data[1] == 4 * 3);  // V 基址 = uvSize, 不再错一条行距
  // 全部 3 条逻辑行逐字节还原 (含旧实现丢失的末行 → 4行绿回归锁)
  for (int r = 0; r < 3; ++r) {
    CHECK(std::memcmp(out.data[1] + r * 4, ff.frame.data[1] + r * 6, 4) == 0);
    CHECK(std::memcmp(out.data[2] + r * 4, ff.frame.data[2] + r * 6, 4) == 0);
  }
}

TEST_CASE("to: 422P奇高紧排split视图V基址正确") {
  const int w = 8, h = 5, uvH = 5;
  std::vector<uint8_t> mem(8 * h + 2 * (4 * uvH));
  YUVFrame frame = {};
  frame.data[0] = mem.data();
  frame.data[1] = mem.data() + 8 * h;
  frame.data[2] = frame.data[1] + 4 * uvH;
  frame.stride[0] = 8;
  frame.stride[1] = 4;
  frame.stride[2] = 4;
  frame.format = {w, h, YuvType::yuv422P};
  std::memset(frame.data[0], 0x80, 8 * h);
  std::memset(frame.data[1], 0x11, 4 * uvH);
  std::memset(frame.data[2], 0x55, 4 * uvH);
  SwVideoBuffer buf;
  buf.form(frame, false);  // 紧排连续 → 引用
  CHECK(buf.bDataRef());
  YUVFrame out = {};
  ImageBuffer tmp;
  REQUIRE(buf.to(out, &tmp));
  // V 基址 = uvPitch*uvHeight = 4*5 = 20, 与实际平面落点重合
  CHECK(out.data[1] == buf.getPointer() + 8 * h);
  CHECK(out.data[2] == out.data[1] + 4 * uvH);
  CHECK(out.data[2][0] == 0x55);
  CHECK(out.data[2][4 * uvH - 1] == 0x55);
}

}  // namespace avox

// P010 打包归一化契约: 高位对齐(>>6) + UV 交错拆分, 产出与 yuv420P10 相同的紧排布局
// (shader 读函数零改动的前提; 硬解 10bit 的 CPU 下载帧即此格式)
namespace avox {

TEST_CASE("p010: 归一化打包 — 右移对齐低10位 + UV拆分, bTightlyPacked恒false") {
  const int w = 4, h = 4, uvH = 2;
  std::vector<uint16_t> ySrc(w * h), uvSrc(uvH * w);
  for (int i = 0; i < w * h; ++i) {
    ySrc[i] = (uint16_t)(((i * 97) % 1024) << 6 | ((i * 41) % 63));  // 高位10bit+低6位噪声
  }
  for (int i = 0; i < uvH * w / 2; ++i) {
    uvSrc[2 * i] = (uint16_t)(((i * 53) % 1024) << 6 | 17);          // U
    uvSrc[2 * i + 1] = (uint16_t)(((i * 71) % 1024) << 6 | 42);      // V
  }
  YUVFrame frame = {};
  frame.format = {w, h, YuvType::p010};
  frame.data[0] = reinterpret_cast<uint8_t*>(ySrc.data());
  frame.data[1] = reinterpret_cast<uint8_t*>(uvSrc.data());
  frame.data[2] = nullptr;
  frame.stride[0] = w * 2;
  frame.stride[1] = w * 2;
  CHECK_FALSE(bTightlyPacked(frame));  // p010 恒走归一化打包
  std::vector<uint8_t> buf(getYuvFrameSize(frame.format, w * 2));
  copyPlaneYUV2TightlyBuffer(frame, buf.data());
  auto* words = reinterpret_cast<const uint16_t*>(buf.data());
  // Y: 右移对齐低 10 位
  for (int i = 0; i < w * h; ++i) {
    CHECK(words[i] == (uint16_t)(ySrc[i] >> 6));
  }
  // UV: 拆分为独立 U/V 平面(U 在前), 同样右移对齐
  const int uvWords = w * h / 4;
  const uint16_t* u = words + w * h;
  const uint16_t* v = u + uvWords;
  for (int i = 0; i < uvH * w / 2; ++i) {
    CHECK(u[i] == (uint16_t)(uvSrc[2 * i] >> 6));
    CHECK(v[i] == (uint16_t)(uvSrc[2 * i + 1] >> 6));
  }
}
}  // namespace avox
