#include "TeamTaskGraph.hpp"

#include <set>
#include <unordered_set>
#include <utility>

namespace avox {

void assertTeamTaskGraphCandidate(
    const std::map<std::string, TeamTaskSnapshot>& current,
    const TeamTaskSnapshot& candidate) {
  std::map<std::string, TeamTaskSnapshot> tasks = current;
  tasks[candidate.id] = candidate;

  for (const auto& [id, task] : tasks) {
    if (task.status == TeamTaskStatus::Deleted) continue;
    std::set<std::string> seen;
    for (const std::string& blockerId : task.blockedBy) {
      if (blockerId == task.id) {
        throw TeamTaskGraphError("team task \"" + task.id
                                 + "\" cannot block itself",
                                 TeamTaskGraphViolation::Cycle);
      }
      if (!seen.insert(blockerId).second) {
        throw TeamTaskGraphError("team task \"" + task.id
                                 + "\" repeats blocker \"" + blockerId + "\"",
                                 TeamTaskGraphViolation::Duplicate);
      }
      const auto blocker = tasks.find(blockerId);
      if (blocker == tasks.end()
          || blocker->second.status == TeamTaskStatus::Deleted) {
        throw TeamTaskGraphError(
            "blocker task \"" + blockerId + "\" for \"" + task.id
                + "\" is missing or deleted",
            TeamTaskGraphViolation::Missing);
      }
    }
  }

  // DFS 环检测 (迭代实现: 显式栈 + 路径集)。
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  for (const auto& [startId, start] : tasks) {
    if (visited.count(startId) || start.status == TeamTaskStatus::Deleted) {
      continue;
    }
    // 栈元素: 任务 id + 是否已展开子依赖 (后序出栈)。
    std::vector<std::pair<std::string, bool>> stack;
    stack.emplace_back(startId, false);
    while (!stack.empty()) {
      auto [id, expanded] = stack.back();
      stack.pop_back();
      if (expanded) {
        visiting.erase(id);
        visited.insert(id);
        continue;
      }
      if (visiting.count(id)) {
        throw TeamTaskGraphError("task dependency cycle includes \"" + id + "\"",
                                 TeamTaskGraphViolation::Cycle);
      }
      if (visited.count(id)) continue;
      const auto entry = tasks.find(id);
      if (entry == tasks.end()
          || entry->second.status == TeamTaskStatus::Deleted) {
        continue;
      }
      stack.emplace_back(id, true);
      visiting.insert(id);
      for (const std::string& blockerId : entry->second.blockedBy) {
        if (!visited.count(blockerId)) stack.emplace_back(blockerId, false);
      }
    }
  }
}

}
