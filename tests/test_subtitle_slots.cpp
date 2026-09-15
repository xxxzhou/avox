// 字幕三槽位仲裁状态机单测(计划 doc/plan/player/字幕模块合并计划.md)
// 规则: 后激活者胜, 任一时刻至多一个槽位; 重复激活免拆除(activate 返回值
// == 入参); deactivateIf 只在目标槽位本就是胜者时清空(关轨不影响外挂胜者)。
#include <doctest.h>

#include "avox/subtitle/SubtitleSlots.hpp"

namespace {

using avox::SubtitleSlots;
using Slot = SubtitleSlots::Slot;

// 三槽位全排列(激活顺序无关性: 胜者始终是最后激活者)
TEST_CASE("字幕三槽位: 全排列激活, 胜者为最后激活者") {
  const Slot perm[6][3] = {
      {Slot::track, Slot::file, Slot::asr},  {Slot::track, Slot::asr, Slot::file},
      {Slot::file, Slot::track, Slot::asr},  {Slot::file, Slot::asr, Slot::track},
      {Slot::asr, Slot::track, Slot::file},  {Slot::asr, Slot::file, Slot::track},
  };
  for (const auto& p : perm) {
    CAPTURE((int)p[0]);
    CAPTURE((int)p[1]);
    CAPTURE((int)p[2]);
    SubtitleSlots s;
    CHECK(s.active() == Slot::none);
    // 第一步: 从 none 激活, 被顶掉者为 none
    CHECK(s.activate(p[0]) == Slot::none);
    CHECK(s.active() == p[0]);
    // 第二步: 被顶掉者为第一步的槽位
    CHECK(s.activate(p[1]) == p[0]);
    CHECK(s.active() == p[1]);
    // 第三步: 被顶掉者为第二步的槽位, 胜者为最后激活者
    CHECK(s.activate(p[2]) == p[1]);
    CHECK(s.active() == p[2]);
  }
}

TEST_CASE("字幕三槽位: 重复激活免拆除") {
  SubtitleSlots s;
  s.activate(Slot::file);
  // 已是胜者再激活: 返回值==入参, 调用方据此跳过拆除
  CHECK(s.activate(Slot::file) == Slot::file);
  CHECK(s.active() == Slot::file);

  s.activate(Slot::asr);
  CHECK(s.activate(Slot::asr) == Slot::asr);
  CHECK(s.active() == Slot::asr);

  s.activate(Slot::track);
  CHECK(s.activate(Slot::track) == Slot::track);
  CHECK(s.active() == Slot::track);
}

TEST_CASE("字幕三槽位: deactivateIf 只清本就是胜者的槽位") {
  SUBCASE("关轨不影响外挂胜者") {
    SubtitleSlots s;
    s.activate(Slot::file);
    // setSubtitleTrack(-1): 轨不是胜者, 胜者保持
    CHECK(s.deactivateIf(Slot::track) == Slot::file);
    CHECK(s.active() == Slot::file);
  }
  SUBCASE("关轨且轨是胜者") {
    SubtitleSlots s;
    s.activate(Slot::track);
    CHECK(s.deactivateIf(Slot::track) == Slot::track);
    CHECK(s.active() == Slot::none);
  }
  SUBCASE("关 ASR 且 ASR 是胜者") {
    SubtitleSlots s;
    s.activate(Slot::asr);
    CHECK(s.deactivateIf(Slot::asr) == Slot::asr);
    CHECK(s.active() == Slot::none);
  }
  SUBCASE("空态下关任意槽位") {
    SubtitleSlots s;
    CHECK(s.deactivateIf(Slot::track) == Slot::none);
    CHECK(s.active() == Slot::none);
  }
}

TEST_CASE("字幕三槽位: reset 无条件清空") {
  SubtitleSlots s;
  s.activate(Slot::track);
  CHECK(s.reset() == Slot::track);
  CHECK(s.active() == Slot::none);
  // 空态 reset 幂等
  CHECK(s.reset() == Slot::none);
}

TEST_CASE("字幕三槽位: 胜者拆掉后可再激活(空窗不回落)") {
  SubtitleSlots s;
  s.activate(Slot::file);
  s.activate(Slot::track);
  s.deactivateIf(Slot::track);
  // 轨关掉后不回落到 file, 下一次激活才产生新胜者
  CHECK(s.active() == Slot::none);
  CHECK(s.activate(Slot::file) == Slot::none);
  CHECK(s.active() == Slot::file);
}

}
