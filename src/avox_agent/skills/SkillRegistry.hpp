#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Skill.hpp"
#include "avox/AvoxDef.h"

namespace avox {

// skill 注册中心: 把所有 Skill 收口于一处。
// 替代原 handlers/HandlerRegistry — 不再有类层次, 只有 Skill 实例。
// 新增一条 skill = 往 builtinSkillRegistry() add 一个 Skill 实例。
class SkillRegistry {
 public:
  void add(Skill skill);
  // 按 skill 名查 (找不到返回 nullptr)
  Skill* find(const std::string& name) const;
  std::vector<Skill*> all() const;
  std::vector<std::string> names() const;

  // 给 VLM 的 system prompt: 能力目录 (cmd/skill/辅助工具) + 两层优先级 + 诊断规则。
  // skill 目录语义对齐 dsh: 只列模型可调 (invocation.modelInvocable) 的 skill,
  // 每条一行 `- `name`: description`, 加载交给单一 skill 工具。
  std::string systemPrompt() const;

 private:
  // unique_ptr 存储: push_back 扩容时只搬运指针, Skill 堆对象地址保持稳定,
  // 外部持有的 Skill* (如 SkillTool) 长期有效, 不再因 add 扩容触发 use-after-free。
  std::vector<std::unique_ptr<Skill>> skills;
};

// 内置 skill 注册中心 (Meyers 单例): 首次访问即按 DSH 二层优先级 (bundled + custom) 扫 SKILL.md 装载。
// 装配层 (composeDiagnosticAgent) 与工具层 (BuiltinTools 的 SkillTool) 共用同一份。
SkillRegistry& builtinSkillRegistry();

// 在 builtinSkillRegistry() 首次访问之前, 注入本会话的 customSkillDirs 配置 (DSH 同款
// customSkillDirs 语义)。在首次访问之后调用 = no-op, 已加载完成的 registry 不受影响。
// 顺序依赖:必须在任何对 builtinSkillRegistry() 的访问 (含 systemPrompt()) 之前调用,
// 否则 customSkillDirs 已被 frozen, 本会话该字段失效。
void prepareBuiltinSkillRegistry(std::vector<std::string> customSkillDirs);

}
