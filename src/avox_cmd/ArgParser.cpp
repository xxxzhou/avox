#include "ArgParser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "avox/module/Json.hpp"

namespace avox {

// ParsedArgs

bool ParsedArgs::has(const std::string& name) const {
  return values.find(name) != values.end();
}

std::string ParsedArgs::getString(const std::string& name,
                                  const std::string& defaultVal) const {
  auto it = values.find(name);
  return (it != values.end()) ? it->second : defaultVal;
}

int ParsedArgs::getInt(const std::string& name, int defaultVal) const {
  auto it = values.find(name);
  if (it == values.end()) return defaultVal;
  return std::atoi(it->second.c_str());
}

float ParsedArgs::getFloat(const std::string& name, float defaultVal) const {
  auto it = values.find(name);
  if (it == values.end()) return defaultVal;
  return static_cast<float>(std::atof(it->second.c_str()));
}

bool ParsedArgs::getBool(const std::string& name, bool defaultVal) const {
  auto it = values.find(name);
  if (it == values.end()) return defaultVal;
  const auto& v = it->second;
  return v == "true" || v == "1" || v == "yes";
}

const std::vector<std::string>& ParsedArgs::positional() const {
  return positionalArgs;
}

const std::vector<std::string>& ParsedArgs::getStringList(
    const std::string& name) const {
  static const std::vector<std::string> kEmpty;
  auto it = multiValues.find(name);
  return (it != multiValues.end()) ? it->second : kEmpty;
}

// ArgParser

const Arg* ArgParser::findArg(const std::string& name) const {
  for (const auto& arg : args) {
    if (arg.shortName == name || arg.longName == name) return &arg;
  }
  return nullptr;
}

std::string ArgParser::keyName(const std::string& name) {
  // 去除前导的 -- 或 -
  if (name.size() >= 2 && name[0] == '-' && name[1] == '-')
    return name.substr(2);
  if (name.size() >= 1 && name[0] == '-') return name.substr(1);
  return name;
}

bool ArgParser::validateType(const std::string& value, ArgType type) {
  if (type == ArgType::Boolean) {
    return value == "true" || value == "false" || value == "1" ||
           value == "0" || value == "yes" || value == "no";
  }
  if (type == ArgType::Int) {
    if (value.empty()) return false;
    char* end = nullptr;
    std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0';
  }
  if (type == ArgType::Number) {
    if (value.empty()) return false;
    char* end = nullptr;
    std::strtod(value.c_str(), &end);
    return end != value.c_str() && *end == '\0';
  }
  // String: any value is ok
  return true;
}

void ArgParser::addArg(const Arg& arg) { args.push_back(arg); }

ParsedArgs ArgParser::parse(int argc, const char* const* argv) const {
  ParsedArgs result;
  // 初始化默认值
  for (const auto& arg : args) {
    std::string key =
        keyName(arg.longName.empty() ? arg.shortName : arg.longName);
    if (!arg.defaultVal.empty()) {
      result.values[key] = arg.defaultVal;
    }
  }
  // 解析命令行参数
  int i = 0;
  bool stopFlags = false;  // 遇到 -- 后停止解析 flag
  while (i < argc) {
    std::string token = argv[i];
    // -- 标记后续全部为位置参数
    if (token == "--") {
      stopFlags = true;
      ++i;
      continue;
    }
    // 非 flag 参数
    if (stopFlags || token.empty() || token[0] != '-') {
      result.positionalArgs.push_back(token);
      ++i;
      continue;
    }
    // -help / --help 特殊处理
    if (token == "-help" || token == "--help" || token == "-h") {
      result.values["help"] = "true";
      result.positionalArgs.push_back(token);
      ++i;
      continue;
    }
    // 查找匹配的 Arg 定义
    const Arg* def = findArg(token);
    // 如果精确匹配失败，尝试只用长名模式匹配 (用户可能用了 --
    // 前缀但只定义了短名，或反之) 这里严格匹配: token 必须是 shortName 或
    // longName
    if (!def) {
      fprintf(stderr, "Unknown option: %s\n", token.c_str());
      ++i;
      continue;
    }
    std::string key =
        keyName(def->longName.empty() ? def->shortName : def->longName);
    if (def->type == ArgType::Boolean) {
      result.values[key] = "true";
      ++i;
    } else {
      // String / Int / Number: 需要消费下一个 token 作为值
      if (i + 1 >= argc) {
        fprintf(stderr, "Option %s requires a value\n", token.c_str());
        result.values[key] = "";
        ++i;
      } else {
        std::string value = argv[i + 1];
        // 如果下一个 token 看起来像另一个选项，则缺少值
        if (!value.empty() && value[0] == '-' && value != "--") {
          // 尝试判断是不是数字 (负数)
          if (def->type == ArgType::Int || def->type == ArgType::Number) {
            // 允许以 - 开头的数字作为值
            char* end = nullptr;
            std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0') {
              fprintf(stderr, "Option %s requires a value\n", token.c_str());
              ++i;
              continue;
            }
          } else {
            fprintf(stderr, "Option %s requires a value\n", token.c_str());
            ++i;
            continue;
          }
        }
        result.values[key] = value;
        result.multiValues[key].push_back(value);
        i += 2;
      }
    }
  }
  // 检查必填参数
  for (const auto& arg : args) {
    if (!arg.required) continue;
    std::string key =
        keyName(arg.longName.empty() ? arg.shortName : arg.longName);
    if (!result.has(key)) {
      fprintf(
          stderr, "Required option %s is missing\n",
          arg.longName.empty() ? arg.shortName.c_str() : arg.longName.c_str());
    }
  }
  return result;
}

