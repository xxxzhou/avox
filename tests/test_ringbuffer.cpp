// RingBuffer 单元测试
// 从 samples/functest/ringbuffertest.cpp 迁来: 原文件是压力演示 —— 主线程跑
// 100 万次 enqueue + 毫秒级 sleep (约 1000 秒), 且失序时只打一行 warn 日志,
// 既不计数也不影响返回值。这里改成小规模、无 sleep 的确定性用例: 顺序/满/覆盖/并发。
#include <doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "avox/module/Ringbuffer.hpp"

namespace avox {

TEST_CASE("RingBuffer: 单线程 FIFO 顺序") {
  RingBuffer<int32_t> rb(10);
  for (int32_t i = 0; i < 5; ++i) {
    CHECK(rb.enqueue(i));
  }
  int32_t v = -1;
  for (int32_t i = 0; i < 5; ++i) {
    CHECK(rb.dequeue(v));
    CHECK(v == i);
  }
  CHECK(!rb.dequeue(v));  // 取空后返回 false
}

TEST_CASE("RingBuffer: 满队列不覆盖时丢弃新值") {
  RingBuffer<int32_t> rb(4);
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(rb.enqueue(i));
  }
  CHECK(!rb.enqueue(99));  // 满了, bCover=false -> 新值丢弃, 老数据不动

  int32_t v = -1;
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(rb.dequeue(v));
    CHECK(v == i);
  }
}

TEST_CASE("RingBuffer: 满队列覆盖时挤掉最老的一个") {
  RingBuffer<int32_t> rb(4);
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(rb.enqueue(i));
  }
  CHECK(!rb.enqueue(4, true));  // 返回 false (没真的"加进去"), 但覆盖了最老的 0

  int32_t v = -1;
  for (int32_t expect = 1; expect <= 4; ++expect) {
    CHECK(rb.dequeue(v));
    CHECK(v == expect);
  }
}

TEST_CASE("RingBuffer: 一写一读不丢不乱序 (2000 个)") {
  constexpr int32_t kCount = 2000;
  RingBuffer<int32_t> rb(16);
  std::atomic<bool> done{false};
  std::vector<int32_t> got;
  got.reserve(kCount);

  std::thread producer([&] {
    for (int32_t i = 0; i < kCount; ++i) {
      rb.enqueueWait(i);  // 满则阻塞等消费, 不丢数据
    }
    done.store(true);
  });
  std::thread consumer([&] {
    while (got.size() < static_cast<size_t>(kCount)) {
      int32_t v = 0;
      if (rb.dequeue(v)) {
        got.push_back(v);
      } else {
        std::this_thread::yield();
      }
    }
  });
  producer.join();
  consumer.join();

  CHECK(done.load());
  CHECK(got.size() == static_cast<size_t>(kCount));
  bool ordered = got.size() == static_cast<size_t>(kCount);
  for (int32_t i = 0; ordered && i < kCount; ++i) {
    ordered = (got[static_cast<size_t>(i)] == i);
  }
  CHECK(ordered);  // enqueueWait 模式下必须严格 0,1,2,... 不能跳号
}

}  // namespace avox
