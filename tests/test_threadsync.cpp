// 线程同步单元测试
// 从 samples/functest/threadtest.cpp 迁来: 原文件演示 condition_variable 的用法,
// 全程 cout 无判定。这里断言"等待返回时确实重新拿到了锁"这个最容易写错的点。
#include <doctest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

TEST_CASE("condition_variable: wait 期间释放锁, 返回时重新持有") {
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> ready{false};
  std::atomic<bool> waitReturned{false};

  std::thread notifier([&] {
    std::lock_guard<std::mutex> lock(mtx);
    ready.store(true);
    cv.notify_one();
  });

  bool ownsLockAfterWait = false;
  {
    std::unique_lock<std::mutex> lock(mtx);
    // 谓词为假时阻塞, 阻塞期间会释放锁 -> 通知方才拿得到锁
    cv.wait(lock, [&] { return ready.load(); });
    waitReturned.store(true);
    ownsLockAfterWait = lock.owns_lock();
  }
  notifier.join();

  CHECK(ready.load());
  CHECK(waitReturned.load());
  CHECK(ownsLockAfterWait);  // wait 返回时必须重新持有锁, 否则后续临界区是裸的
}
