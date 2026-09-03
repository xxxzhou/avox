#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../AvoxDef.h"
#include "Ringbuffer.hpp"

// wasm task实现可以参考 https://www.hellobit.com.cn/b/767368973/2401459541.html
// 线程使用尽量继承这个类，方便后期统一更新，如wasm用不上线程，这个用来模拟单线程循环
namespace avox {

class TaskTrack;

class AVOX_EXPORT RunTask {
  // 这些同步量，全部用在当前线程，子类改动可能导致逻辑不正常
 private:
  std::atomic<bool> runflag;
  /* data */
  std::thread thread;
  // 同步资源初始化与释放
  std::mutex taskMtx;
  // 暂停标志，由子类在 onRunTask 循环中自行检查
  std::atomic<bool> tpause;
  // 供子类等待恢复用的条件变量
  std::condition_variable pauseCond;
  std::vector<std::function<bool()>> actions;

 protected:
  std::string taskName = "run task";

 public:
  RunTask(/* args */);
  virtual ~RunTask();

 protected:
  // 子类可能需要初始化相应资源
  virtual bool onStartTask() { return true; };
  // 子类如何执行
  virtual void onRunTask() = 0;
  // 子类可能需要释放资源
  virtual void onStopTask() {};

 public:
  // 直接用start/run/stop容易重名
  bool startTask();
  void runTask();
  void stopTask();
  bool running();
  void sleepTask(bool yield, int32_t ms = 10);
  // 若本对象同时是 TaskTrack（大对象多重继承），返回其 TaskTrack 指针；否则 nullptr
  virtual TaskTrack* asTaskTrack() { return nullptr; }

 public:
  // GPU上下文一般会和线程绑定，所以如果不同线程回调需要同步
  void addAction(const std::function<bool()>& action);
  void executeActions();

 public:
  void pauseTask();
  void resumeTask();
  // 子类在 onRunTask 循环中自行检查：if (pauseing()) { ... }
  bool pauseing();
};

class LogTask : public RunTask {
 public:
  LogTask(/* args */);
  virtual ~LogTask();

 private:
  RingBuffer<LogItem> logQueue;

 public:
  void addItem(LogLevel level, const char* message);
  void addItem(const LogItem& item);
  // 同步排空当前队列里的日志(在调用线程直接log)
  // agent chain runner 取回日志前调用, 避免异步队列残留导致尾部日志丢失
  void drain(int timeoutMs = 1000);

 protected:
  virtual void onRunTask() override;
};

}