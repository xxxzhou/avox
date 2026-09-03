#include "AgentInstructions.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avox/module/Sha256.hpp"
#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/policy/PolicySupport.hpp"
#include "avox_agent/tools/ToolArgs.hpp"

namespace avox {

namespace {

// 与 dsh render.ts 逐字对齐的模型可见措辞 (插件名 / 系统提醒框 / 简介)。
const char* kPluginName = "agent-instructions";

const char* kWorkspaceContextIntro =
    "The following workspace instructions may be relevant to your work. "
    "Use them as guidance when applicable. More specific instructions take "
    "precedence over broader ones. They do not override system, developer, or "
    "direct user instructions.";

// ---- 路径与文本小工具 ----

// 统一成 '/' 分隔, 与 dsh 的展示路径一致 (Windows 下 path 默认给 '\\')。
std::string forwardSlashes(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

// 展示路径的目录部分: "src/foo/AGENTS.md" -> "src/foo"; 根目录文件 -> "."。
std::string dirnameOf(const std::string& displayPath) {
  const size_t slash = displayPath.find_last_of('/');
  if (slash == std::string::npos) return ".";
  return displayPath.substr(0, slash);
}

std::string basenameOf(const std::string& displayPath) {
  const size_t slash = displayPath.find_last_of('/');
  return slash == std::string::npos ? displayPath : displayPath.substr(slash + 1);
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += separator;
    out += parts[i];
  }
  return out;
}

// 去首尾空白后取 SHA-256 指纹, 用作同一目录内兄弟候选的去重身份
// (dsh 的 trimmedInstructionDigest; 算法换成 avox 已有的 sha256, 不去重跨仓)。
std::string trimmedInstructionDigest(const std::string& content) {
  const size_t begin = content.find_first_not_of(" \t\r\n");
  const size_t end = content.find_last_not_of(" \t\r\n");
  const std::string trimmed = begin == std::string::npos
      ? std::string()
      : content.substr(begin, end - begin + 1);
  return hexEncode(sha256(trimmed));
}

// 把正文里的 </system-reminder> 转义成 <\/system-reminder> (dsh render.ts 同款),
// 避免嵌套的系统提醒框提前闭合。
std::string escapeFrameBody(const std::string& body) {
  const char* kNeedle = "</system-reminder>";
  const char* kReplacement = "<\\/system-reminder>";
  const size_t needleLen = std::char_traits<char>::length(kNeedle);
  const size_t replacementLen = std::char_traits<char>::length(kReplacement);
  std::string out = body;
  size_t pos = 0;
  while ((pos = out.find(kNeedle, pos)) != std::string::npos) {
    out.replace(pos, needleLen, kReplacement);
    pos += replacementLen;
  }
  return out;
}

std::string frameBody(const std::string& body) {
  return std::string("<system-reminder>\n") + escapeFrameBody(body)
         + "\n</system-reminder>";
}

// 截断到 maxBytes 字节, 且不在 UTF-8 码点中间劈开。
std::string truncateUtf8(const std::string& value, int maxBytes) {
  if (value.empty() || maxBytes <= 0) return std::string();
  const size_t budget = static_cast<size_t>(maxBytes);
  size_t used = 0;
  size_t index = 0;
  while (index < value.size() && used < budget) {
    const unsigned char c = static_cast<unsigned char>(value[index]);
    size_t length = 1;
    if ((c & 0xE0) == 0xC0) {
      length = 2;
    } else if ((c & 0xF0) == 0xE0) {
      length = 3;
    } else if ((c & 0xF8) == 0xF0) {
      length = 4;
    }
    if (index + length > value.size()) break;
    if (used + length > budget) break;
    used += length;
    index += length;
  }
  return value.substr(0, index);
}

// 绝对化并规范化一个路径。
std::filesystem::path normalizedPath(const std::string& path) {
  std::filesystem::path value(path);
  std::error_code ec;
  if (!std::filesystem::path(value).is_absolute()) {
    value = std::filesystem::absolute(value, ec);
  }
  return value.lexically_normal();
}

// 向上走到第一个含任意标记目录的位置 (dsh findProjectRoot); 找不到返回 cwd 自身。
std::string findProjectRoot(const std::string& cwd,
                            const std::vector<std::string>& markers) {
  std::filesystem::path current = normalizedPath(cwd);
  for (;;) {
    for (const std::string& marker : markers) {
      std::error_code ec;
      if (std::filesystem::exists(current / marker, ec)) {
        return forwardSlashes(current.string());
      }
    }
    const std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
      return forwardSlashes(current.string());
    }
    current = parent;
  }
}

// 项目根 → cwd 的含根目录链, 从宽到窄 (dsh ancestorChain, 两端都含)。
std::vector<std::filesystem::path> ancestorChain(const std::filesystem::path& root,
                                                 const std::filesystem::path& cwd) {
  std::vector<std::filesystem::path> chain;
  std::filesystem::path current = normalizedPath(cwd.string());
  const std::filesystem::path resolvedRoot = normalizedPath(root.string());
  while (current != resolvedRoot) {
    chain.push_back(current);
    const std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) break;
    current = parent;
  }
  chain.push_back(resolvedRoot);
  std::reverse(chain.begin(), chain.end());
  return chain;
}

