#pragma once

namespace avox {

// 字幕三槽位(内封轨/外挂文件/ASR)仲裁状态机(计划 doc/plan/player/字幕模块合并计划.md):
// 后激活者胜 —— 任一时刻至多一个槽位上屏, 胜者空窗就空屏不回落。
// 状态机只记胜者; 槽位的具体拆除(关层/卸轨/停识别)由调用方按 activate 返回的
// 被顶掉槽位执行, 返回值与入参相同 = 重复激活, 调用方免拆除。
class SubtitleSlots {
 public:
  enum class Slot { none, track, file, asr };

  Slot active() const { return active_; }

  // 激活槽位, 返回被顶掉的原胜者(== slot 时重复激活, 调用方免拆除)。
  // slot 传 none 等价 reset。
  Slot activate(Slot slot) {
    if (slot == Slot::none) {
      return reset();
    }
    const Slot prev = active_;
    active_ = slot;
    return prev;
  }

  // 仅当目标槽位本就是胜者时清空(如 setSubtitleTrack(-1) 关轨不影响外挂),
  // 返回清理前的胜者。
  Slot deactivateIf(Slot slot) {
    const Slot prev = active_;
    if (active_ == slot) {
      active_ = Slot::none;
    }
    return prev;
  }

  // 无条件清空(播放器 close/换源时), 返回清理前的胜者
  Slot reset() {
    const Slot prev = active_;
    active_ = Slot::none;
    return prev;
  }

 private:
  Slot active_ = Slot::none;
};

}
