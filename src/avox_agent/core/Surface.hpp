#pragma once

// ============================================================================
// 有序模型可见表面 (surface): 派生历史的唯一来源。
//
// 对齐 dsh 的 packages/core/session/src/surface.ts。
//
// 为什么需要「表面」这一层: 模型历史不是按事件顺序推出来的。每个产出消息的 append 都
// 必须显式声明 surfaceOp —— 追加到尾部, 或用本节点替换一段既存节点。于是:
//   * 原始流分片、turn/step 边界、header 快照天然不在模型历史里 (它们没有 surface 标记);
//   * 压缩只是往 surface 写一个 replace, 不删任何事件 —— 审计与回放仍看得到原始轮次,
//     而模型历史立刻变短。
//
// 坐标约定 (最容易搞错的地方): nodes 是 **事件 seq** 的有序列表, SurfaceReplace 的
// start/end 也是 seq。节点下标会随每次 replace 漂移, seq 永久稳定。
// ============================================================================

#include <cstddef>
#include <optional>
#include <vector>

#include "SessionTypes.hpp"

namespace avox {

// ---------------------------------------------------------------------------
// 投影
// ---------------------------------------------------------------------------

// 把一条事件投影成模型历史里的一条消息; 不产出消息则返回 nullopt。
//
// 这是 surface 投影的全部规则, 且是纯函数: 活 surface、外部重建方与诊断工具折叠同一个
// 函数, 就能重建出任何一次请求当初依据的确切消息。
//
// 有意非穷尽: 只有产出消息的事件参与派生历史, 边界标记、分片、仅记日志的记录都投影为空。
std::optional<Message> deriveEventMessage(const SessionEvent& event);

// ---------------------------------------------------------------------------
// 折叠结果
// ---------------------------------------------------------------------------

// 折叠会话表面时观察到的一次替换操作。
struct SurfaceFoldReplacement {
  // 执行替换的那个事件的 seq。
  size_t seq = 0;
  // 声明的被替换区间起点 seq (含)。
  size_t start = 0;
  // 声明的被替换区间终点 seq (含)。
  size_t end = 0;
  // 实际被移除的表面节点, 按表面顺序。
  std::vector<size_t> shadowedSeqs;
};

struct SurfaceFoldResult {
  // 当前表面上的事件 seq, 模型可见顺序。
  std::vector<size_t> nodes;
  // 替换操作, 按事件顺序。
  std::vector<SurfaceFoldReplacement> replacements;
};

// 把一份完整日志重放过一遍规范折叠。
//
// 用途: seed 校验 (resume / fork 进来的日志必须能过同一套不变式) 与诊断。
// 事件违反 surface 元数据、来源引用、区间或 tool/result 重写规则时抛 std::runtime_error。
SurfaceFoldResult foldSurface(const std::vector<SessionEvent>& events);

// ---------------------------------------------------------------------------
// 增量表面
// ---------------------------------------------------------------------------

// 增量维护的有序表面视图 + append 边界校验器。
//
// 生命周期: 借用日志容器的引用, 由 Session 持有并与日志同生共死。构造后不得让被引用的
// 容器先于本对象销毁。
class SurfaceManager {
 public:
  // eventLog: 借用引用, 必须比本对象活得久。
  explicit SurfaceManager(const std::vector<SessionEvent>& eventLog)
      : eventLog(eventLog) {}

  // 当前表面上的事件 seq, 模型可见顺序 (读取时惰性追平日志)。
  const std::vector<size_t>& nodes();

  // 已提交的定位替换次数, 单调递增。派生历史缓存拿它判断是否需要整体重建 ——
  // 一次 replace 会重写表面中段, 增量续投影不再成立。
  size_t replaceGeneration();

  // 校验一条候选事件能否进入表面, 并在通过时提交状态变更。
  //
  // 必须在事件进入日志之前调用: 候选在入库前先完成规划, 于是校验失败不会让表面处于
  // 半变更状态。
  //
  // expectedSeq 是该事件将要占据的 seq (即当前日志长度), 用于校验信封自洽。
  // 违反任一规则时抛 std::runtime_error, 且不改变任何状态。
  void validateNext(const SessionEvent& event, size_t expectedSeq);

 private:
  // 惰性追平: 把日志里尚未处理的尾部折叠进来。
  void processDelta();

  // 命名注意: 不能叫 log —— 通用日志宏 LOGFLF 展开成对自由函数 log(...) 的调用,
  // 同名成员会遮蔽它。
  const std::vector<SessionEvent>& eventLog;
  std::vector<size_t> nodesState;
  size_t replaceGenerationState = 0;
  // 已处理到的日志长度 (而非 seq, 免去「尚未处理任何事件」的哨兵值)。
  size_t processedCount = 0;
};

}
