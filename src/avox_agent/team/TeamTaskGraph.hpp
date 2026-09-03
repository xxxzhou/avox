#pragma once

// ============================================================================
// 共享任务依赖图的完备校验。
//
// 对齐 dsh packages/experimental/agent-team/src/task-graph.ts。
// 命令层 (TeamTaskBoard) 把这里的违规类别映射成稳定错误码。
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include "avox_agent/core/SessionTypes.hpp"
#include "TeamError.hpp"

namespace avox {

// 依赖关系违规类别 (dsh TeamTaskGraphViolation)。
enum class TeamTaskGraphViolation { Missing, Duplicate, Cycle };

// 包内任务依赖失败 (由命令层捕获并映射错误码)。
class TeamTaskGraphError : public std::runtime_error {
 public:
  TeamTaskGraphError(std::string message, TeamTaskGraphViolation violation)
      : std::runtime_error(std::move(message)), violation(violation) {}

  TeamTaskGraphViolation violation;
};

// 用候选快照替换同 id 项后, 校验整个活跃任务图: 依赖不缺、不重、不自指、不成环。
// dsh assertTaskGraphCandidate 的逐条对应。
void assertTeamTaskGraphCandidate(
    const std::map<std::string, TeamTaskSnapshot>& current,
    const TeamTaskSnapshot& candidate);

}
