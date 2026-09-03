#pragma once

#include <string>
#include "avox/AvoxDef.h"
#include "../AgentExport.h"

namespace avox {

// 判断一个字符串是否合法的 kebab-case skill 名。
// 对齐 dsh 的 public skill-name grammar: /^[a-z0-9]+(?:-[a-z0-9]+)*$/。
bool isSkillName(const std::string& name);

// 模型面散文的安全嵌入 (对齐 dsh 的 escapeText): 把 '&' '<' '>' 转义, 防止提供方文本
// 开合 <skill_*> 框架标签。skill 目录渲染与 <skill_content> 渲染共用。
std::string escapeSkillText(const std::string& value);

// 调用侧开关 (对齐 dsh 的 SkillInvocationPolicy)。
struct SkillInvocation {
  bool modelInvocable = true;   // 模型 (skill 工具/目录) 侧是否可调
  bool userInvocable = true;    // 用户侧 (/name 手势) 是否可调
};

// 资源基址 (对齐 dsh 的 SkillResourceBase)。skill 正文里的相对资源以此解析。
struct SkillResourceBase {
  std::string kind;        // "directory" | "url" | "opaque"; 空 = 无
  std::string path;        // kind == "directory" 时的绝对目录
  std::string url;         // kind == "url" 时的基址 URL
  std::string description; // kind == "opaque" 时的说明
};

// skill 实体 — 一份从 SKILL.md 解析出的完整定义 (对齐 dsh 的 SkillDefinition)。
// 命名、front-matter 契约、渲染格式与 dsh 逐项对应, 同一份 SKILL.md 资产两侧共用:
//   * name/description 必填; whenToUse 可选; 其余未知 key 容忍 (不报错、不存储)
//   * 正文 = content, 由 renderSkillContent() 渲染成模型可见的 <skill_content> 块
class Skill {
 public:
  std::string name;                    // kebab-case, 如 "diagnose-play"
  std::string description;             // 路由描述
  std::string whenToUse;               // 可选补充路由指引
  SkillInvocation invocation;          // 默认 model/user 两侧都可调
  std::string source;                  // 来源桶, 如 "bundled" (提示可见元数据, 不参与优先级)
  std::string provider;                // 提供方, 如 "local"
  SkillResourceBase resourceBase;      // 可选; kind 空 = 无
  std::string path;                    // 来自磁盘时的绝对路径
  std::string content;                 // front-matter 之后的正文 (Markdown 指令)

  // 渲染为模型可见的 <skill_content> 块 (对齐 dsh 的 renderSkillContent)。
  // skill 工具与 runSkill 导出共用, 两条路给模型同一份正文。
  std::string renderSkillContent() const;
};

}