// 会话 cwd 与所触文件所在目录之间的子目录, 从浅到深, 不含 cwd 本身
// (dsh descendantDirsBetween)。文件在 cwd 之外返回空。
std::vector<std::filesystem::path> descendantDirsBetween(const std::string& root,
                                                         const std::string& touchedPath) {
  const std::filesystem::path resolvedRoot = normalizedPath(root);
  std::filesystem::path target = std::filesystem::path(touchedPath);
  if (target.is_relative()) target = resolvedRoot / target;
  target = target.lexically_normal();
  const std::filesystem::path targetDir = target.parent_path();
  if (targetDir.empty()) return {};
  std::vector<std::filesystem::path> chain = ancestorChain(root, targetDir.string());
  if (chain.size() < 2) return {};
  return std::vector<std::filesystem::path>(chain.begin() + 1, chain.end());
}

// ---- 指令文件加载 ----

struct LoadedInstruction {
  std::string absolutePath;
  std::string displayPath;
  std::string content;
};

// 探测并读取一个候选文件; 不存在/超尺寸/读失败一律跳过。
void loadCandidate(const std::filesystem::path& dir, const std::string& projectRoot,
                   const std::string& candidate, const AgentInstructionsConfig& config,
                   std::vector<LoadedInstruction>& out) {
  const std::filesystem::path path = dir / candidate;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return;
  const uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size > static_cast<uintmax_t>(config.maxSourceBytes)) return;
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();
  if (content.size() > static_cast<size_t>(config.maxSourceBytes)) return;
  std::string displayPath = forwardSlashes(
      std::filesystem::relative(path, normalizedPath(projectRoot), ec).string());
  if (ec || displayPath.empty()) displayPath = candidate;
  out.push_back(LoadedInstruction{path.string(), displayPath, std::move(content)});
}

// 一个目录下的全部候选: 先 base (AGENTS.md/CLAUDE.md) 后 local (AGENTS.local.md/...)。
void loadInDirectory(const std::filesystem::path& dir, const std::string& projectRoot,
                     const AgentInstructionsConfig& config,
                     std::vector<LoadedInstruction>& out) {
  for (const std::string& candidate : config.instructionFileCandidates) {
    loadCandidate(dir, projectRoot, candidate, config, out);
  }
  for (const std::string& candidate : config.localInstructionFileCandidates) {
    loadCandidate(dir, projectRoot, candidate, config, out);
  }
}

// 同一目录内按去空白指纹去重: 保留发现序最早的候选 (dsh 同款)。
void dedupByDirectory(std::vector<LoadedInstruction>& files) {
  std::map<std::string, std::set<std::string>> digestsByDir;
  std::vector<LoadedInstruction> kept;
  for (LoadedInstruction& file : files) {
    const std::string dir = dirnameOf(file.displayPath);
    const std::string digest = trimmedInstructionDigest(file.content);
    std::set<std::string>& digests = digestsByDir[dir];
    if (digests.count(digest) != 0) continue;
    digests.insert(digest);
    kept.push_back(std::move(file));
  }
  files = std::move(kept);
}

