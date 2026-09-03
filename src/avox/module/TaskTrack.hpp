#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../AvoxDef.h"

// 线程归属追踪：大对象（MediaPlayer/SourcePlayer/TranscodeRecorder）继承 TaskTrack，
// 由 TrackMgr 维护「线程 id -> TaskTrack」映射，使多实例下日志能带实例前缀。
// RunTask 启动的工作线程自动按「创建者链」归入所属 TaskTrack；
// 第三方库自建线程（如 ZLMediaKit socket 线程）由宿主手动 bindTid 登记。

namespace avox {

class TrackMgr;

// 需要记录线程归属的大对象继承此 mixin
class TaskTrack {
 public:
  TaskTrack();
  virtual ~TaskTrack();
  // 子类实现，返回类型名，如 "MP"/"SP"/"TR"（构造期不可调，TrackMgr 延后取）
  virtual const char* getTrackName() = 0;

  // 加/移除关联线程（自加锁；bSelf=true 设自身主线程 selfTid，不入 tids）
  void addTrack(std::thread::id tid, bool bSelf = false);
  void removeTrack(std::thread::id tid);
  bool haveId(std::thread::id tid);

  std::thread::id creatorTid;         // 创建自身的线程，记录暂不用
  std::thread::id selfTid;            // 自身主线程（根，独立不入 tids）
  std::vector<std::thread::id> tids;  // 子线程集合
  std::string tag;                    // 预缓存可读前缀，如 "MP0"；单实例为空

 private:
  friend class TrackMgr;
  // 不加锁版本，仅 TrackMgr 持锁内部调用，避免递归加锁
  void addTrackLocked(std::thread::id tid, bool bSelf);
  void removeTrackLocked(std::thread::id tid);
  bool haveIdLocked(std::thread::id tid) const;
};

class TrackMgr {
 public:
  static TrackMgr& get();
  // TaskTrack 构造/析构时自动注册/注销
  void add(TaskTrack* t);
  void remove(TaskTrack* t);
  // 按「创建者链」登记：creatorTid 所属 track，tid 也归入它
  void bindTid(std::thread::id creatorTid, std::thread::id tid);
  void unbindTid(std::thread::id tid);
  // 当前线程所属 track（nullptr=无归属）
  TaskTrack* current();
  // 当前线程所属 track 的 tag（多实例如 "MP0"，单实例/无归属返回空）
  std::string currentTag();

 private:
  friend class TaskTrack;
  TrackMgr() = default;
  // 重新计算所有 track 的 tag（按 getTrackName 计数，同名>1 加序号）
  void refreshTags();
  std::mutex mtx;
  std::vector<TaskTrack*> tracks;
  size_t epoch = 0;          // tracks 变化计数
  size_t refreshedEpoch = 0; // tag 已刷新到的 epoch
};

}
