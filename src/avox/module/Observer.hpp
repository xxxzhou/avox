#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "../AvoxDef.h"
#include "LogHelper.hpp"

namespace avox {

// 考虑移除锁
// 如果有类用Observer与RenTask的组合,需要注意
// 开始时调用addObserver，再startTask
// 关闭时先stopTask，再removeObserver
// 但是如果是有Observer,还是需要锁
template <typename T>
class Observer {
 protected:
  /* data */
  std::vector<T*> observers;
  // std::vector<T *> pendingRemovals;
  // 改为读写锁，回调distpach互相不影响
  // 音频与视频数据都是高频IO
  // 原来锁会导致音频/视频同步，导致二者延迟增加,特别是android
  // 在SourcePlayer录制音视频并播放，其录制出来的视频每帧达到100ms
  // 就是因为录音同步导致的,本身1080通过OES纹理直接显示平均只有1ms
  std::shared_mutex mtxOb;
  // 有些对象不需要多个观察者,因此可以不用锁提高性能
  // 如果observe有值,则说明只有一个观察者
  std::atomic<T*> observer = nullptr;

 public:
  Observer(/* args */) = default;
  virtual ~Observer() {
    observer.store(nullptr, std::memory_order_release);
    std::unique_lock<std::shared_mutex> lock(mtxOb);
    observers.clear();
  };

 public:
  // 可以设置成空,如果是空,后面代码进入多观察者逻辑
  void setObserver(T* observer_) {
    observer.store(observer_, std::memory_order_release);
    if (observer_) {
      std::unique_lock<std::shared_mutex> lock(mtxOb);
      if (!observers.empty()) {
        LOGFLF(LogLevel::warn, "observers not empty");
        observers.clear();
      }
    }
  }
  void addObserver(T* observer_) {
    if (observer.load(std::memory_order_relaxed) != nullptr) {
      LOGFLF(LogLevel::warn, "observer is not nullptr,return");
      return;
    }
    std::unique_lock<std::shared_mutex> lock(mtxOb);
    // 使用 std::find 查找是否已经存在该 observer
    auto it = std::find(observers.begin(), observers.end(), observer_);
    if (it == observers.end()) {
      // 如果没找到（即迭代器指向容器末尾），说明不存在，执行添加操作
      observers.push_back(observer_);
    }
  }
  void removeObserver(T* observer_) {
    if (observer.load(std::memory_order_relaxed) == observer_) {
      observer.store(nullptr, std::memory_order_release);
    }
    std::unique_lock<std::shared_mutex> lock(mtxOb);
    // 使用std::remove将匹配的元素移到容器末尾，返回新的逻辑结尾迭代器
    auto newEnd = std::remove(observers.begin(), observers.end(), observer_);
    // 真正从容器中删除那些被移动到末尾的元素（通过erase）
    observers.erase(newEnd, observers.end());
  }
  // Dispatch里有删除需要时，回调里调用
  // void removeDispatch(T *observer) { pendingRemovals.push_back(observer); }

  // 不用时手动调用
  void clear() {
    observer.store(nullptr, std::memory_order_release);
    std::unique_lock<std::shared_mutex> lock(mtxOb);
    observers.clear();
  }

  // void onEvent(int32_t value)
  // observer.dispatch(&Observer::onEvent, 1);
  // 匹配T的成员函数指针，调用观察者的方法，线程调用安全
  template <typename Func, typename... Args>
  void dispatch(Func func, Args&&... args) {
    T* singleObs = observer.load(std::memory_order_acquire);
    if (singleObs) {
      (singleObs->*func)(std::forward<Args>(args)...);
      return;
    }
    std::shared_lock<std::shared_mutex> lock(mtxOb);
    // 回调在lock下，一定注意这个回调如果再到有锁的情况可能导致死锁
    // 比如在回调中调用removeObserver，就会导致死锁，除非使用递归锁
    // 并且removeObserver会导致迭代器失效，所以需要重新遍历
    for (T* observer : observers) {
      // 使用成员函数指针调用观察者的方法
      (observer->*func)(std::forward<Args>(args)...);
    }
  }

  bool empty() {
    if (observer.load(std::memory_order_relaxed) != nullptr) {
      return false;
    }
    std::shared_lock<std::shared_mutex> lock(mtxOb);
    return observers.empty();
  }
};

}