// 项目根 → cwd 全链各目录各探测一次 (基线发现)。
std::vector<LoadedInstruction> discoverBaselineFiles(
    const std::string& projectRoot, const std::string& cwd,
    const AgentInstructionsConfig& config) {
  const std::vector<std::filesystem::path> chain = ancestorChain(projectRoot, cwd);
  std::vector<LoadedInstruction> files;
  for (const std::filesystem::path& dir : chain) {
    loadInDirectory(dir, projectRoot, config, files);
  }
  dedupByDirectory(files);
  return files;
}

// ---- 渲染 ----

// 预算渲染: 全塞得下直接出; 塞不下从最宽的段开始丢; 只剩最具体的一段还塞不下就截断。
// intro 为空时纯段拼接 (动态投影用); 非空时作为首个段 (基线用)。
std::string renderBounded(const std::vector<std::string>& sections,
                          const std::string& intro, int maxBytes) {
  if (sections.empty() || maxBytes <= 0) return std::string();
  const auto build = [&](const std::vector<std::string>& included) {
    std::vector<std::string> parts;
    if (!intro.empty()) parts.push_back(intro);
    parts.insert(parts.end(), included.begin(), included.end());
    return join(parts, "\n\n");
  };

  std::vector<std::string> included(sections);
  std::string body = build(included);
  if (static_cast<int>(body.size()) <= maxBytes) return frameBody(body);

  for (size_t start = 1; start < sections.size(); ++start) {
    included.assign(sections.begin() + static_cast<ptrdiff_t>(start),
                    sections.end());
    body = build(included);
    if (static_cast<int>(body.size()) <= maxBytes) return frameBody(body);
  }

  // 只剩最具体的一段也超预算: 对段做二分截断, 取能塞下预算的最长前缀。
  const std::string last = sections.back();
  const std::string prefix = intro.empty() ? std::string() : intro + "\n\n";
  int low = 0;
  int high = static_cast<int>(last.size());
  std::string best;
  while (low <= high) {
    const int mid = low + (high - low) / 2;
    const std::string candidateBody = prefix + truncateUtf8(last, mid);
    if (static_cast<int>(candidateBody.size()) <= maxBytes) {
      best = frameBody(candidateBody);
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return best;
}

std::string renderBaseline(const std::vector<LoadedInstruction>& files, int maxBytes) {
  if (files.empty()) return std::string();
  std::vector<std::string> sections;
  sections.reserve(files.size());
  for (const LoadedInstruction& file : files) {
    sections.push_back(
        "Instructions from: " + file.displayPath + "\n\n" + file.content);
  }
  return renderBounded(sections, kWorkspaceContextIntro, maxBytes);
}

// 动态投影渲染。actions 与 files 一一对应: "set" 新增 / "replace" 变更。
std::string renderDynamic(const std::vector<LoadedInstruction>& files,
                          const std::vector<std::string>& actions, int maxBytes) {
  if (files.empty()) return std::string();
  std::vector<std::string> sections;
  sections.reserve(files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    const LoadedInstruction& file = files[i];
    if (actions[i] == "replace") {
      sections.push_back(
          "Updated instructions from: " + file.displayPath + "\n\n"
          + "This file changed after it was loaded. Use the following content "
            "instead of the previously loaded instructions from this file.\n\n"
          + file.content);
    } else {
      const std::string scope = dirnameOf(file.displayPath);
      sections.push_back(
          "Additional instructions from: " + file.displayPath + "\n\n"
          + "These instructions apply to work under `" + scope
          + "`. Use them as guidance when relevant; more specific instructions "
            "take precedence. They do not override system, developer, or direct "
            "user instructions.\n\n"
          + file.content);
    }
  }
  return renderBounded(sections, std::string(), maxBytes);
}

// ---- 作用域与会话状态 ----

// 逻辑作用域: 展示目录 + '\0' + 候选文件名 (dsh 的 candidateScopeKey)。
std::string scopeKeyFor(const std::string& displayPath) {
  return dirnameOf(displayPath) + std::string("\0", 1) + basenameOf(displayPath);
}

struct InstructionScopeState {
  std::string path;   // 展示路径
  std::string digest; // 去空白内容指纹
};

// 每个会话的注入状态: 基线是否注入过 + 已注入作用域 → 指纹 + 投影序号。
struct SessionInstructionState {
  bool baselineInjected = false;
  int projectionSeq = 0;
  std::map<std::string, InstructionScopeState> scopes;
};

// 会话级状态表。多 agent (主 + 子) 并发驱动, 用互斥锁保护。
class InstructionStateTable {
 public:
  // 首次成功标记基线; 已标记返回 false (跳过重扫)。
  bool tryMarkBaselineInjected(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mtx);
    SessionInstructionState& state = states[sessionId];
    if (state.baselineInjected) return false;
    state.baselineInjected = true;
    return true;
  }

  // 记录已注入的作用域 (基线与动态投影都走这里)。
  void commitScopes(const std::string& sessionId,
                    const std::vector<LoadedInstruction>& files) {
    std::lock_guard<std::mutex> lock(mtx);
    SessionInstructionState& state = states[sessionId];
    for (const LoadedInstruction& file : files) {
      state.scopes[scopeKeyFor(file.displayPath)] =
          InstructionScopeState{file.displayPath,
                                trimmedInstructionDigest(file.content)};
    }
  }

  // 相对已注入作用域计算新增/变更; 无变化的文件不返回, 也不写缓存。
  std::vector<std::pair<LoadedInstruction, std::string>> diffProjection(
      const std::string& sessionId, std::vector<LoadedInstruction> files) {
    std::lock_guard<std::mutex> lock(mtx);
    SessionInstructionState& state = states[sessionId];
    std::vector<std::pair<LoadedInstruction, std::string>> changes;
    for (LoadedInstruction& file : files) {
      const std::string scope = scopeKeyFor(file.displayPath);
      const std::string digest = trimmedInstructionDigest(file.content);
      const auto it = state.scopes.find(scope);
      if (it == state.scopes.end()) {
        changes.emplace_back(std::move(file), "set");
      } else if (it->second.digest != digest) {
        changes.emplace_back(std::move(file), "replace");
      }
    }
    return changes;
  }

  // 分配一次投影的序号 (消息 id 用, 同会话内唯一)。
  int nextProjectionSeq(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mtx);
    return ++states[sessionId].projectionSeq;
  }

  // 会话销毁时清理 (subagent 结束)。
  void erase(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mtx);
    states.erase(sessionId);
  }

 private:
  std::mutex mtx;
  std::map<std::string, SessionInstructionState> states;
};

