#include "SystemPrompt.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace avox {

namespace {

// 把 {{name}} 替换成注册的变量值。
//
// 严格: 引用了未注册的变量就抛。静默留下一个 {{foo}} 会原样进模型请求, 那比装配失败
// 难查得多 —— 模型会当成字面量, 而排查者要从输出往回倒推很久。
std::string interpolate(const std::string& text,
                        const std::vector<std::pair<std::string, std::string>>& variables) {
  std::string output;
  output.reserve(text.size());
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t open = text.find("{{", pos);
    if (open == std::string::npos) {
      output.append(text, pos, std::string::npos);
      break;
    }
    const size_t close = text.find("}}", open + 2);
    if (close == std::string::npos) {
      // 没有闭合就不是占位符, 原样保留 —— 提示词里出现落单的 {{ 不该让装配失败。
      output.append(text, pos, std::string::npos);
      break;
    }
    output.append(text, pos, open - pos);
    const std::string name = text.substr(open + 2, close - open - 2);
    const auto found = std::find_if(
        variables.begin(), variables.end(),
        [&](const std::pair<std::string, std::string>& entry) {
          return entry.first == name;
        });
    if (found == variables.end()) {
      throw std::runtime_error("提示词引用了未注册的变量 {{" + name + "}}");
    }
    output.append(found->second);
    pos = close + 2;
  }
  return output;
}

// 去掉首尾空白, 用于判断一段是否为空。
bool blank(const std::string& text) {
  for (char c : text) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

}  // namespace

PromptLayer::PromptLayer(ScopeKey scope)
    : sections([scope](const std::string& name) {
        return scope == nullptr ? "提示词段 \"" + name + "\" 已注册"
                                : "提示词段 \"" + name + "\" 已在本作用域注册";
      }),
      contexts([scope](const std::string& name) {
        return scope == nullptr ? "运行时上下文 \"" + name + "\" 已注册"
                                : "运行时上下文 \"" + name + "\" 已在本作用域注册";
      }),
      variables([scope](const std::string& name) {
        return scope == nullptr ? "提示词变量 \"" + name + "\" 已注册"
                                : "提示词变量 \"" + name + "\" 已在本作用域注册";
      }) {}

SystemPrompt::SystemPrompt() : layers([this]() { change.emit(0); }) {}

Disposer SystemPrompt::section(PromptSection promptSection, ScopeKey owner) {
  if (promptSection.name.empty()) throw std::runtime_error("提示词段必须有名字");
  if (!std::isfinite(static_cast<double>(promptSection.order))) {
    throw std::runtime_error("提示词段 \"" + promptSection.name + "\" 的 order 必须有限");
  }
  if (promptSection.text == nullptr) {
    throw std::runtime_error("提示词段 \"" + promptSection.name + "\" 缺少 text");
  }
  const std::string name = promptSection.name;
  return layers.effect(owner, [&](PromptLayer& layer) {
    return layer.sections.insert(name, std::move(promptSection));
  });
}

Disposer SystemPrompt::context(PromptContext promptContext, ScopeKey owner) {
  if (promptContext.name.empty()) throw std::runtime_error("运行时上下文必须有名字");
  if (promptContext.text == nullptr) {
    throw std::runtime_error("运行时上下文 \"" + promptContext.name + "\" 缺少 text");
  }
  const std::string name = promptContext.name;
  return layers.effect(owner, [&](PromptLayer& layer) {
    return layer.contexts.insert(name, std::move(promptContext));
  });
}

Disposer SystemPrompt::variable(std::string name, std::string value,
                                ScopeKey owner) {
  if (name.empty()) throw std::runtime_error("提示词变量必须有名字");
  return layers.effect(owner, [&](PromptLayer& layer) {
    return layer.variables.insert(name, std::move(value));
  });
}

