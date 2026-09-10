// Observer 单元测试
// 从 samples/functest/observertest.cpp 迁来: 原文件只打印 onEvent 的结果,
// 不知道谁被通知了几次。这里让观察者记录收到的值, 断言派发范围。
#include <doctest.h>

#include <vector>

#include "avox/module/Observer.hpp"

namespace avox {
namespace {

struct Recorder {
  int32_t x = 0;
  std::vector<int32_t> got;
  void onEvent(int32_t value) { got.push_back(value); }
};

}  // namespace

TEST_CASE("Observer: 多观察者派发与移除") {
  Observer<Recorder> observer;
  Recorder r1, r2, r3;
  observer.addObserver(&r1);
  observer.addObserver(&r2);
  observer.addObserver(&r3);

  observer.dispatch(&Recorder::onEvent, 1);
  CHECK(r1.got.size() == 1);
  CHECK(r2.got.size() == 1);
  CHECK(r3.got.size() == 1);
  CHECK(r1.got.back() == 1);

  observer.removeObserver(&r2);
  observer.dispatch(&Recorder::onEvent, 2);
  CHECK(r1.got.size() == 2);
  CHECK(r2.got.size() == 1);  // 已移除, 不再收到
  CHECK(r3.got.size() == 2);
  CHECK(r1.got.back() == 2);

  observer.addObserver(&r2);  // 重复 add 不应造成重复派发
  observer.addObserver(&r2);
  observer.dispatch(&Recorder::onEvent, 3);
  CHECK(r2.got.size() == 2);
}

TEST_CASE("Observer: setObserver 单观察者优先, clear 后不再派发") {
  Observer<Recorder> observer;
  Recorder single, other;
  observer.addObserver(&other);
  observer.setObserver(&single);  // 设为单观察者后只通知它

  observer.dispatch(&Recorder::onEvent, 7);
  CHECK(single.got.size() == 1);
  CHECK(single.got.back() == 7);

  observer.clear();
  observer.dispatch(&Recorder::onEvent, 8);
  CHECK(single.got.size() == 1);
  CHECK(other.got.size() == 0);
}

}  // namespace avox
