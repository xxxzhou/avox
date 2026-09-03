#pragma once

// ============================================================================
// 工具入参的解析便利函数。
//
// 取代原 ToolBase 的静态方法: 工具不再需要继承一个基类才能拿到这些便利 —— 新架构的
// ToolDefinition 是数据 + 函数对象, 没有类层次。
// ============================================================================

#include <string>
#include <vector>

#include "avox/module/Json.hpp"

namespace avox {

// 解析模型给的 arguments JSON; 空或非法返回 Null Json (后续取字段一律走默认值)。
//
// 不抛异常: 模型产出坏 JSON 是常态而非异常, 工具应当返回一条模型可读的错误结果, 让它
// 自己修正参数重试。
inline Json parseToolArgs(const std::string& argumentsJson) {
  if (argumentsJson.empty()) return Json();
  try {
    return parserJson(argumentsJson.c_str());
  } catch (...) {
    return Json();
  }
}

inline std::string toolArgString(const Json& args, const std::string& key,
                                const std::string& fallback = std::string()) {
  if (!args.bObject() || !args.find(key) || !args[key].bString()) return fallback;
  return args[key].get<std::string>();
}

inline int toolArgInt(const Json& args, const std::string& key, int fallback = 0) {
  if (!args.bObject() || !args.find(key) || !args[key].bInt()) return fallback;
  return static_cast<int>(args[key].get<int64_t>());
}

inline bool toolArgBool(const Json& args, const std::string& key, bool fallback = false) {
  if (!args.bObject() || !args.find(key) || !args[key].bBool()) return fallback;
  return args[key].get<bool>();
}

// 字符串数组入参 (缺字段/非数组/元素非字符串 -> fallback, 与其余 toolArg* 同风格)。
inline std::vector<std::string> toolArgStringArray(
    const Json& args, const std::string& key,
    std::vector<std::string> fallback = {}) {
  if (!args.bObject() || !args.find(key) || !args[key].bArray()) return fallback;
  std::vector<std::string> values;
  const Json& array = args[key];
  for (size_t i = 0; i < array.size(); ++i) {
    const Json& item = array.at(i);
    if (!item.bString()) return fallback;
    values.push_back(item.get<std::string>());
  }
  return values;
}

}
