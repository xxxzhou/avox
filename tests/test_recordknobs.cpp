// 录制旋钮纯函数单元测: 帧率门(投屏转码腿 B2 的判定核心; 尺寸缩放走
// ISurfaceRender::enableSizeChange 现成机制, 引擎侧无自研缩放)
#include <cmath>

#include "avox/video/VideoEncodeParams.hpp"
#include "doctest.h"

using namespace avox;

// 按 srcFps 的整数毫秒网格喂 seconds 秒, 返回保留帧数(长期平均=输出帧率)
static int32_t simulate(double srcFps, double capFps, double seconds) {
  FpsGateState st;
  int32_t kept = 0;
  const int32_t frames = (int32_t)std::llround(srcFps * seconds);
  for (int32_t i = 0; i < frames; ++i) {
    const int64_t pts = (int64_t)std::llround(i * 1000.0 / srcFps);
    if (recordFpsGate(pts, st, capFps)) {
      ++kept;
    }
  }
  return kept;
}

TEST_CASE("recordFpsGate: 60fps 源过 30fps 门隔帧保留") {
  FpsGateState st;
  // 60fps 源 pts ≈ 16.7ms 网格; 30fps 门 = 间隔 33.3ms
  CHECK(recordFpsGate(0, st, 30) == true);
  CHECK(recordFpsGate(17, st, 30) == false);
  CHECK(recordFpsGate(33, st, 30) == true);
  CHECK(recordFpsGate(50, st, 30) == false);
  CHECK(recordFpsGate(67, st, 30) == true);
  CHECK(recordFpsGate(83, st, 30) == false);
  CHECK(recordFpsGate(100, st, 30) == true);
}

TEST_CASE("recordFpsGate: 非整除比例按目标时刻累积(不腰斩)") {
  // 逐帧重置相位的旧实现: 60→24 只有 20fps, 50→24 只有 16.7fps
  CHECK(simulate(60, 24, 10) == 240);
  CHECK(simulate(50, 24, 10) == 240);
  CHECK(simulate(30, 24, 10) == 240);
  CHECK(simulate(60, 30, 10) == 300);
  CHECK(simulate(120, 60, 10) == 600);
}

TEST_CASE("recordFpsGate: 源帧率不高于上限时一帧不丢") {
  CHECK(simulate(24, 30, 10) == 240);
  CHECK(simulate(24, 24, 10) == 240);
  CHECK(simulate(30, 30, 10) == 300);
  // 29.97 不能先截成 29, 否则门槛 34ms > 帧间隔 33.37ms
  CHECK(simulate(29.97, 29.97, 10) == 300);
  CHECK(simulate(23.976, 24, 10) == 240);
}

TEST_CASE("recordFpsGate: 不限/首帧/非法 pts 语义") {
  FpsGateState st;
  CHECK(recordFpsGate(100, st, 0) == true);   // 不限恒保留
  CHECK(recordFpsGate(0, st, 30) == true);    // 首帧恒保留
  CHECK(recordFpsGate(5, st, 30) == false);   // 紧随其后超频丢弃
  CHECK(recordFpsGate(100, st, 30) == true);  // 间隔足保留
  // 无有效时间戳(AVOX_NOVALID_PTS): 旁路且不动基准
  CHECK(recordFpsGate(INT64_MIN, st, 30) == true);
  CHECK(recordFpsGate(101, st, 30) == false);
}

TEST_CASE("recordFpsGate: pts 回退(seek/换段)重设基准") {
  FpsGateState st;
  CHECK(recordFpsGate(10000, st, 30) == true);
  CHECK(recordFpsGate(10033, st, 30) == true);
  CHECK(recordFpsGate(10067, st, 30) == true);
  // 新段 pts 从头开始: 不能一直丢到追上旧位置
  CHECK(recordFpsGate(0, st, 30) == true);
  CHECK(recordFpsGate(17, st, 30) == false);
  CHECK(recordFpsGate(33, st, 30) == true);
}