std::string ArgParser::helpText(const std::string& cmdName) const {
  std::string text;
  text += cmdName + "\n\n";
  text += "Usage: avox_cli " + cmdName + " [options]\n\n";
  if (!args.empty()) {
    text += "Options:\n";
    // 计算最大 flag 宽度用于对齐
    size_t maxFlagWidth = 0;
    for (const auto& arg : args) {
      std::string flagLine;
      if (!arg.shortName.empty()) flagLine += arg.shortName;
      if (!arg.longName.empty()) {
        if (!flagLine.empty()) flagLine += ", ";
        flagLine += arg.longName;
      }
      // 非 Boolean 类型显示值占位符
      if (arg.type != ArgType::Boolean) flagLine += " <value>";
      maxFlagWidth = std::max(maxFlagWidth, flagLine.size());
    }
    for (const auto& arg : args) {
      std::string line = "  ";
      std::string flagLine;
      if (!arg.shortName.empty()) flagLine += arg.shortName;
      if (!arg.longName.empty()) {
        if (!flagLine.empty()) flagLine += ", ";
        flagLine += arg.longName;
      }
      if (arg.type != ArgType::Boolean) flagLine += " <value>";
      line += flagLine;
      // 对齐描述
      size_t padding = (maxFlagWidth >= flagLine.size())
                           ? maxFlagWidth - flagLine.size() + 2
                           : 2;
      line += std::string(padding, ' ');
      line += arg.desc;
      if (arg.required) line += " (必填)";
      if (!arg.defaultVal.empty()) line += " [默认: " + arg.defaultVal + "]";
      text += line + "\n";
    }
  }
  return text;
}

