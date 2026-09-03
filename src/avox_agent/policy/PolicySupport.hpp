#pragma once

// ============================================================================
// 策略插件的共享小工具。
//
// 「插件」在 avox 里就是注册到扩展点的函数对象: 每个策略暴露一个 install* 函数, 返回撤销器,
// 装配顺序写在 compose/ 的显式代码里。这比配置驱动的插件加载器更适合 C++, 也更容易调试。
//
// 一条与 dsh 共通的硬规则: **策略里不允许硬编码可调参数**。超时毫秒数、溢出阈值、重复
// 阈值、压缩比例都是 Config 字段, 从 agent.json 可改; 一个 DEFAULT_* 常量不算可配置。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// 一次工具调用所属的作用域 (无 agent 的直接调用走全局层)。
inline ScopeKey scopeOfExecution(const ToolExecution& exec) {
  return exec.agent == nullptr ? nullptr : exec.agent->scope();
}

// 把纯文本内容块拼成一个字符串; 含任何非文本块则返回 false。
//
// 溢出裁剪与循环提醒都只处理纯文本结果 —— 它们只知道最终格式化文本, 不知道工具内部结构,
// 对着一个带图片的结果动手只会破坏它。
inline bool flattenPlainText(const std::vector<ContentBlock>& content,
                            std::string& out) {
  out.clear();
  for (const ContentBlock& block : content) {
    const auto* text = std::get_if<TextBlock>(&block);
    if (text == nullptr) return false;
    out += text->text;
  }
  return true;
}

// 把多个注册合并成一个撤销器 (逆序撤销: 后注册的先撤)。
inline Disposer combineDisposers(std::vector<Disposer> disposers) {
  auto shared = std::make_shared<std::vector<Disposer>>(std::move(disposers));
  return [shared]() {
    for (size_t i = shared->size(); i > 0; --i) {
      Disposer& disposer = (*shared)[i - 1];
      if (disposer != nullptr) disposer();
    }
    shared->clear();
  };
}

// 构造一条插件来源的注入消息。
inline UserMessage makePluginMessage(const std::string& id,
                                    const std::string& pluginName,
                                    std::string text) {
  UserMessage message;
  message.id = MessageId(id);
  message.content.push_back(TextBlock{std::move(text)});
  // dsh 的 plugin source 用 plugin 字段携带插件名。
  message.source = pluginSource(pluginName);
  return message;
}

}
