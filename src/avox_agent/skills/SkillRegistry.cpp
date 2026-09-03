#include "SkillRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>  // std::error_code (fs::error_code 的别名, MSVC 需显式包含)
#include <utility>   // std::move

#include <filesystem>

#include "avox/Avox.hpp"                  // getAvoxPath (binary 目录, skill 根以此解析)
#include "avox/module/AssetLoader.hpp"   // getSystemConfigPath (骨架文件 user-level 覆盖)
#include "avox/module/LogHelper.hpp"     // LOGFLF (skill 装载诊断日志)

namespace avox {

namespace {

// 通用辅助工具的名字与一句话说明。
//
// 写成常量表而不是从 AgentHost 的注册表反查, 是因为**能力目录进的是 system prompt 前缀**:
// 它必须逐字稳定, 而注册表的内容取决于装配。目录描述与实际可调工具的偏差由工具 schema
// 兜底 —— 模型看得到真实的工具列表。
struct HelperToolDoc {
  const char* name;
  const char* summary;
};

constexpr HelperToolDoc kHelperTools[] = {
    {"read", "读取大内容的指定行范围, 返回带行号内容, 用于分段分析"},
    {"grep", "按正则搜索文件行, 返回匹配行号与片段, 在大日志里快速定位"},
    {"run_code", "执行 python 代码或脚本 (可 import avox 调截图/OCR/输入/播放)"},
    {"skill", "按 skill 名加载完整指令 (name 必须与 skill 目录里的名字一字不差)"},
};

// 取一段文本的首行 (原在 AgentContext 里, 随它一并迁来 —— 这里是唯一的消费者)。
std::string firstLine(const std::string& text) {
  const size_t end = text.find('\n');
  std::string line = end == std::string::npos ? text : text.substr(0, end);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line;
}

std::string trimSpaces(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// ASCII 小写化 (UTF-8 安全: 多字节序列不含 0x00-0x7F, 逐字节 tolower 只动 ASCII 字母)。
std::string lowerAscii(std::string s) {
  for (char& ch : s) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return s;
}

// front-matter 布尔 (对齐 dsh 的 frontmatterBoolean): true/yes/on/1 → true;
// false/no/off/0 → false; 其余 undefined (视为未提供)。
std::optional<bool> frontmatterBoolean(const std::string& value) {
  const std::string v = lowerAscii(trimSpaces(value));
  if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
  if (v == "false" || v == "no" || v == "off" || v == "0") return false;
  return std::nullopt;
}

// ========== skill (.md) 解析 (对齐 dsh 的 skill-filesystem) ==========
// SKILL.md: 首行 `---` ... 某行 `---` 为 front-matter (key: value), 其后为正文。
// 契约与 dsh 逐项对应: name/description 必填; whenToUse 可选; disable-model-invocation /
// user-invocable 可选布尔; 其余未知 key 容忍 (不报错、不存储)。name 必须满足 isSkillName。
struct ParsedSkillMd {
  std::string name;
  std::string description;
  std::string whenToUse;
  SkillInvocation invocation;   // 默认两侧都可调
  std::string body;             // front-matter 之后的正文 (Markdown 指令)
};

// 解析 SKILL.md 内容到 out。首行非 `---` / 未闭合 / 缺 name 或 description / 非法名 → false。
static bool parseSkillMd(const std::string& content, ParsedSkillMd& out) {
  std::vector<std::string> lines;
  {
    std::stringstream ss(content);
    std::string l;
    while (std::getline(ss, l)) lines.push_back(l);
  }
  if (lines.empty() || trimSpaces(lines[0]) != "---") return false;  // 首行必须 ---

  size_t i = 1;
  std::vector<std::string> fm;
  bool closed = false;
  for (; i < lines.size(); ++i) {
    if (trimSpaces(lines[i]) == "---") {
      closed = true;
      ++i;
      break;
    }
    fm.push_back(lines[i]);
  }
  if (!closed) return false;                              // 未闭合 ---

  std::string body;
  for (; i < lines.size(); ++i) {
    body += lines[i];
    body += "\n";
  }
  out.body = trimSpaces(body);

  for (const std::string& line : fm) {
    const std::string t = trimSpaces(line);
    if (t.empty() || t[0] == '#') continue;               // 空行 / 注释
    const size_t colon = t.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = trimSpaces(t.substr(0, colon));
    const std::string val = trimSpaces(t.substr(colon + 1));
    if (key == "name") {
      out.name = val;
    } else if (key == "description") {
      out.description = val;
    } else if (key == "whenToUse") {
      out.whenToUse = val;
    } else if (key == "disable-model-invocation") {
      const auto b = frontmatterBoolean(val);
      if (b.has_value()) out.invocation.modelInvocable = !*b;
    } else if (key == "user-invocable") {
      const auto b = frontmatterBoolean(val);
      if (b.has_value()) out.invocation.userInvocable = *b;
    }
    // 其余未知 key 容忍 (dsh 约定), 不报错不存储。
  }
  if (out.name.empty() || out.description.empty()) return false;
  if (!isSkillName(out.name)) return false;               // 非法 skill 名整条丢弃
  return true;
}

// 扫描一个 skill 根下每个子目录的 SKILL.md, 解析 front-matter 注册为 Skill (一切皆 skill)。
// 对齐 dsh: source=传入的 sourceTag, provider="filesystem",
// resourceBase={kind:"directory", path:<目录>}, path=<SKILL.md 绝对路径>, content=正文。
// 同名不覆盖 (先加载者胜)。调用方按 DSH 优先级从高到低依次传入多个根, 高优先级先加载即胜出。
static void loadSkillsFromRoot(SkillRegistry& reg, const std::filesystem::path& root,
                               const std::string& sourceTag) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return;
  for (fs::directory_iterator it(root, ec), end; it != end; ++it) {
    if (ec) break;
    if (!it->is_directory()) continue;                    // 每个 skill 一个子目录
    fs::path skillMd = it->path() / "SKILL.md";
    if (!fs::exists(skillMd, ec)) continue;
    std::ifstream f(skillMd, std::ios::binary);
    if (!f) continue;
    std::stringstream ss;
    ss << f.rdbuf();
    ParsedSkillMd pm;
    if (!parseSkillMd(ss.str(), pm)) {
      LOGFLF(LogLevel::warn, "[skills] 忽略 skill 文件: ", skillMd.string().c_str());
      continue;
    }
    if (reg.find(pm.name)) continue;                      // 同名不覆盖 (高优先级先加载即胜出)
    Skill c;
    c.name = pm.name;
    c.description = pm.description;
    c.whenToUse = pm.whenToUse;
    c.invocation = pm.invocation;
    c.source = sourceTag;                                 // DSH source 标签透出
    c.provider = "filesystem";
    c.resourceBase.kind = "directory";
    std::error_code ec2;
    fs::path canonDir = fs::weakly_canonical(it->path(), ec2);
    c.resourceBase.path = ec2 ? it->path().string() : canonDir.string();
    fs::path canonMd = fs::weakly_canonical(skillMd, ec2);
    c.path = ec2 ? skillMd.string() : canonMd.string();
    c.content = pm.body;
    reg.add(std::move(c));
    LOGFLF(LogLevel::info, "[skills] 装载 ", pm.name.c_str(), " <- ", c.path.c_str(),
           " (", sourceTag.c_str(), ")");
  }
}

}  // namespace

void SkillRegistry::add(Skill skill) {
  skills.push_back(std::make_unique<Skill>(std::move(skill)));
}

Skill* SkillRegistry::find(const std::string& name) const {
  for (const auto& c : skills) {
    if (c->name == name) return c.get();
  }
  return nullptr;
}

std::vector<Skill*> SkillRegistry::all() const {
  std::vector<Skill*> out;
  out.reserve(skills.size());
  for (const auto& c : skills) out.push_back(c.get());
  return out;
}

std::vector<std::string> SkillRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(skills.size());
  for (const auto& c : skills) out.push_back(c->name);
  return out;
}

