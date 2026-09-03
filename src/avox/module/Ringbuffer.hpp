#pragma once

#include <cassert>
#include <mutex>
#include <vector>
#include <atomic>

#include "../AvoxDef.h"
#include "LogHelper.hpp"
#include <condition_variable>

namespace avox {

// 环形队列
template <typename T> class RingBuffer {
public:
  RingBuffer(int32_t maxCount = 10) { setMaxSize(maxCount); }
  ~RingBuffer() { buffer.clear(); }

public:
  void setMaxSize(int32_t maxCount) {
    std::lock_guard<std::mutex> lock(buMtx);
    maxSize = maxCount;
    buffer.resize(maxSize);
  }

protected:
  // 读取位置
  std::atomic<int32_t> rindex{0};
  // 写入数据个数
  std::atomic<int32_t> wcount{0};
  // 环形缓冲区
  std::vector<T> buffer;
  // 最大长度
  std::atomic<int32_t> maxSize{10};
  // 互斥锁
  mutable std::mutex buMtx;
  // 队列未满条件变量
  std::condition_variable cvNotFull;
  // 如果队列与线程有关，有用堵塞写入，需要记录线程状态
  // 否则可能在线程退出时，还堵在enqueueWait中，导致不能关闭
  // 使用队列的线程关闭，当前队列不可使用，所有堵塞打开
  std::atomic<bool> bClose{false};

public:
  bool enqueue(const T &item, bool bCover = false) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount >= maxSize) {
      if (bCover) {
        buffer[rindex] = item;
        rindex = (rindex + 1) % maxSize;
      }
      return false;
    }
    int32_t index = (rindex + wcount) % maxSize;
    buffer[index] = item;
    wcount++;
    return true;
  }

  void enqueueWait(const T &item) {
    std::unique_lock<std::mutex> lock(buMtx);
    cvNotFull.wait(lock, [this] { return wcount < maxSize || bClose; });
    if (bClose) {
      return;
    }
    int32_t index = (rindex + wcount) % maxSize;
    buffer[index] = item;
    wcount++;
  }

  // 先检查队列最后一个元素是否满足条件
  // 一般是如命令队列,重复命令不添加
  void enqueueWaitBackMatch(const T &item,
                            std::function<bool(const T &back)> matchCond) {
    std::unique_lock<std::mutex> lock(buMtx);
    cvNotFull.wait(lock, [this] { return wcount < maxSize || bClose; });
    if (bClose) {
      return;
    }
    // 没有数据，则直接添加
    bool bMatch = true;
    if (wcount > 0) {
      // 最后一个是否满足条件
      bMatch = matchCond(buffer[(rindex + wcount - 1) % maxSize]);
    }
    if (!bMatch) {
      return;
    }
    int32_t index = (rindex + wcount) % maxSize;
    buffer[index] = item;
    wcount++;
  }

  template <typename B>
  bool enqueue(const B &item, std::function<void(T &right, const B &left)> func,
               bool bCover = false) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount >= maxSize) {
      if (bCover) {
        func(buffer[rindex], item);
        rindex = (rindex + 1) % maxSize;
        return true;
      }
      return false;
    }
    int32_t index = (rindex + wcount) % maxSize;
    func(buffer[index], item);
    wcount++;
    return true;
  }

  template <typename B>
  void enqueueWait(const B &item,
                   std::function<void(T &right, const B &left)> func) {
    std::unique_lock<std::mutex> lock(buMtx);
    cvNotFull.wait(lock, [this] { return wcount < maxSize || bClose; });
    if (bClose) {
      return;
    }
    int32_t index = (rindex + wcount) % maxSize;
    func(buffer[index], item);
    wcount++;
  }

  template <typename B>
  void
  enqueueWait(const B &item,
              std::function<bool(T &right, const B &left)> matchCond,
              std::function<void(T &right, const B &left, bool bMatch)> func) {
    {
      // 先查询队列最后一个元素,如果满足条件,则在最后元素上操作
      std::lock_guard<std::mutex> lock(buMtx);
      if (wcount > 0) {
        T &witem = buffer[(rindex + wcount - 1) % maxSize];
        if (matchCond(witem, item)) {
          func(witem, item, true);
          return;
        }
      }
    }
    // 封装一个新的 lambda 函数，将 bMatch 固定为 false
    std::function<void(T & right, const B &left)> adaptedFunc =
        [&func](T &right, const B &left) { func(right, left, false); };
    std::unique_lock<std::mutex> lock(buMtx);
    // 使用条件变量等待队列未满，同时检查外部条件
    cvNotFull.wait(lock, [this] { return wcount < maxSize || bClose; });
    if (bClose) {
      return;
    }
    // 插入新元素
    int32_t index = (rindex + wcount) % maxSize;
    adaptedFunc(buffer[index], item);
    wcount++;
  }

  bool dequeue(T &item) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    item = buffer[rindex];
    rindex = (rindex + 1) % maxSize;
    wcount--;
    cvNotFull.notify_one();
    return true;
  }

  bool dequeueAction(std::function<void(const T &right)> action) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    action(buffer[rindex]);
    rindex = (rindex + 1) % maxSize;
    wcount--;
    cvNotFull.notify_one();
    return true;
  }

  // 忽略 bClose 排干整队列: 锁内取一帧、解锁后调 action, 直到空。
  // 供消费者线程 stop 后排干 in-flight 残留 —— 此时 dequeue/dequeueAction 会因
  // bClose=true 返回 false, 无法取出; drain 专用于"已决定丢弃队列、但想先逐项处理"
  // 的收尾场景(如 AudioTap 关闭前排干尾部帧喂给上层)。
  void drain(std::function<void(const T &right)> action) {
    while (true) {
      T item;
      {
        std::unique_lock<std::mutex> lock(buMtx);
        if (wcount == 0) {
          return;
        }
        item = buffer[rindex];
        rindex = (rindex + 1) % maxSize;
        wcount--;
        cvNotFull.notify_one();
      }
      action(item);
    }
  }

  template <typename B>
  B counter(std::function<B(const T &begin, const T &end)> func) {
    std::lock_guard<std::mutex> lock(buMtx);
    B b = {};
    if (wcount == 0 || bClose) {
      return b;
    }
    T &begin = buffer[rindex];
    T &end = buffer[(rindex + wcount - 1) % maxSize];
    b = func(begin, end);
    return b;
  }

  int32_t counter(std::function<void(T &begin, T &end)> func) {
    std::lock_guard<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return 0;
    }
    T &begin = buffer[rindex];
    T &end = buffer[(rindex + wcount - 1) % maxSize];
    func(begin, end);
    return wcount;
  }

  // 返回第一个元素,但是不出peek+pop=dequeue
  bool peek(T &item) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    item = buffer[rindex];
    return true;
  }

  bool peekAction(std::function<void(const T &right)> action) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    action(buffer[rindex]);
    return true;
  }

  bool back(T &item) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    item = buffer[(rindex + wcount - 1) % maxSize];
    return true;
  }

  bool backAction(std::function<void(const T &right)> action) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return false;
    }
    action(buffer[(rindex + wcount - 1) % maxSize]);
    return true;
  }

  void pop() {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return;
    }
    rindex = (rindex + 1) % maxSize;
    wcount--;
    cvNotFull.notify_one();
  }

  void pop(std::function<void(const T &right)> action) {
    std::unique_lock<std::mutex> lock(buMtx);
    if (wcount == 0 || bClose) {
      return;
    }
    action(buffer[rindex]);
    rindex = (rindex + 1) % maxSize;
    wcount--;
    cvNotFull.notify_one();
  }

  const T &operator[](int32_t index) const {
    std::unique_lock<std::mutex> lock(buMtx);
    assert(index < wcount && index >= 0);
    return buffer[(rindex + index) % maxSize];
  }

  T &operator[](int32_t index) {
    std::unique_lock<std::mutex> lock(buMtx);
    assert(index < wcount && index >= 0);
    return buffer[(rindex + index) % maxSize];
  }

  // 不锁，外面只用来读取写入日志
  int32_t size() const { return wcount; }

  int32_t getRindex() const { return rindex; }

  int32_t maxCount() const { return maxSize; }

  bool empty() const { return wcount == 0; }

  bool full() const { return wcount == maxSize; }

  void clear() {
    std::unique_lock<std::mutex> lock(buMtx);
    // 直接clear会导致队列size为0,后续不可用
    buffer.assign(buffer.size(), T());
    wcount = 0;
    rindex = 0;
    cvNotFull.notify_all();
  }

  void clear(std::function<void(const T &right)> func) {
    std::unique_lock<std::mutex> lock(buMtx);
    for (int32_t i = 0; i < wcount; i++) {
      func(buffer[(rindex + i) % maxSize]);
    }
    // 直接clear会导致队列size为0,后续不可用
    buffer.assign(buffer.size(), T());
    wcount = 0;
    rindex = 0;
    cvNotFull.notify_all();
  }

  void action(std::function<void(T &right)> func) {
    std::unique_lock<std::mutex> lock(buMtx);
    for (int32_t i = 0; i < wcount; i++) {
      func(buffer[(rindex + i) % maxSize]);
    }
  }

  void lock() { buMtx.lock(); }

  void unlock() { buMtx.unlock(); }

  // 通知条件变量wait检查条件，wait重新做函数检查
  void setClose(bool close) {
    std::unique_lock<std::mutex> lock(buMtx);
    bClose = close;
    // 通知条件变量
    cvNotFull.notify_all();
  }
};

}