PromptAssembly SystemPrompt::assemble(const AssembleContext& assembleContext) const {
  const ScopeKey scope = assembleContext.scope;

  // 具名条目按同名遮蔽合并, 最近的作用域胜。
  std::vector<std::pair<std::string, PromptSection>> sections =
      layers.merge<PromptSection>(
          scope, [](const PromptLayer& layer) -> const NamedEntries<PromptSection>& {
            return layer.sections;
          });
  std::vector<std::pair<std::string, PromptContext>> contexts =
      layers.merge<PromptContext>(
          scope, [](const PromptLayer& layer) -> const NamedEntries<PromptContext>& {
            return layer.contexts;
          });
  const std::vector<std::pair<std::string, std::string>> variables =
      layers.merge<std::string>(
          scope, [](const PromptLayer& layer) -> const NamedEntries<std::string>& {
            return layer.variables;
          });

  // order 升序; 同 order 保持合并后的插入序, 于是装配顺序是确定的。
  std::stable_sort(sections.begin(), sections.end(),
                   [](const auto& a, const auto& b) {
                     return a.second.order < b.second.order;
                   });
  std::stable_sort(contexts.begin(), contexts.end(),
                   [](const auto& a, const auto& b) {
                     return a.second.order < b.second.order;
                   });

  PromptAssembly assembly;

  // 先求值全部段落: complete 段也要等其余段求值完再顶替, 因为求值本身可能有必须发生的
  // 副作用 (例如触发一次 skill 目录刷新)。
  std::vector<std::string> rendered;
  rendered.reserve(sections.size());
  const PromptSection* completeSection = nullptr;
  std::string completeText;
  for (const auto& entry : sections) {
    std::string text = entry.second.text(assembleContext);
    if (entry.second.complete) {
      if (completeSection != nullptr) {
        throw std::runtime_error("提示词段 \"" + entry.first + "\" 与 \""
                                 + completeSection->name
                                 + "\" 都声明了 complete: 这是配置矛盾");
      }
      completeSection = &entry.second;
      completeText = std::move(text);
      continue;
    }
    if (!blank(text)) rendered.push_back(std::move(text));
  }

  if (completeSection != nullptr) {
    assembly.system = interpolate(completeText, variables);
  } else {
    std::string joined;
    for (size_t i = 0; i < rendered.size(); ++i) {
      if (i > 0) joined += "\n\n";
      joined += rendered[i];
    }
    assembly.system = interpolate(joined, variables);
  }

  if (toolsProvider != nullptr) {
    assembly.toolsJson = toolsProvider(assembleContext);
    // 空数组当作「无工具」: 让 header 比较不必区分 "[]" 与不存在两种表示。
    if (assembly.toolsJson == "[]") assembly.toolsJson.clear();
  }

  for (const auto& entry : contexts) {
    std::string text = entry.second.text(assembleContext);
    if (blank(text)) continue;
    assembly.contextSections.push_back(interpolate(text, variables));
  }
  return assembly;
}

// ===========================================================================
// RuntimeContextProjection
// ===========================================================================

const char* RuntimeContextProjection::sourceName() { return "runtime-context"; }

RuntimeContextProjection::RuntimeContextProjection(Session& session)
    : sessionId(session.id()) {
  // 反向找最后一条本投影产出的消息, 把它的文本当作「上次说过的话」。
  //
  // 没有这一步, resume 出来的会话会把上一进程刚说过的运行时状态再说一遍 —— 那不仅浪费
  // 一条消息, 还会让模型以为状态刚刚发生了变化。
  const std::vector<SessionEvent>& events = session.events();
  for (size_t i = events.size(); i > 0; --i) {
    const SessionEvent& event = events[i - 1];
    if (event.type != EventType::UserMessageEvent) continue;
    const UserMessage& message = std::get<UserMessageData>(event.data).message;
    if (message.source.kind != MessageSourceKind::Plugin) continue;
    // dsh 的 plugin source 把插件名放在 plugin 字段 (name 属于 skill-invocation)。
    if (message.source.plugin.value_or("") != sourceName()) continue;
    for (const ContentBlock& block : message.content) {
      if (const auto* text = std::get_if<TextBlock>(&block)) {
        lastText = text->text;
        break;
      }
    }
    break;
  }
}

std::optional<UserMessage> RuntimeContextProjection::project(const std::string& text) {
  if (text == lastText) return std::nullopt;
  lastText = text;

  UserMessage message;
  message.id = MessageId(sessionId.value + "/runtime-context/"
                         + std::to_string(++counter));
  message.content.push_back(TextBlock{text});
  message.source = pluginSource(sourceName());
  return message;
}

}