// 给 VLM 的 system prompt: 从外部骨架文件读 + 动态拼能力目录。
// 骨架文件: assets/agent/system_prompt.md (或 getSystemConfigPath()/agent/system_prompt.md 覆盖),
// 含 {{能力目录}} 占位符, 替换为 skill/helper 两栏清单 (GUI 操作入口摘要在骨架文件里写死)。
// skill 目录语义对齐 dsh: 只列模型可调 (invocation.modelInvocable) 的 skill, 每条一行
// `- `name`: description` (description 做文本转义), 加载交给单一 skill 工具。
std::string SkillRegistry::systemPrompt() const {
  // skill 名集合: 枚举辅助工具时排除同名者 (已在上面的 skill 栏, 不重复进辅助工具)。
  std::set<std::string> skillNames;
  for (const auto& c : skills) skillNames.insert(c->name);

  // 读外部骨架文件: user-level (可被项目打包外的个性化覆盖) → binary-relative (随项目走)。
  // 不再用 CWD-相对 (web UI 进程 CWD 不是仓库根, 会拿不到)。
  std::string skeleton;
  namespace fs = std::filesystem;
  std::vector<fs::path> promptPaths;
  std::string sysPath = AssetLoader::getSystemConfigPath();
  if (!sysPath.empty()) promptPaths.emplace_back(sysPath + "/agent/system_prompt.md");
  const std::string runDir = getAvoxPath();
  if (!runDir.empty()) promptPaths.emplace_back(runDir + "/assets/agent/system_prompt.md");
  for (const auto& p : promptPaths) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
      std::ifstream f(p, std::ios::binary);
      if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        skeleton = ss.str();
        break;
      }
    }
  }
  // 回退: 骨架文件不存在时用最小骨架 (保证不崩)。
  if (skeleton.empty()) {
    skeleton = "你是 avplay 多媒体智能体。\n\n{{能力目录}}\n";
  }

  // 动态拼能力目录 (替换 {{能力目录}} 占位符)。
  // 命令层不再逐条暴露成工具, 也不在此枚举: avox_cli 各命令的用法与纪律由 assets/agent/skills
  // 的 avox-cli skill 承载, 入口摘要在骨架文件 system_prompt.md 里写死。
  std::string catalog;
  catalog += "【能力目录】(此处只记\"有什么、何时用\";细节加载对应 skill 或跑 -help 现查)\n\n";
  catalog +=
      "skill(目录只是摘要,不要凭摘要推断或执行其指令 —— 若用户点名某 skill, 或任务与其"
      "描述明确匹配, 先调 skill 工具, 传 name=<skill名> 加载该 skill 的完整指令再行动):\n";
  for (const auto& c : skills) {
    if (!c->invocation.modelInvocable) continue;          // dsh: 目录只列模型可调
    catalog += "- `" + c->name + "`: " + escapeSkillText(c->description) + "\n";
  }
  catalog += "\n辅助工具:\n";
  for (const HelperToolDoc& helper : kHelperTools) {
    const std::string name = helper.name;
    if (skillNames.count(name)) continue;   // 已在上面的 skill 栏
    catalog += "- " + name + ": " + helper.summary + "\n";
  }

  // 替换占位符。
  const std::string placeholder = "{{能力目录}}";
  auto pos = skeleton.find(placeholder);
  if (pos != std::string::npos) {
    skeleton.replace(pos, placeholder.size(), catalog);
  }
  return skeleton;
}

