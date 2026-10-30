// IMediaReader 纯逻辑单测: 枚举字符串表/状态迁移表/网络 URI 判定/
// display matrix 旋转归一/音频峰值扫描与桶映射(AvoxMedia.cpp 字符串实现同编)
#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "avox/AvoxMedia.h"
#include "avox/video/MediaThumbUtil.hpp"
#include "avox_ffmpeg/MediaReaderUtil.hpp"

namespace avox {

namespace {

// 构造 3x3 16.16 定点 display matrix(元素 0-5 有效, 同 ffmpeg 布局)
void makeMatrix(int32_t* m, double cosV, double sinV) {
  const int32_t kOne = 1 << 16;
  m[0] = (int32_t)(cosV * kOne);
  m[1] = (int32_t)(sinV * kOne);
  m[2] = 0;
  m[3] = (int32_t)(-sinV * kOne);
  m[4] = (int32_t)(cosV * kOne);
  m[5] = 0;
  m[6] = 0;
  m[7] = 0;
  m[8] = kOne;
}

}  // namespace

TEST_CASE("media reader enum strings") {
  // 每个枚举值都有非空字符串; 越界返回 unknown
  for (int32_t i = 0; i <= 14; ++i) {
    CHECK(std::strlen(getMediaErrorStr((MediaError)i)) > 0);
  }
  CHECK(std::strcmp(getMediaErrorStr(MediaError::inputOpenFailed),
                    "input open failed") == 0);
  CHECK(std::strcmp(getMediaErrorStr(MediaError::renderUnavailable),
                    "render unavailable") == 0);
  CHECK(std::strcmp(getMediaErrorStr((MediaError)999), "unknown") == 0);
  CHECK(std::strcmp(getMediaErrorStr((MediaError)-1), "unknown") == 0);
  for (int32_t i = 0; i <= 5; ++i) {
    CHECK(std::strlen(getMediaReaderStateStr((MediaReaderState)i)) > 0);
  }
  CHECK(std::strcmp(getMediaReaderStateStr(MediaReaderState::indexing),
                    "indexing") == 0);
  CHECK(std::strcmp(getMediaReaderStateStr((MediaReaderState)99), "unknown") ==
        0);
  for (int32_t i = 0; i <= 3; ++i) {
    CHECK(std::strlen(getMediaOpStr((MediaOp)i)) > 0);
  }
  CHECK(std::strcmp(getMediaOpStr(MediaOp::frame), "frame") == 0);
  CHECK(std::strcmp(getMediaOpStr((MediaOp)42), "unknown") == 0);
}

TEST_CASE("media reader state transitions") {
  CHECK(mediaReaderCanTransit(MediaReaderState::idle,
                              MediaReaderState::opening));
  CHECK(mediaReaderCanTransit(MediaReaderState::opening,
                              MediaReaderState::ready));
  CHECK(mediaReaderCanTransit(MediaReaderState::opening,
                              MediaReaderState::idle));
  CHECK(mediaReaderCanTransit(MediaReaderState::ready,
                              MediaReaderState::indexing));
  CHECK(mediaReaderCanTransit(MediaReaderState::ready,
                              MediaReaderState::grabbing));
  CHECK(mediaReaderCanTransit(MediaReaderState::ready,
                              MediaReaderState::closing));
  CHECK(mediaReaderCanTransit(MediaReaderState::indexing,
                              MediaReaderState::ready));
  CHECK(mediaReaderCanTransit(MediaReaderState::grabbing,
                              MediaReaderState::ready));
  CHECK(mediaReaderCanTransit(MediaReaderState::indexing,
                              MediaReaderState::closing));
  CHECK(mediaReaderCanTransit(MediaReaderState::closing,
                              MediaReaderState::idle));
  // 非法迁移
  CHECK_FALSE(mediaReaderCanTransit(MediaReaderState::idle,
                                    MediaReaderState::ready));
  CHECK_FALSE(mediaReaderCanTransit(MediaReaderState::idle,
                                    MediaReaderState::indexing));
  CHECK_FALSE(mediaReaderCanTransit(MediaReaderState::ready,
                                    MediaReaderState::opening));
  CHECK_FALSE(mediaReaderCanTransit(MediaReaderState::indexing,
                                    MediaReaderState::indexing));
}

TEST_CASE("network uri detection") {
  CHECK(isNetworkUri("http://example.com/a.mp4"));
  CHECK(isNetworkUri("https://example.com/a.mp4"));
  CHECK(isNetworkUri("rtsp://cam/stream"));
  CHECK(isNetworkUri("rtmp://live/x"));
  // file:// 本质是本地
  CHECK_FALSE(isNetworkUri("file:///D:/video/a.mp4"));
  CHECK_FALSE(isNetworkUri("FILE://D:/video/a.mp4"));
  // 本地路径/UNC/映射盘
  CHECK_FALSE(isNetworkUri("D:/video/a.mp4"));
  CHECK_FALSE(isNetworkUri("\\\\server\\share\\a.mp4"));
  CHECK_FALSE(isNetworkUri("//server/share/a.mp4"));
  CHECK_FALSE(isNetworkUri("/home/u/a.mp4"));
  CHECK_FALSE(isNetworkUri(nullptr));
  CHECK_FALSE(isNetworkUri(""));
  // 路径中途的 :// 不是 scheme
  CHECK_FALSE(isNetworkUri("/home/u/a://b.mp4"));
  // 非法 scheme 首字符
  CHECK_FALSE(isNetworkUri("1http://x/y"));
}

TEST_CASE("display matrix rotation") {
  const int32_t kOne = 1 << 16;
  int32_t m[9] = {};
  // 恒等 → 0
  makeMatrix(m, 1.0, 0.0);
  CHECK(displayMatrixRotation(m) == 0);
  // 90°/180°/270°
  makeMatrix(m, 0.0, 1.0);
  CHECK(displayMatrixRotation(m) == 90);
  makeMatrix(m, -1.0, 0.0);
  CHECK(displayMatrixRotation(m) == 180);
  makeMatrix(m, 0.0, -1.0);
  CHECK(displayMatrixRotation(m) == 270);
  // 镜像矩阵(det<0) 不转
  m[0] = kOne;
  m[1] = 0;
  m[3] = 0;
  m[4] = -kOne;
  m[8] = kOne;
  CHECK(displayMatrixRotation(m) == 0);
  // 自由角(45°) 不转
  makeMatrix(m, 0.7071067811865476, 0.7071067811865476);
  CHECK(displayMatrixRotation(m) == 0);
  CHECK(displayMatrixRotation(nullptr) == 0);
}

TEST_CASE("thumbnail output size") {
  // 横拍 16:9, 请求宽 320
  CHECK(thumbDisplayHeight(320, 1920, 1080, 0) == 180);
  // 竖拍(编码 360x640): 请求 320 → 569, 与 ak_frames 旧口径一致
  CHECK(thumbDisplayHeight(320, 360, 640, 0) == 569);
  // display matrix 90/270: 显示宽高互换(编码横的转竖, 竖的转横)
  CHECK(thumbDisplayHeight(320, 1920, 1080, 90) == 569);
  CHECK(thumbDisplayHeight(320, 1920, 1080, 270) == 569);
  CHECK(thumbDisplayHeight(320, 360, 640, 90) == 180);
  // 180 只翻转, 不改比例
  CHECK(thumbDisplayHeight(320, 640, 360, 180) == 180);
  // 非法输入/极小高度兜底
  CHECK(thumbDisplayHeight(0, 640, 360, 0) == 0);
  CHECK(thumbDisplayHeight(320, 0, 360, 0) == 0);
  CHECK(thumbDisplayHeight(320, 4000, 1, 0) == 1);
  // 请求宽钳位 [48,1280]
  CHECK(clampThumbWidth(1) == 48);
  CHECK(clampThumbWidth(320) == 320);
  CHECK(clampThumbWidth(4000) == 1280);
}

TEST_CASE("thumbnail yuv gate") {
  // VkYUV2RGBALayer 支持的面
  CHECK(thumbSupportsYuv(YuvType::yuv420P));
  CHECK(thumbSupportsYuv(YuvType::yuv422P));
  CHECK(thumbSupportsYuv(YuvType::nv12));
  CHECK(thumbSupportsYuv(YuvType::yuv420P10));
  CHECK(thumbSupportsYuv(YuvType::yuv422P10));
  CHECK(thumbSupportsYuv(YuvType::p010));
  // 444P/gray/打包格式不支持: 抓帧回 unsupportedPixFmt, 不落 CPU 兜底
  CHECK_FALSE(thumbSupportsYuv(YuvType::yuv444P));
  CHECK_FALSE(thumbSupportsYuv(YuvType::other));
  CHECK_FALSE(thumbSupportsYuv(YuvType::uyvyI));
  CHECK_FALSE(thumbSupportsYuv(YuvType::uyvy422_10B));
}

TEST_CASE("audio frame peak") {
  // U8 interleaved: (128,255,0) → 0, +127/128, -1 → 峰 1.0
  {
    const uint8_t data[] = {128, 255, 0};
    const uint8_t* planes[1] = {data};
    CHECK(mediaFramePeak(planes, 1, 3, AudioFormat::AVOX_AUDIO_U8) ==
          doctest::Approx(1.0));
  }
  // S16 interleaved
  {
    const int16_t data[] = {16384, -32768};
    const uint8_t* planes[1] = {(const uint8_t*)data};
    CHECK(mediaFramePeak(planes, 1, 2, AudioFormat::AVOX_AUDIO_S16) ==
          doctest::Approx(1.0));
  }
  // S16P planar: 面0 峰 0.5, 面1 峰 1.0
  {
    const int16_t p0[] = {16384};
    const int16_t p1[] = {-32768};
    const uint8_t* planes[2] = {(const uint8_t*)p0, (const uint8_t*)p1};
    CHECK(mediaFramePeak(planes, 2, 1, AudioFormat::AVOX_AUDIO_S16P) ==
          doctest::Approx(1.0));
  }
  // FLT interleaved
  {
    const float data[] = {0.25f, -0.5f};
    const uint8_t* planes[1] = {(const uint8_t*)data};
    CHECK(mediaFramePeak(planes, 1, 2, AudioFormat::AVOX_AUDIO_FLT) ==
          doctest::Approx(0.5));
  }
  // DBL >1 钳到 1
  {
    const double data[] = {2.0};
    const uint8_t* planes[1] = {(const uint8_t*)data};
    CHECK(mediaFramePeak(planes, 1, 1, AudioFormat::AVOX_AUDIO_DBL) ==
          doctest::Approx(1.0));
  }
  // S32: 2^30 → 0.5
  {
    const int32_t data[] = {1073741824};
    const uint8_t* planes[1] = {(const uint8_t*)data};
    CHECK(mediaFramePeak(planes, 1, 1, AudioFormat::AVOX_AUDIO_S32) ==
          doctest::Approx(0.5));
  }
  // U8P: 空面跳过
  {
    const uint8_t p0[] = {0};
    const uint8_t* planes[2] = {p0, nullptr};
    CHECK(mediaFramePeak(planes, 2, 1, AudioFormat::AVOX_AUDIO_U8P) ==
          doctest::Approx(1.0));
  }
  // 非法输入
  const uint8_t* planes[1] = {nullptr};
  CHECK(mediaFramePeak(planes, 1, 4, AudioFormat::AVOX_AUDIO_S16) == 0.0);
  CHECK(mediaFramePeak(nullptr, 1, 4, AudioFormat::AVOX_AUDIO_S16) == 0.0);
}

TEST_CASE("peak bucket mapping") {
  std::vector<float> out;
  // 200ms 分 2 桶: 0/50ms → 桶0(max 0.25,1.0), 100ms → 桶1(0.0625);
  // 线性最大不开方
  std::vector<std::pair<int64_t, double>> frames = {
      {0, 0.25}, {50, 1.0}, {100, 0.0625}};
  mapPeaksToBuckets(frames, 2, 200, out);
  REQUIRE(out.size() == 2);
  CHECK(out[0] == doctest::Approx(1.0));
  CHECK(out[1] == doctest::Approx(0.0625));
  // duration<=0 → 全 0 且长度=buckets(与 ak_index 行为一致)
  mapPeaksToBuckets(frames, 3, 0, out);
  REQUIRE(out.size() == 3);
  CHECK(out[0] == 0.0f);
  CHECK(out[2] == 0.0f);
  // 超界时间钳最后一桶
  std::vector<std::pair<int64_t, double>> late = {{99999, 0.5}};
  mapPeaksToBuckets(late, 2, 200, out);
  REQUIRE(out.size() == 2);
  CHECK(out[0] == 0.0f);
  CHECK(out[1] == doctest::Approx(0.5));
  // 负时间跳过
  std::vector<std::pair<int64_t, double>> neg = {{-5, 0.9}};
  mapPeaksToBuckets(neg, 2, 200, out);
  REQUIRE(out.size() == 2);
  CHECK(out[0] == 0.0f);
}

}  // namespace avox
