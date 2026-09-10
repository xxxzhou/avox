// 时间与时钟单元测试
// 从 samples/functest/logtest.cpp (打印 12 次 + 每次 sleep 1s = 12 秒) 与
// regedittest.cpp (while(true) 死循环打印 clock, 还依赖 avox_vulkan) 合并而来:
// 两个都是"跑起来看输出"的探针, 这里改成毫秒级的确定性断言。
#include <doctest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "avox/AvoxTime.h"
#include "avox/module/LogHelper.hpp"
#include "avox/player/Clock.hpp"

namespace avox {

TEST_CASE("时间戳: 单调且与本地时区差在一天内") {
  const int64_t t1 = timeStampMS();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const int64_t t2 = timeStampMS();

  CHECK(t1 > 0);
  CHECK(t2 >= t1);  // 单调不减

  const int64_t local = localTimeStampMS();
  const int64_t diff = local > t2 ? local - t2 : t2 - local;
  CHECK(diff < 15 * 3600 * 1000);  // 只差一个时区偏移 (-12 ~ +14 小时)

  CHECK(timeTick() > 0);
  CHECK(timeTickMS() > 0.0);
}

TEST_CASE("Timespan: 时分秒构造与换算") {
  Timespan ts;
  ts.build(1, 2, 3);
  CHECK(ts.getHours() == 1);
  CHECK(ts.getMinutes() == 2);
  CHECK(ts.getSeconds() == 3);
  CHECK(ts.getTotalSeconds() == doctest::Approx(3723.0));
  CHECK(ts.getTotalMilliSeconds() == doctest::Approx(3723000.0));
}

TEST_CASE("log: 各级别与多种参数都能出日志") {
  log(LogLevel::info, "hello world");
  log(LogLevel::info, "gettime:", timeStampMS());
  Timespan ts = {timeStampMS() * 10000};
  log(LogLevel::warn, "timespan:", ts.getTotalSeconds());
}

TEST_CASE("Clock: 随真实时间走, 暂停后冻结") {
  Clock c;
  c.update(1000);
  CHECK(c.clock() >= 1000);
  CHECK(c.clock() < 1000 + 500);  // 刚 update, 不该跳出去

  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK(c.clock() > 1000);  // 未暂停时跟着真实时间涨

  c.pause(true);
  const int64_t frozen = c.clock();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(c.clock() == frozen);  // 暂停期间不再前进

  c.pause(false);
  CHECK(c.clock() >= frozen);  // 恢复后从暂停点继续, 不把暂停时长补进来
}

}  // namespace avox
