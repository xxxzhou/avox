#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>

std::mutex mutex;
std::condition_variable cv;
std::atomic<bool> bflag;
std::thread t1;
void opencv() {
  std::cout << "opencv start" << std::endl;
  t1 = std::thread([] {
    std::unique_lock<std::mutex> lock(mutex);
    std::cout << "lock3:" << lock.owns_lock() << std::endl;
    cv.notify_one();
    std::cout << "t1-" << std::endl;
    bflag.store(true);
    std::cout << "opencv end" << std::endl;
  });
}
void stopcv() {
  std::cout << "stopcv start" << std::endl;
  std::cout << "bflag:" << bflag.load() << std::endl;
  std::unique_lock<std::mutex> lock(mutex);
  std::cout << "lock1:" << lock.owns_lock() << std::endl;
  // 在条件不满足时，会阻塞当前线程，但是这时会释放锁，所以别的线程可以拿到锁
  cv.wait(lock, [] { return bflag.load(); });
  // 重新拿到锁
  std::cout << "lock2:" << lock.owns_lock() << std::endl;
  std::cout << "bflag:" << bflag.load() << std::endl;
  std::cout << "stopcv end" << std::endl;
}

void testcv() {
  std::cout << "testcv" << std::endl;
  bflag.store(false);
  opencv();
  stopcv();
  if (t1.joinable()) {
    t1.join();
  }
}
int main() {
  testcv();
  return 0;
}