ParsedArgs ArgParser::fromJson(const std::string& jsonStr) const {
  ParsedArgs result;
  // 初始化默认值
  for (const auto& arg : args) {
    std::string key =
        keyName(arg.longName.empty() ? arg.shortName : arg.longName);
    if (!arg.defaultVal.empty()) {
      result.values[key] = arg.defaultVal;
    }
  }
  // 解析 JSON
  try {
    Json root = parserJson(jsonStr.c_str());
    if (!root.bObject()) return result;
    for (const auto& arg : args) {
      std::string key =
          keyName(arg.longName.empty() ? arg.shortName : arg.longName);
      if (!root.find(key)) continue;
      auto& val = root[key];
      if (arg.type == ArgType::Boolean) {
        // JSON boolean 或字符串 "true"/"false"
        if (val.bBool()) {
          result.values[key] = val.get<bool>() ? "true" : "false";
        } else if (val.bString()) {
          result.values[key] = val.get<std::string>();
        } else {
          result.values[key] = "true";
        }
      } else if (arg.type == ArgType::Int) {
        if (val.bInt()) {
          result.values[key] = std::to_string(val.get<int>());
        } else if (val.bString()) {
          result.values[key] = val.get<std::string>();
        } else if (val.bNumber()) {
          result.values[key] = std::to_string((int)val.get<double>());
        }
      } else if (arg.type == ArgType::Number) {
        if (val.bNumber()) {
          result.values[key] = std::to_string(val.get<double>());
        } else if (val.bString()) {
          result.values[key] = val.get<std::string>();
        } else if (val.bInt()) {
          result.values[key] = std::to_string((double)val.get<int>());
        }
      } else {
        // String: 任何类型转字符串
        if (val.bString()) {
          result.values[key] = val.get<std::string>();
        } else {
          result.values[key] = val.dump();
        }
      }
    }
  } catch (...) {
    // JSON 解析失败: 返回只有默认值的 ParsedArgs
  }
  return result;
}

std::string ArgParser::usageSummary() const {
  if (args.empty()) return "";
  std::string s;
  for (const auto& arg : args) {
    s += "  ";
    // 旗标名: -short/--long
    if (!arg.shortName.empty()) s += arg.shortName;
    if (!arg.longName.empty()) {
      if (!arg.shortName.empty()) s += "/";
      s += arg.longName;
    }
    // 类型标注 (Boolean 不标, 其余标 <type>)
    if (arg.type != ArgType::Boolean) {
      const char* typeName = "string";
      if (arg.type == ArgType::Int) typeName = "int";
      else if (arg.type == ArgType::Number) typeName = "number";
      s += " <";
      s += typeName;
      s += ">";
    }
    s += " : ";
    s += arg.desc;
    if (!arg.defaultVal.empty()) {
      s += " [默认: ";
      s += arg.defaultVal;
      s += "]";
    }
    s += "\n";
  }
  // 去掉末尾换行
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}

std::string ArgParser::toJsonSchema() const {
  if (args.empty()) {
    return R"({"type":"object","properties":{}})";
  }
  std::string s = R"({"type":"object","properties":{)";
  std::string requiredList;
  bool first = true;
  int reqCount = 0;
  for (const auto& arg : args) {
    if (!first) s += ",";
    first = false;
    // key 名: 优先 longName(去 --), 否则 shortName(去 -)
    std::string key =
        keyName(arg.longName.empty() ? arg.shortName : arg.longName);
    s += "\"" + jsonEscape(key) + "\":{";
    // ArgType → JSON Schema type
    switch (arg.type) {
      case ArgType::Boolean:
        s += "\"type\":\"boolean\"";
        break;
      case ArgType::Int:
        s += "\"type\":\"integer\"";
        break;
      case ArgType::Number:
        s += "\"type\":\"number\"";
        break;
      default:  // String / Array / Object / Null → string
        s += "\"type\":\"string\"";
        break;
    }
    s += ",\"description\":\"" + jsonEscape(arg.desc) + "\"";
    if (!arg.defaultVal.empty()) {
      s += ",\"default\":\"" + jsonEscape(arg.defaultVal) + "\"";
    }
    s += "}";
    if (arg.required) {
      if (reqCount > 0) requiredList += ",";
      requiredList += "\"" + jsonEscape(key) + "\"";
      reqCount++;
    }
  }
  s += "}";
  if (reqCount > 0) {
    s += ",\"required\":[" + requiredList + "]";
  }
  s += "}";
  return s;
}

}
