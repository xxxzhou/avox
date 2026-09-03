#include "TeamActivity.hpp"

#include <chrono>
#include <utility>

namespace avox {

namespace {
// dsh 的合法等待区间: 10 秒到 1 小时。
constexpr int64_t kMinTimeoutMs = 10000;
constexpr int64_t kMaxTimeoutMs = 3600000;
}  // namespace

TeamWaitResult TeamActivity::wait(int64_t timeoutMs) {
  if (timeoutMs < kMinTimeoutMs || timeoutMs > kMaxTimeoutMs) {
    throw TeamError("wait timeout must be between 10000 and 3600000 ms",
                    TEAM_INVALID_TIMEOUT);
  }
  return poll(timeoutMs);
}

TeamWaitResult TeamActivity::poll(int64_t maxMs) {
  std::unique_lock<std::mutex> lock(mtx);
  const uint64_t observed = changeEpoch;
  const auto changed = [this, observed]() {
    return closed || changeEpoch != observed;
  };
  // 队伍已销毁: 立即抛, 不进入等待。
  if (closed) {
    throw TeamError("team disposed while waiting", TEAM_WAIT_ABORTED);
  }
  const bool woken =
      cv.wait_for(lock, std::chrono::milliseconds(maxMs), changed);
  if (closed) {
    throw TeamError("team disposed while waiting", TEAM_WAIT_ABORTED);
  }
  TeamWaitResult result;
  result.timedOut = !woken;
  return result;
}

void TeamActivity::notifyChanged() {
  std::lock_guard<std::mutex> lock(mtx);
  ++changeEpoch;
  cv.notify_all();
}

void TeamActivity::close() {
  std::lock_guard<std::mutex> lock(mtx);
  closed = true;
  cv.notify_all();
}

}
