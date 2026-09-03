#include "Skill.hpp"

#include <string>

#include "avox/module/Json.hpp"                 // jsonEscape (runSkill)
#include "avox_agent/skills/SkillRegistry.hpp"  // builtinSkillRegistry (runSkill 查 skill)

namespace avox {

bool isSkillName(const std::string& name) {
  // /^[a-z0-9]+(?:-[a-z0-9]+)*$/ — 每段 [a-z0-9]+, 由单个 '-' 分隔。
  if (name.empty()) return false;
  size_t start = 0;
  while (true) {
    if (start >= name.size()) return false;  // 尾随 '-' 或空段
    const size_t end = name.find('-', start);
    const std::string seg =
        name.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (seg.empty()) return false;  // 前导/连续 '-' ("-a" / "a--b")
    for (const char ch : seg) {
      const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
      if (!ok) return false;
    }
    if (end == std::string::npos) return true;
    start = end + 1;
  }
}

namespace {

// name 属性转义 (对齐 dsh escapeAttr): '&' '"' '<'。
std::string escapeAttr(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '<': out += "&lt;"; break;
      default: out += ch;
    }
  }
  return out;
}

// <skill_resources> 段的内容 (对齐 dsh renderResourceHint)。
std::string renderResourceHint(const Skill& skill) {
  std::string lines;
  const SkillResourceBase& base = skill.resourceBase;
  if (base.kind.empty()) {
    lines += "Resources for this skill are managed by provider \"" +
             escapeSkillText(skill.provider) + "\".\n";
    lines += "Load referenced resources only as needed.";
  } else if (base.kind == "directory") {
    lines += "Base directory for this skill: " + escapeSkillText(base.path) + "\n";
    lines += "Resolve relative paths mentioned by this skill against the base directory "
             "before using them. Load referenced resources only as needed.";
  } else if (base.kind == "url") {
    lines += "Base URL for this skill: " + escapeSkillText(base.url) + "\n";
    lines += "Resolve relative URLs mentioned by this skill against the base URL before "
             "using them. Load referenced resources only as needed.";
  } else {  // opaque
    lines += "Resources for this skill: " + escapeSkillText(base.description) + "\n";
    lines += "Load referenced resources only as needed.";
  }
  return lines;
}

}  // namespace

std::string escapeSkillText(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += ch;
    }
  }
  return out;
}

std::string Skill::renderSkillContent() const {
  std::string s;
  s += "<skill_content name=\"" + escapeAttr(name) + "\">\n";
  s += "<skill_resources>\n";
  s += renderResourceHint(*this);
  s += "\n</skill_resources>\n";
  s += "\n";
  s += "<skill_instructions>\n";
  s += content;  // 正文逐字嵌入 (skill 是受信本地内容)
  s += "\n</skill_instructions>\n";
  s += "</skill_content>";
  return s;
}

// ============== C 导出 ==============
// runSkill: 脱离模型直接读一条预定义 skill, 返回 {skill, output} JSON。
// 对齐 dsh 的 skill 工具语义: skill 只是加载正文, 由调用方 (模型/脚本) 按指令执行;
// output = renderSkillContent() 的 <skill_content> 块, static thread_local 托管, 无需释放。
const char* runSkill(const char* skillName, const char* userInput) {
  static thread_local std::string buf;
  const std::string name = skillName ? skillName : "";
  Skill* sk = builtinSkillRegistry().find(name);
  std::string output;
  if (sk == nullptr) {
    output = "FAIL: skill not found: " + name;
  } else if (!sk->invocation.modelInvocable) {
    output = "FAIL: skill not available for model invocation: " + name;
  } else {
    output = sk->renderSkillContent();
  }
  buf = "{\"skill\":\"" + jsonEscape(name) + "\",\"output\":\"" + jsonEscape(output) + "\"}";
  return buf.c_str();
}

}
