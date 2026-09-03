#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "avox/AvoxDef.h"
#include "avox/AvoxBase.h"

namespace avox {

struct Arg {
  std::string shortName;  // "-i"
  std::string longName;   // "--input"
  ArgType type;
  bool required;
  std::string desc;
  std::string defaultVal;
};

class ParsedArgs {
 public:
  bool has(const std::string& name) const;
  std::string getString(const std::string& name,
                        const std::string& defaultVal = "") const;
  int getInt(const std::string& name, int defaultVal = 0) const;
  float getFloat(const std::string& name, float defaultVal = 0.0f) const;
  bool getBool(const std::string& name, bool defaultVal = false) const;
  // 取同名参数的所有值 (支持 -t a -t b 重复出现; 无传入返回空)
  const std::vector<std::string>& getStringList(const std::string& name) const;
  const std::vector<std::string>& positional() const;

 private:
  friend class ArgParser;
  std::unordered_map<std::string, std::string> values;
  // 同名参数的多次出现值 (仅收集用户实际传入; 默认值不计入, 见 ArgParser::parse)
  std::unordered_map<std::string, std::vector<std::string>> multiValues;
  std::vector<std::string> positionalArgs;
};

class ArgParser {
 public:
  ArgParser() = default;
  ~ArgParser() = default;

 private:
  std::vector<Arg> args;
  // 根据短名或长名查找 Arg，返回 nullptr 表示未找到
  const Arg* findArg(const std::string& name) const;
  // 从 name 中提取键名 (去除前导 -/--)
  static std::string keyName(const std::string& name);
  // 检查一个值是否符合 Arg 的类型
  static bool validateType(const std::string& value, ArgType type);

 public:
  void addArg(const Arg& arg);
  ParsedArgs parse(int argc, const char* const* argv) const;
  std::string helpText(const std::string& cmdName) const;

  // 从 JSON 对象解析参数 (agent execute 路径: JSON → ParsedArgs)
  // key 用 keyName 规则(无前导 -), Boolean 型传 true/false/"true"/"false" 均可
  ParsedArgs fromJson(const std::string& jsonStr) const;

  // 导出 OpenAI function-calling 的 JSON Schema (parameters 字段)
  // ArgType 映射: Boolean→boolean, Int→integer, Number→number, String→string
  std::string toJsonSchema() const;

  // 生成精简用法摘要 (供 AI 工具描述自动拼接, 让 AI 知道参数含义和组合方式)
  // 格式: 每行 "  -short/--long <type> : desc [默认: val]"
  // Boolean 型不显示 <type>, 只显示旗标名
  std::string usageSummary() const;
};

}
