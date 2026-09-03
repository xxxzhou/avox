#include "RunTask.hpp"

#include <chrono>

#include "../Avox.hpp"
#include "AvoxManager.hpp"
#include "LogHelper.hpp"
#include "TaskTrack.hpp"

namespace avox {

void sleepThread(bool yield, int32_t ms) {
  if (yield) {
    // 高优先级实时处理,密集数据包队列
    // 进入就绪态，可能直接参与下一次调度竞争,CPU占用较高
    std::this_thread::yield();
  } else {
    // 减少 CPU 空转,平衡功耗与性能
    // 进入阻塞态，休眠期间不参与调度,CPU占用低
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
}

RunTask::RunTask(/* args */) {
  runflag.store(false);
  tpause.store(false);
}

RunTask::~RunTask() { stopTask(); }

bool RunTask::startTask() {
  std::unique_lock<std::mutex> lock(taskMtx);
  if (runflag.load()) {
    log(LogLevel::info, "task:", taskName, " is already running");
    return true;
  }
  // 子类资源初始化,需要考虑资源初始化不成功的处理
  if (!onStartTask()) {
    log(LogLevel::warn, "task:", taskName, " start failed");
    return false;
  }
  runflag.store(true);
  if (thread.joinable()) {
    thread.join();
  }
  std::thread::id creatorTid = std::this_thread::get_id();
  thread = std::thread(&RunTask::runTask, this);
  std::thread::id newTid = thread.get_id();
  // 线程归属：根（自身是 TaskTrack）自锚；子任务按创建者链归入所属 TaskTrack
  if (auto* track = asTaskTrack()) {
    track->addTrack(newTid, true);
  } else {
    TrackMgr::get().bindTid(creatorTid, newTid);
  }
  log(LogLevel::info, "task thread id:", std::this_thread::get_id(),
      " start task:", taskName);
  return true;
}

void RunTask::runTask() {
  if (!runflag.load()) {
    return;
  }
  // LOGFLF(LogLevel::info, "usage memory:", getCurrentMemoryUsageKB());
  log(LogLevel::info, "task run:", taskName, " thread id ",
      std::this_thread::get_id());
  // 子类用runflag做判断
  onRunTask();
  // 清理资源
  onStopTask();
  // 非外部关闭，自身关闭也需要重置flag
  runflag.store(false);
  tpause.store(false);
#ifdef __ANDROID__
  // 退出时,退出附加当前线程
  AvoxManager::Get().detachThread();
#endif
  // LOGFLF(LogLevel::info, "usage memory:", getCurrentMemoryUsageKB());
}

void RunTask::stopTask() {
  std::unique_lock<std::mutex> lock(taskMtx);
  // 已经关闭或是没正常初始化
  if (!runflag.load()) {
    // 如果线程还是可join的
    if (thread.joinable()) {
      thread.join();
    }
    return;
  }
  // 让runTask知道结束的flag
  runflag.store(false);
  tpause.store(false);
  // 等待线程执行完
  std::thread::id childTid;
  if (thread.joinable()) {
    childTid = thread.get_id();
    thread.join();
  }
  // 解除线程归属登记（join 后线程已结束，id 可能被复用）
  if (childTid != std::thread::id()) {
    TrackMgr::get().unbindTid(childTid);
  }
  log(LogLevel::info, "task stop:", taskName,
      " thread id:", std::this_thread::get_id());
}

bool RunTask::running() { return runflag.load(); }

void RunTask::sleepTask(bool yield, int32_t ms) { sleepThread(yield, ms); }

void RunTask::addAction(const std::function<bool()>& action) {
  std::unique_lock<std::mutex> lock(taskMtx);
  actions.push_back(action);
}

void RunTask::executeActions() {
  std::unique_lock<std::mutex> lock(taskMtx);
  for (auto& action : actions) {
    action();
  }
  actions.clear();
}

void RunTask::pauseTask() {
  // 任务不在运行或是已经暂停，直接返回
  if (!running() || pauseing()) {
    return;
  }
  tpause.store(true);
  log(LogLevel::info, "task pause:", taskName,
      " thread id:", std::this_thread::get_id());
}

void RunTask::resumeTask() {
  if (tpause.load()) {
    tpause.store(false);
    log(LogLevel::info, "task resume:", taskName,
        " thread id:", std::this_thread::get_id());
  }
}

bool RunTask::pauseing() { return tpause.load(); }

LogTask::LogTask() {
  taskName = "log task";
  logQueue.setMaxSize(100);
  startTask();
}

LogTask::~LogTask() {}

void LogTask::addItem(LogLevel level, const char* message) {
  LogItem item = {};
  item.timestamp = {localTimeStampMS() * 10000};
  item.level = level;
  item.msg = message;
  addItem(item);
}

void LogTask::addItem(const LogItem& item) {
  if (logQueue.full()) {
    LogItem item = {};
    while (logQueue.dequeue(item)) {
      log(item);
    }
  }
  logQueue.enqueue(item);
}

void LogTask::onRunTask() {
  while (running()) {
    LogItem item = {};
    if (logQueue.dequeue(item)) {
      log(item);
    }
    sleepTask(logQueue.size() > 0);
  }
}

void LogTask::drain(int timeoutMs) {
  // 在调用线程同步处理完队列里现有的日志
  // 与后台 onRunTask 线程竞争 dequeue (RingBuffer 自带锁, 线程安全)
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeoutMs);
  LogItem item = {};
  while (std::chrono::steady_clock::now() < deadline) {
    if (!logQueue.dequeue(item)) {
      break;
    }
    log(item);
  }
}

}
