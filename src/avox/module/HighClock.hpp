#pragma once

#include <chrono>
#include <vector>

#include "../Avox.hpp"

namespace avox {

class HighClock {
private:
  /* data */
  std::chrono::high_resolution_clock::time_point startPoint = {};
  std::vector<std::chrono::high_resolution_clock::time_point> recordPoints;

public:
  HighClock(/* args */);
  ~HighClock();

public:
  // 开始计时，初始化默认调用，每次调用会重置开始与记录点
  void start();
  // 记录当前时间点,返回记录点索引
  int32_t record();
  // 记录并返回与start相差的微秒microseconds
  int64_t recordDelta();
  // 记录并返回与上个记录点相差的微秒microseconds
  int64_t recordLast();
  // 微秒microseconds
  int64_t getClock(int32_t recordIndex);
  // 微秒microseconds
  int64_t getClock(int32_t recordStart, int32_t recordEnd);
};

}
