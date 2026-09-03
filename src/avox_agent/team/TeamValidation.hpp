#pragma once

// ============================================================================
// 名册与任务命令共享的入参规范化。
//
// 对齐 dsh packages/experimental/agent-team/src/validation.ts (+ roster.ts 的
// MEMBER_NAME 校验, 收敛于此避免头文件互相引用)。
// ============================================================================

#include <string>

#include "TeamError.hpp"

namespace avox {

// 规范化一段必填的人写文本: trim、非空、不超长。违规抛 TeamError
// (TEAM_INVALID_ARGUMENT)。
std::string teamRequiredText(const std::string& value, const char* field,
                             size_t maxLength);

// 校验队友名 (dsh roster MEMBER_NAME): /^[a-z0-9]+(?:-[a-z0-9]+)*$/, 长度 <=64,
// 不得为 "lead"。违规抛 TeamError (TEAM_INVALID_MEMBER_NAME)。
void checkTeamMemberName(const std::string& name);

// 规范化一个工作区相对路径前缀 (不作为锁, 仅提示重叠)。反斜杠转正斜杠、去 "./"
// 前缀与尾斜杠; 拒绝空/绝对路径/盘符/空段/'.'/'..'。违规抛 TeamError
// (TEAM_INVALID_WRITE_SCOPE)。
std::string teamWriteScope(const std::string& value);

}