// 模型是否触发了文件 I/O (dsh FILE_TOUCH_TOOL_NAMES)。
bool isFileTouchTool(const std::string& name) {
  return name == "read" || name == "write" || name == "edit";
}

// 从工具入参里取 file_path; 非文件工具或参数缺失返回空。
std::string filePathFromExecution(const ToolExecution& exec) {
  if (!isFileTouchTool(exec.name)) return std::string();
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string path = toolArgString(args, "file_path");
  const size_t begin = path.find_first_not_of(" \t");
  if (begin == std::string::npos) return std::string();
  const size_t end = path.find_last_not_of(" \t");
  return path.substr(begin, end - begin + 1);
}

}  // namespace

Disposer installAgentInstructions(ToolRuntime& tools, AgentExtensionPoints& points,
                                  const AgentInstructionsConfig& config) {
  auto state = std::make_shared<InstructionStateTable>();
  std::vector<Disposer> disposers;

  // ---- 基线注入 (agent/pre-step) ----
  // 会话第一个「真有输入」的 step 执行一次: 合成项目根 → cwd 链上的指令文件,
  // 塞到批次最前, 随后随该 step 一起落日志进模型历史。
  using PreStepNext = Chain<PreStepPayload, PreStepDecision>::Next;
  disposers.push_back(points.preStep.on(
      [config, state](PreStepPayload& payload,
                      const PreStepNext& next) -> PreStepDecision {
        PreStepDecision decision = next();
        if (std::holds_alternative<PreStepReject>(decision)) return decision;
        if (payload.agent == nullptr) return decision;
        std::vector<UserMessage>& messages = std::get<PreStepEnter>(decision).messages;
        if (messages.empty()) return decision;

        const std::string sessionId = payload.agent->id().value;
        if (!state->tryMarkBaselineInjected(sessionId)) return decision;

        std::string rendered;
        try {
          // 会话 cwd 只认显式配置 (session.header.cwd), 缺省即无 cwd -> 不注入工作区
          // 指令, 也不回退到进程 cwd (与 dsh 的 agent.session.header.cwd 对齐)。
          const std::optional<std::string> cwd =
              payload.agent->session().getHeader().cwd;
          if (!cwd.has_value()) return decision;
          const std::string projectRoot =
              findProjectRoot(*cwd, config.projectRootMarkers);
          std::vector<LoadedInstruction> files =
              discoverBaselineFiles(projectRoot, *cwd, config);
          rendered = renderBaseline(files, config.maxBytes);
          if (!rendered.empty()) state->commitScopes(sessionId, files);
        } catch (const std::exception&) {
          rendered.clear();
        }
        if (rendered.empty()) return decision;

        UserMessage message = makePluginMessage(
            sessionId + "/agent-instructions/baseline", kPluginName,
            std::move(rendered));
        messages.insert(messages.begin(), std::move(message));
        return decision;
      }));

  // ---- 动态投影 (tools/post-execute) ----
  // 成功的 read/write/edit 触碰到会话 cwd 内的新子目录时, 把该目录链上新出现的
  // 指令文件合成一条上下文, 经 additionalContexts 排进 next-step, 下一 step 进模型。
  using PostToolNext = Chain<PostToolPayload, PostToolDecision>::Next;
  disposers.push_back(tools.postExecute.on(
      [config, state](PostToolPayload& payload,
                      const PostToolNext& next) -> PostToolDecision {
        PostToolDecision decision = next();
        if (std::holds_alternative<PostToolBlock>(decision)) return decision;
        if (payload.exec == nullptr || payload.result == nullptr) return decision;
        if (payload.exec->agent == nullptr || payload.result->isError()) {
          return decision;
        }

        const std::string touched = filePathFromExecution(*payload.exec);
        if (touched.empty()) return decision;

        try {
          // 与基线一致: 只认显式会话 cwd, 无 cwd 时不做动态投影。
          const std::optional<std::string> cwd =
              payload.exec->agent->session().getHeader().cwd;
          if (!cwd.has_value()) return decision;
          const std::vector<std::filesystem::path> dirs =
              descendantDirsBetween(*cwd, touched);
          if (dirs.empty()) return decision;

          const std::string projectRoot =
              findProjectRoot(*cwd, config.projectRootMarkers);
          std::vector<LoadedInstruction> files;
          for (const std::filesystem::path& dir : dirs) {
            loadInDirectory(dir, projectRoot, config, files);
          }
          dedupByDirectory(files);
          if (files.empty()) return decision;

          const std::string sessionId = payload.exec->agent->id().value;
          std::vector<std::pair<LoadedInstruction, std::string>> changes =
              state->diffProjection(sessionId, std::move(files));
          if (changes.empty()) return decision;

          std::vector<LoadedInstruction> changeFiles;
          std::vector<std::string> actions;
          changeFiles.reserve(changes.size());
          actions.reserve(changes.size());
          for (auto& [file, action] : changes) {
            changeFiles.push_back(std::move(file));
            actions.push_back(std::move(action));
          }
          const std::string rendered =
              renderDynamic(changeFiles, actions, config.maxBytes);
          if (rendered.empty()) return decision;
          state->commitScopes(sessionId, changeFiles);

          PostToolAccept& accept = std::get<PostToolAccept>(decision);
          accept.additionalContexts.push_back(makePluginMessage(
              sessionId + "/agent-instructions/projection-"
                  + std::to_string(state->nextProjectionSeq(sessionId)),
              kPluginName, rendered));
        } catch (const std::exception&) {
          // 发现/渲染失败不影响本次工具结果。
        }
        return decision;
      }));

  return combineDisposers(std::move(disposers));
}

}
