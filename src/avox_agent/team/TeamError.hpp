#pragma once

// ============================================================================
// Agent Teams 的稳定错误身份。
//
// 对齐 dsh packages/experimental/agent-team/src/error.ts。
//
// 域层用异常表达拒绝 (校验、限额、CAS 失败), 工具层捕获并折叠成模型可见的
// ToolResult (code = 这里的稳定码) —— 与 dsh 的 TeamError.code 一一对应,
// 模型可依码自愈, 测试可依码断言。
// ============================================================================

#include <stdexcept>
#include <string>
#include <utility>

#include "avox/AvoxDef.h"

namespace avox {

// dsh TeamError: message 面向人, code 面向程序。
class TeamError : public std::runtime_error {
 public:
  TeamError(std::string message, std::string errorCode)
      : std::runtime_error(std::move(message)), code(std::move(errorCode)) {}

  // 稳定路由码 (TEAM_*), 与 dsh agent-team 的抛出点逐字一致。
  const std::string code;
};

// ---- 稳定码词汇表 (dsh error.ts 的全集) ----

// 名字非法 (格式/长度/保留名)。
inline constexpr const char* TEAM_INVALID_MEMBER_NAME = "TEAM_INVALID_MEMBER_NAME";
// 名字已被本队伍用过 —— 名字永不复用 (fold 不变式)。
inline constexpr const char* TEAM_MEMBER_NAME_TAKEN = "TEAM_MEMBER_NAME_TAKEN";
// 队伍成员数达到上限。
inline constexpr const char* TEAM_MEMBER_LIMIT = "TEAM_MEMBER_LIMIT";
// 按名字找不到 active 队友。
inline constexpr const char* TEAM_MEMBER_NOT_FOUND = "TEAM_MEMBER_NOT_FOUND";
// 仅 Lead 可执行的操作。
inline constexpr const char* TEAM_LEAD_REQUIRED = "TEAM_LEAD_REQUIRED";
// 目标不能是自己 (自发送无意义: 同会话内说话不经邮箱)。
inline constexpr const char* TEAM_SELF_MESSAGE = "TEAM_SELF_MESSAGE";
// 投递目标不合法 (如 interrupt 自己)。
inline constexpr const char* TEAM_INVALID_TARGET = "TEAM_INVALID_TARGET";
// 目标成员的待投递消息数达到上限。
inline constexpr const char* TEAM_MAILBOX_FULL = "TEAM_MAILBOX_FULL";
// 完整投递帧 (含框头) 超过字节上限。
inline constexpr const char* TEAM_MESSAGE_TOO_LARGE = "TEAM_MESSAGE_TOO_LARGE";
// 队伍共享任务数达到上限。
inline constexpr const char* TEAM_TASK_LIMIT = "TEAM_TASK_LIMIT";
// 任务 id 不存在 (或已删除后取值)。
inline constexpr const char* TEAM_TASK_NOT_FOUND = "TEAM_TASK_NOT_FOUND";
// CAS 前提失败: revision 已前进。
inline constexpr const char* TEAM_TASK_STALE_REVISION = "TEAM_TASK_STALE_REVISION";
// 任务已删除 (墓碑), 不再接受任何变更。
inline constexpr const char* TEAM_TASK_DELETED = "TEAM_TASK_DELETED";
// 变更需要任务 owner 或 Lead。
inline constexpr const char* TEAM_TASK_UNAUTHORIZED = "TEAM_TASK_UNAUTHORIZED";
// 任务已被别人持有。
inline constexpr const char* TEAM_TASK_ALREADY_CLAIMED =
    "TEAM_TASK_ALREADY_CLAIMED";
// 依赖未完成, 不能 claim/reassign。
inline constexpr const char* TEAM_TASK_BLOCKED = "TEAM_TASK_BLOCKED";
// 状态机不允许该迁移。
inline constexpr const char* TEAM_TASK_INVALID_TRANSITION =
    "TEAM_TASK_INVALID_TRANSITION";
// 还有任务依赖它, 不能删除。
inline constexpr const char* TEAM_TASK_HAS_DEPENDENTS = "TEAM_TASK_HAS_DEPENDENTS";
// 依赖图成环 (含自依赖)。
inline constexpr const char* TEAM_TASK_DEPENDENCY_CYCLE =
    "TEAM_TASK_DEPENDENCY_CYCLE";
// 通用入参错误 (缺字段/重复 blocker/空文本等)。
inline constexpr const char* TEAM_INVALID_ARGUMENT = "TEAM_INVALID_ARGUMENT";
// write scope 不是合法的工作区相对前缀。
inline constexpr const char* TEAM_INVALID_WRITE_SCOPE = "TEAM_INVALID_WRITE_SCOPE";
// wait 超时区间非法。
inline constexpr const char* TEAM_INVALID_TIMEOUT = "TEAM_INVALID_TIMEOUT";
// 等待期间队伍被销毁。
inline constexpr const char* TEAM_WAIT_ABORTED = "TEAM_WAIT_ABORTED";
// 队伍已销毁, 拒绝新操作。
inline constexpr const char* TEAM_DISPOSED = "TEAM_DISPOSED";
// 销毁收尾超时 (成员未在预算内静止)。
inline constexpr const char* TEAM_DISPOSAL_TIMEOUT = "TEAM_DISPOSAL_TIMEOUT";
// provisioning 收敛冲突 (冷恢复时描述符/初始提示不符)。
inline constexpr const char* TEAM_PROVISIONING_CONFLICT =
    "TEAM_PROVISIONING_CONFLICT";

}
