#pragma once

#include <cassert>
#include <mutex>
#include <vector>

#include "../AvoxTime.h"
#include "LogHelper.hpp"

namespace avox {

// 用来统计FPS/码率等信息
class TickCounter {
 public:
  TickCounter() = default;
  virtual ~TickCounter() = default;

 protected:
  // 上一次
  int64_t lastTime = 0;
  // 当前
  int64_t curTime = 0;
  // 统计间隔
  int64_t interval = 1000;
  // 统计值
  double tickValue = 0.0;
  // 每interval打开一次
  bool bInterval = false;

 protected:
  // 重置记时器
  virtual void onReset() {}
  // 每record原则上调用一次，用来统计需要的数据
  virtual void onTick() {}
  // 统计每次record在interval内的结果
  virtual void onRecord() = 0;

 public:
  void setInterval(int64_t interval_) { interval = interval_; }
  int64_t getInterval() const { return interval; }
  // 调用，统计每interval内onTick
  void record() {
    bInterval = false;
    int64_t delta = timeStampMS() - lastTime;
    // 第一次或是太久没更新，重新计时
    if (lastTime == 0 || delta > interval * 5) {
      lastTime = timeStampMS();
      curTime = 0;
      tickValue = 0.0;
      onReset();
      return;
    }
    onTick();
    curTime += delta;
    if (curTime > interval) {
      onRecord();
      curTime -= interval;
      bInterval = true;
    }
    lastTime = timeStampMS();
  }
  double value() const { return tickValue; }
  // 每一间隔打开一次
  bool bTrigger() const { return bInterval; }
  void reset() {
    curTime = 0;
    tickValue = 0.0;
    lastTime = 0;
    onReset();
  }
};

// 统计FPS
class FpsCounter : public TickCounter {
 public:
  FpsCounter() = default;
  virtual ~FpsCounter() = default;

 private:
  int32_t frameCount = 0;

 protected:
  virtual void onReset() override { frameCount = 0; }
  virtual void onTick() override { frameCount++; }
  virtual void onRecord() override {
    tickValue = (double)frameCount * 1000 / curTime;
    frameCount = 0;
  }
};

// 统计码率
class RateCounter : public TickCounter {
 public:
  RateCounter() = default;
  virtual ~RateCounter() = default;

 private:
  double totalRate = 0;
  double tickRate = 0.0;
  // 平均码率统计
  int64_t totalValue = 0;
  int64_t firstTime = 0;

 protected:
  virtual void onReset() override {
    totalRate = 0;
    totalValue = 0;
    firstTime = 0;
  }
  virtual void onTick() override { totalRate += tickRate; }
  virtual void onRecord() override {
    tickValue = totalRate * 1000 / curTime;
    totalRate = 0;
  }

 public:
  void record(int32_t packSize) {
    tickRate = packSize;
    totalValue += packSize;
    if (firstTime == 0) {
      firstTime = timeStampMS();
    }
    TickCounter::record();
  }  
  int64_t getTotalValue() const { return totalValue; }
  int64_t getTotalSpan() {
    if (firstTime == 0.0) {
      return 0.0;
    }
    return timeStampMS() - firstTime;
  }
};

// 固定时间统计状态次数，如进入buffing状态次数表示卡顿次数
class StateCounter : public TickCounter {
 public:
  StateCounter() { interval = 10000; }
  StateCounter(int64_t interval_) { interval = interval_; }

  virtual ~StateCounter() = default;

 private:
  int32_t frameCount = 0;

 protected:
  virtual void onReset() override { frameCount = 0; }
  virtual void onTick() override { frameCount++; }
  virtual void onRecord() override {
    tickValue = (double)frameCount;
    frameCount = 0;
  }
};

// 用来检测线程在一段时间有没有执行
class TickChecker {
 public:
  TickChecker(int32_t delayMs_) {
    delayMs = delayMs_;
    initTime = timeStampMS();
  }
  ~TickChecker() = default;

 private:
  int64_t initTime = 0;
  int32_t delayMs = 1000;

 public:
  // 检测是否超时
  bool timeout() {
    int64_t curTime = timeStampMS();
    if (curTime - initTime > delayMs) {
      return true;
    }
    return false;
  }
  // 重置
  void reset() { initTime = timeStampMS(); }
  // 运行期调整超时阈值
  void setDelay(int32_t delayMs_) { delayMs = delayMs_; }
};

// 用来打印Tick信息,但是不需要每次都打印
// 用来当某次状态改变时,或是隔段时间打印一次
class TickLog {
 public:
  TickLog(int32_t intervalMs = 0) { interval = intervalMs; };
  virtual ~TickLog() = default;

 private:
  // 已打印
  bool bLogged = false;
  // 如果是0,只在没log时打印
  // 如果大于0,简隔对应毫秒自动打印一次
  int64_t interval = 0;
  int64_t logTime = 0;

 public:
  template <typename... Args>
  void olog(LogLevel level, Args... args) {
    if (bLogged && interval <= 0) {
      return;
    }
    if (interval > 0 && logTime > 0) {
      if (timeStampMS() - logTime < interval) {
        return;
      }
    }
    log(level, args...);
    bLogged = true;
    logTime = timeStampMS();
  }
  void reset() { bLogged = false; }
};

}