// ========== 内置 skill 装载 ==========
//
// DSH 二层发现 (binary-relative only, 对齐 @deepseek-ai/dsh-skill-filesystem):
//   rank 200 project-agents <getAvoxPath()>/assets/agent/skills  — avox bundled, 随 binary 走
//   rank 300 custom         AgentConfig.customSkillDirs[]      — 用户配置的任意目录
//
// 注: DSH 还有 rank 100 project-dsh (<getAvoxPath()>/.dsh/skills) 一层, 本期不启用 —
//   部署打补丁目前不开放, skill 全部由 bundled + customSkillDirs 覆盖。
// 不引入 user-level 层 (skill 跟 binary 同源, 不放用户目录)。
// 同名覆盖规则: 按 rank 升序加载, 先加载者胜出 — 调用方按 (200,300) 顺序传入, 高优先级先注册。
// ⚠ Android/iOS: getAvoxPath() 在 Android 返回 "" (Avox.cpp:373); iOS 的 app bundle 路径需平台
//   层在加载前把 skills/ 释放到可写目录, 然后经 customSkillDirs 注入。本期不处理移动端。

namespace {

// DSH 默认根 (rank 200)。getAvoxPath() 为空时整层跳过 (如 Android)。
struct DefaultRoot { int rank; std::string path; std::string source; };

std::vector<DefaultRoot> defaultSkillRoots() {
  std::vector<DefaultRoot> r;
  const std::string runDir = getAvoxPath();
  if (!runDir.empty()) {
    r.push_back({200, runDir + "/assets/agent/skills", "project-agents"});
  }
  return r;
}

}  // namespace

// 文件级状态: prepareBuiltinSkillRegistry() 在 builtinSkillRegistry() 首次访问前调用。
// 首次访问后 prepare 是 no-op (registry 已被 frozen)。命名空间外, 让外部的
// prepareBuiltinSkillRegistry/loadSkills 也能引用。
std::vector<std::string> g_pendingCustomDirs;
bool g_prepared = false;

void prepareBuiltinSkillRegistry(std::vector<std::string> customSkillDirs) {
  if (g_prepared) return;                                  // idempotent: 已冻结则丢弃
  g_pendingCustomDirs = std::move(customSkillDirs);
  g_prepared = true;
}

static void loadSkills(SkillRegistry& reg) {
  namespace fs = std::filesystem;
  std::vector<DefaultRoot> roots = defaultSkillRoots();
  // customSkillDirs 插在 rank 300, 即 project-agents (200) 之后。
  // DSH 排序: rank 升序 — bundled (200) 先于 custom (300) 加载, custom 内的相对顺序
  // 由 AgentConfig 数组顺序决定, 用户显式列在前面的同名 skill 胜出。
  for (const auto& dir : g_pendingCustomDirs) {
    roots.push_back({300, dir, "custom"});
  }
  // rank 升序遍历 (默认已升序; g_pendingCustomDirs 按 push_back 顺序追加)。
  std::sort(roots.begin(), roots.end(),
            [](const DefaultRoot& a, const DefaultRoot& b) { return a.rank < b.rank; });
  for (const auto& root : roots) {
    loadSkillsFromRoot(reg, root.path, root.source);
  }
}

SkillRegistry& builtinSkillRegistry() {
  static SkillRegistry reg;
  static bool inited = false;
  if (!inited) {
    // 首次访问即装载 DSH bundled + customSkillDirs (顺序已在 loadSkills 内处理)。
    // 必须在 prepareBuiltinSkillRegistry() 之后调用, 否则 g_pendingCustomDirs 为空。
    loadSkills(reg);
    inited = true;
  }
  return reg;
}

}
