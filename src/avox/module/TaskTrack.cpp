#include "TaskTrack.hpp"

namespace avox {

TaskTrack::TaskTrack() {
  // 基类构造期 getTrackName 尚不可调，这里只登记指针、不取 name
  creatorTid = std::this_thread::get_id();
  TrackMgr::get().add(this);
}

TaskTrack::~TaskTrack() {
  // TrackMgr 为常驻不析构单例(见 get()), 退出期调用仍安全
  TrackMgr::get().remove(this);
}

void TaskTrack::addTrack(std::thread::id tid, bool bSelf) {
  std::lock_guard<std::mutex> lock(TrackMgr::get().mtx);
  addTrackLocked(tid, bSelf);
}

void TaskTrack::removeTrack(std::thread::id tid) {
  std::lock_guard<std::mutex> lock(TrackMgr::get().mtx);
  removeTrackLocked(tid);
}

bool TaskTrack::haveId(std::thread::id tid) {
  std::lock_guard<std::mutex> lock(TrackMgr::get().mtx);
  return haveIdLocked(tid);
}

void TaskTrack::addTrackLocked(std::thread::id tid, bool bSelf) {
  if (bSelf) {
    selfTid = tid;
    return;
  }
  if (!haveIdLocked(tid)) {
    tids.push_back(tid);
  }
}

void TaskTrack::removeTrackLocked(std::thread::id tid) {
  for (auto it = tids.begin(); it != tids.end(); ++it) {
    if (*it == tid) {
      tids.erase(it);
      return;
    }
  }
}

bool TaskTrack::haveIdLocked(std::thread::id tid) const {
  if (tid == selfTid) {
    return true;
  }
  for (auto& id : tids) {
    if (id == tid) {
      return true;
    }
  }
  return false;
}

TrackMgr& TrackMgr::get() {
  // 故意泄漏不析构: 若 TrackMgr 参与静态析构, exit() 后仍在跑的工作线程
  // (硬解回调线程等不可 join)打日志会锁已析构的 mtx -> pthread_mutex_lock
  // 返 EINVAL -> libc++ 抛 system_error -> 线程入口/C 回调无人接 -> abort
  // (2026-09-27 rc_seek_v3 实证)。常驻后退出期日志路径恒安全, 同 ZLM
  // Logger / RtcEngine 的泄漏策略; 静态存储随进程回收, 不算真泄漏。
  static TrackMgr* inst = new TrackMgr();
  return *inst;
}

void TrackMgr::add(TaskTrack* t) {
  std::lock_guard<std::mutex> lock(mtx);
  tracks.push_back(t);
  epoch++;
  // 不在此 refreshTags：t 可能仍在基类构造期，getTrackName 不可调
}

void TrackMgr::remove(TaskTrack* t) {
  std::lock_guard<std::mutex> lock(mtx);
  for (auto it = tracks.begin(); it != tracks.end(); ++it) {
    if (*it == t) {
      tracks.erase(it);
      break;
    }
  }
  epoch++;
}

void TrackMgr::bindTid(std::thread::id creatorTid, std::thread::id tid) {
  std::lock_guard<std::mutex> lock(mtx);
  for (auto* t : tracks) {
    if (t->haveIdLocked(creatorTid)) {
      t->addTrackLocked(tid, false);
      return;
    }
  }
}

void TrackMgr::unbindTid(std::thread::id tid) {
  std::lock_guard<std::mutex> lock(mtx);
  for (auto* t : tracks) {
    if (t->haveIdLocked(tid)) {
      t->removeTrackLocked(tid);
      return;
    }
  }
}

TaskTrack* TrackMgr::current() {
  std::thread::id tid = std::this_thread::get_id();
  std::lock_guard<std::mutex> lock(mtx);
  for (auto* t : tracks) {
    if (t->haveIdLocked(tid)) {
      return t;
    }
  }
  return nullptr;
}

std::string TrackMgr::currentTag() {
  std::thread::id tid = std::this_thread::get_id();
  std::lock_guard<std::mutex> lock(mtx);
  // tracks 有变化时才重算 tag（此时各对象已完整，getTrackName 可调）
  if (refreshedEpoch != epoch) {
    refreshTags();
    refreshedEpoch = epoch;
  }
  for (auto* t : tracks) {
    if (t->haveIdLocked(tid)) {
      return t->tag;
    }
  }
  return "";
}

void TrackMgr::refreshTags() {
  // 按 getTrackName 分组计数，同名 >1 则加序号前缀
  std::unordered_map<std::string, int> counts;
  for (auto* t : tracks) {
    counts[t->getTrackName()]++;
  }
  std::unordered_map<std::string, int> idx;
  for (auto* t : tracks) {
    std::string name = t->getTrackName();
    if (counts[name] > 1) {
      t->tag = name + std::to_string(idx[name]++);
    } else {
      t->tag.clear();
    }
  }
}

}
