#pragma once

// ============================================================================
// 系统提示词装配。
//
// 对齐 dsh 的 packages/core/system-prompt。
//
// 两条注册通道的区别不是「静态 vs 动态」, 而是**缓存代价**:
//
//   section  进 system prompt 前缀。改它, 从变动的那个 token 起整个 KV cache 复用失效,
//            而 system + tool schema 是每个 step 都要重付的部分。
//   context  进**历史尾部**的一条 user 消息。前缀一个字节都不动, 缓存全保留。
//
// 所以判据是变化频率: 一年不改的领域知识 → section; 每轮都可能不同的运行时状态 → context,
// 并且只在与上次注入的文本不同时才产出 (见 RuntimeContextProjection)。
//
// 现成对照: skill 的能力目录是 section (很少变), skill 正文是 context/注入 (用户显式
// 调用时才给那一个)。
// ============================================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Abort.hpp"
#include "Dispatch.hpp"
#include "Scope.hpp"
#include "Session.hpp"
#include "SessionTypes.hpp"

namespace avox {

class Agent;

// order 约定 (与 dsh 一致):
//   -100      harness 身份
//      0      部署人格 —— 具名常量, 为的是让 preset 用同名 section 遮蔽它
//   100-199   工具与能力指引
//   200+      策略声明
inline constexpr int PROMPT_ORDER_IDENTITY = -100;
inline constexpr int PROMPT_ORDER_PERSONA = 0;
inline constexpr const char* PROMPT_SECTION_PERSONA = "persona";
inline constexpr int PROMPT_ORDER_TOOL_GUIDANCE = 100;

// 装配时传给求值函数的上下文。
struct AssembleContext {
  // 为哪个 agent 装配; 裸装配 (诊断、单测) 时为空, 求值函数必须容忍。
  Agent* agent = nullptr;
  ScopeKey scope = nullptr;
  std::shared_ptr<AbortSignal> signal;
};

// 一段进入 system prompt 前缀的提示词。
struct PromptSection {
  // 同一层内唯一; 作用域层的同名 section 遮蔽全局层的。
  std::string name;
  // 升序拼接。必须是有限值。
  int order = 0;
  std::function<std::string(const AssembleContext&)> text;
  // 声明「我就是整个 system prompt」。装配照样跑完 (让工具与上下文得到解析), 然后把
  // 本段恢复成唯一一段。出现两个 complete 则装配失败 —— 那是配置矛盾, 不是可合并项。
  bool complete = false;
};

// 一段进入历史尾部的动态运行时上下文。
struct PromptContext {
  std::string name;
  int order = 0;
  std::function<std::string(const AssembleContext&)> text;
};

// 装配结果。
struct PromptAssembly {
  // 渲染后的完整 system 文本 (变量已插值)。
  std::string system;
  // 装配后的工具 schema 数组 JSON; 无工具则为空串。
  std::string toolsJson;
  // 动态上下文各段的文本, 已按 order 排好序、已剔除空段。
  std::vector<std::string> contextSections;
};

// 一个作用域的完整提示词贡献。
class PromptLayer {
 public:
  explicit PromptLayer(ScopeKey scope);

  NamedEntries<PromptSection> sections;
  NamedEntries<PromptContext> contexts;
  NamedEntries<std::string> variables;

  bool isEmpty() const {
    return sections.isEmpty() && contexts.isEmpty() && variables.isEmpty();
  }
};

class SystemPrompt {
 public:
  SystemPrompt();

  SystemPrompt(const SystemPrompt&) = delete;
  SystemPrompt& operator=(const SystemPrompt&) = delete;

  // 注册一段前缀提示词。order 非有限值或同层重名抛 std::runtime_error。
  Disposer section(PromptSection section, ScopeKey owner = nullptr);

  // 注册一段动态运行时上下文。
  Disposer context(PromptContext context, ScopeKey owner = nullptr);

  // 注册一个 {{name}} 插值变量。
  Disposer variable(std::string name, std::string value, ScopeKey owner = nullptr);

  // 设置工具 schema 的来源 (由 compose 接到 ToolRuntime)。
  //
  // 与 dsh 的偏离: dsh 允许注册多个 tools provider 并累加, 因为它要让 tools 的来源可扩展。
  // avox 这里只接一个 —— ToolRuntime 的分层注册表本身就是那个可扩展点 (MCP 桥、skill 派生
  // 工具都往它注册), 再套一层累加只会有两处需要维护排序与去重。
  void setToolsProvider(std::function<std::string(const AssembleContext&)> provider) {
    toolsProvider = std::move(provider);
  }

  // 装配一次。
  //
  // 变量插值是严格的: 引用了未注册的变量则抛 —— 静默留下一个 {{foo}} 会直接进模型请求,
  // 那比装配失败难查得多。
  PromptAssembly assemble(const AssembleContext& context) const;

  // 提示词组成发生变化 (注册或撤销)。
  Notify<int> change{"system-prompt/change"};

 private:
  ScopedLayers<PromptLayer> layers;
  std::function<std::string(const AssembleContext&)> toolsProvider;
};

// 动态运行时上下文的历史投影。
//
// 把易变信息 (工作目录、播放器状态、打开的日志) 放到历史尾部而不是 system 里, 是保住 KV
// cache 前缀的一半; 另一半是**只在与上次注入的文本不同时才产出**, 相同就不发, 于是历史
// 保持 append-only 而不是每轮堆一条重复消息。
class RuntimeContextProjection {
 public:
  // session: 借用引用。构造时从日志里反向找最后一条本投影产出的消息, 于是 resume 出来的
  // 会话不会因为「内存里没有上次快照」而重复注入一条它已经说过的话。
  explicit RuntimeContextProjection(Session& session);

  // 与上次不同则产出一条待注入的消息, 否则返回 nullopt。
  //
  // 调用方负责把它 append 进日志 (通常经 pre-step 的消息批次)。
  std::optional<UserMessage> project(const std::string& text);

  // 本投影产出的消息在 MessageSource::name 上的标记。
  static const char* sourceName();

 private:
  std::string lastText;
  // 用于生成稳定且唯一的消息 id。
  size_t counter = 0;
  const SessionId sessionId;
};

}
