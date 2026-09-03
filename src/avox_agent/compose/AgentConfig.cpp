#include "AgentConfig.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "avox/module/Json.hpp"

namespace avox {

namespace {

bool has(const Json& j, const char* key) { return j.bObject() && j.find(key); }

std::string readString(const Json& j, const char* key, const std::string& fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bString()) throw std::runtime_error(std::string("配置 ") + key + " 必须是字符串");
  return v.get<std::string>();
}

int readInt(const Json& j, const char* key, int fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bInt()) throw std::runtime_error(std::string("配置 ") + key + " 必须是整数");
  return static_cast<int>(v.get<int64_t>());
}

int64_t readInt64(const Json& j, const char* key, int64_t fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bInt()) throw std::runtime_error(std::string("配置 ") + key + " 必须是整数");
  return v.get<int64_t>();
}

bool readBool(const Json& j, const char* key, bool fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bBool()) throw std::runtime_error(std::string("配置 ") + key + " 必须是布尔");
  return v.get<bool>();
}

double readDouble(const Json& j, const char* key, double fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (v.bInt()) return static_cast<double>(v.get<int64_t>());
  if (!v.bNumber()) throw std::runtime_error(std::string("配置 ") + key + " 必须是数字");
  return v.get<double>();
}

std::vector<std::string> readStringArray(const Json& j, const char* key,
                                        const std::vector<std::string>& fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bArray()) throw std::runtime_error(std::string("配置 ") + key + " 必须是数组");
  std::vector<std::string> values;
  values.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    const Json& item = v.at(i);
    if (!item.bString()) {
      throw std::runtime_error(std::string("配置 ") + key + " 的元素必须是字符串");
    }
    values.push_back(item.get<std::string>());
  }
  return values;
}

std::vector<int> readIntArray(const Json& j, const char* key,
                             const std::vector<int>& fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bArray()) throw std::runtime_error(std::string("配置 ") + key + " 必须是数组");
  std::vector<int> values;
  values.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    const Json& item = v.at(i);
    if (!item.bInt()) {
      throw std::runtime_error(std::string("配置 ") + key + " 的元素必须是整数");
    }
    values.push_back(static_cast<int>(item.get<int64_t>()));
  }
  return values;
}

}  // namespace

AgentConfig AgentConfig::fromJson(const std::string& json) {
  const Json root = parserJson(json.c_str());
  if (!root.bObject()) throw std::runtime_error("agent 配置必须是 JSON 对象");

  AgentConfig config;
  config.provider = readString(root, "provider", config.provider);
  config.model = readString(root, "model", config.model);
  if (has(root, "maxTokens")) {
    config.maxTokens = readInt(root, "maxTokens", 0);
  }
  config.sessionRoot = readString(root, "sessionRoot", config.sessionRoot);
  if (has(root, "sessionCwd")) {
    config.sessionCwd = readString(root, "sessionCwd", std::string());
    // 空串等于没指定 (缺省即「无 cwd」); 给了却被当成项目目录会误导布局与 resume。
    if (config.sessionCwd->empty()) config.sessionCwd = std::nullopt;
  }
  config.persona = readString(root, "persona", config.persona);
  config.maxParallelToolCalls =
      readInt(root, "maxParallelToolCalls", config.maxParallelToolCalls);
  if (config.maxParallelToolCalls < 1) {
    throw std::runtime_error("maxParallelToolCalls 必须是正整数");
  }
  // skill 发现 (DSH customSkillDirs 同款): 额外 skill 目录数组, rank 300。
  // 装配层在 builtinSkillRegistry() 首次访问前调 prepareBuiltinSkillRegistry() 注入。
  config.customSkillDirs = readStringArray(root, "customSkillDirs", config.customSkillDirs);

  if (has(root, "timeout")) {
    const Json& node = root["timeout"];
    config.enableTimeout = readBool(node, "enabled", config.enableTimeout);
    config.timeout.defaultTimeoutMs =
        readInt(node, "defaultTimeoutMs", config.timeout.defaultTimeoutMs);
  }

  if (has(root, "repeatGuard")) {
    const Json& node = root["repeatGuard"];
    config.enableRepeatGuard = readBool(node, "enabled", config.enableRepeatGuard);
    config.repeatGuard.thresholds =
        readIntArray(node, "thresholds", config.repeatGuard.thresholds);
    config.repeatGuard.argumentsPreviewChars = readInt(
        node, "argumentsPreviewChars", config.repeatGuard.argumentsPreviewChars);
  }

  if (has(root, "spill")) {
    const Json& node = root["spill"];
    config.enableSpill = readBool(node, "enabled", config.enableSpill);
    config.spill.maxInlineBytes =
        readInt(node, "maxInlineBytes", config.spill.maxInlineBytes);
    config.spill.spillRoot = readString(node, "spillRoot", config.spill.spillRoot);
    config.spill.skipTools =
        readStringArray(node, "skipTools", config.spill.skipTools);
  }

  if (has(root, "approval")) {
    const Json& node = root["approval"];
    config.enableApproval = readBool(node, "enabled", config.enableApproval);
    const std::string policy = readString(node, "policy", "ask");
    if (policy == "ask") {
      config.approval.defaultPolicy = ApprovalPolicy::Ask;
    } else if (policy == "never") {
      config.approval.defaultPolicy = ApprovalPolicy::Never;
    } else {
      throw std::runtime_error("approval.policy 只能是 \"ask\" 或 \"never\"");
    }
    config.approval.requireApproval = readStringArray(
        node, "requireApproval", config.approval.requireApproval);
  }

  if (has(root, "compaction")) {
    const Json& node = root["compaction"];
    config.enableCompaction = readBool(node, "enabled", config.enableCompaction);
    config.compaction.thresholdRatio =
        readDouble(node, "thresholdRatio", config.compaction.thresholdRatio);
    config.compaction.fallbackContextWindow = readInt64(
        node, "fallbackContextWindow", config.compaction.fallbackContextWindow);
    config.compaction.keepTailNodes =
        readInt(node, "keepTailNodes", config.compaction.keepTailNodes);
    config.compaction.summaryPrompt =
        readString(node, "summaryPrompt", config.compaction.summaryPrompt);
    config.compaction.summaryModel =
        readString(node, "summaryModel", config.compaction.summaryModel);
  }

  if (has(root, "modelRoute")) {
    const Json& node = root["modelRoute"];
    config.enableModelRoute = readBool(node, "enabled", config.enableModelRoute);
    config.modelRoute.autoKeyword =
        readString(node, "autoKeyword", config.modelRoute.autoKeyword);
    config.modelRoute.maxAttempts =
        readInt(node, "maxAttempts", config.modelRoute.maxAttempts);
    config.modelRoute.retryableCodes = readStringArray(
        node, "retryableCodes", config.modelRoute.retryableCodes);
    config.modelRoute.fallbackModel =
        readString(node, "fallbackModel", config.modelRoute.fallbackModel);
    // 退避参数与 dsh BackoffConfig 同名同默认 (initialDelayMs=500/maxDelayMs=10000/
    // jitterRatio=0.1), 嵌套键名逐字一致。
    if (has(node, "backoff")) {
      const Json& backoff = node["backoff"];
      config.modelRoute.backoffInitialMs = readInt(
          backoff, "initialDelayMs", config.modelRoute.backoffInitialMs);
      config.modelRoute.backoffMaxMs = readInt(
          backoff, "maxDelayMs", config.modelRoute.backoffMaxMs);
      config.modelRoute.backoffJitterRatio = readDouble(
          backoff, "jitterRatio", config.modelRoute.backoffJitterRatio);
    }
  }

  if (has(root, "tokenMeter")) {
    const Json& node = root["tokenMeter"];
    config.enableTokenMeter = readBool(node, "enabled", config.enableTokenMeter);
    config.tokenMeter.inputPricePerMillion = readDouble(
        node, "inputPricePerMillion", config.tokenMeter.inputPricePerMillion);
    config.tokenMeter.cachedInputPricePerMillion = readDouble(
        node, "cachedInputPricePerMillion",
        config.tokenMeter.cachedInputPricePerMillion);
    config.tokenMeter.outputPricePerMillion = readDouble(
        node, "outputPricePerMillion", config.tokenMeter.outputPricePerMillion);
    config.tokenMeter.creditsPerYuan =
        readDouble(node, "creditsPerYuan", config.tokenMeter.creditsPerYuan);
  }

  if (has(root, "agentInstructions")) {
    const Json& node = root["agentInstructions"];
    config.enableAgentInstructions =
        readBool(node, "enabled", config.enableAgentInstructions);
    config.agentInstructions.projectRootMarkers = readStringArray(
        node, "projectRootMarkers", config.agentInstructions.projectRootMarkers);
    config.agentInstructions.instructionFileCandidates = readStringArray(
        node, "instructionFileCandidates",
        config.agentInstructions.instructionFileCandidates);
    config.agentInstructions.localInstructionFileCandidates = readStringArray(
        node, "localInstructionFileCandidates",
        config.agentInstructions.localInstructionFileCandidates);
    config.agentInstructions.maxBytes =
        readInt(node, "maxBytes", config.agentInstructions.maxBytes);
    config.agentInstructions.maxSourceBytes =
        readInt(node, "maxSourceBytes", config.agentInstructions.maxSourceBytes);
    if (config.agentInstructions.maxBytes < 0
        || config.agentInstructions.maxSourceBytes < 0) {
      throw std::runtime_error("agentInstructions.maxBytes / maxSourceBytes 必须非负");
    }
  }

  if (has(root, "subagent")) {
    const Json& node = root["subagent"];
    config.enableSubagent = readBool(node, "enabled", config.enableSubagent);
    config.subagent.maxDepth = readInt(node, "maxDepth", config.subagent.maxDepth);
    // 0 合法 (= 禁止委派, 对齐 dsh); 负数连「永远拒绝」都表达不了, 是配置错误。
    if (config.subagent.maxDepth < 0) {
      throw std::runtime_error("subagent.maxDepth 必须是非负整数 (0 = 禁止委派)");
    }
  }

  if (has(root, "team")) {
    const Json& node = root["team"];
    config.enableTeam = readBool(node, "enabled", config.enableTeam);
    config.team.maxMembers = readInt(node, "maxMembers", config.team.maxMembers);
    config.team.maxTasks = readInt(node, "maxTasks", config.team.maxTasks);
    config.team.maxPendingMessagesPerMember = readInt(
        node, "maxPendingMessagesPerMember",
        config.team.maxPendingMessagesPerMember);
    config.team.maxMessageBytes =
        readInt(node, "maxMessageBytes", config.team.maxMessageBytes);
    config.team.disposalTimeoutMs = readInt(
        node, "disposalTimeoutMs", config.team.disposalTimeoutMs);
    if (config.team.maxMembers < 1 || config.team.maxTasks < 1
        || config.team.maxPendingMessagesPerMember < 1
        || config.team.maxMessageBytes < 1 || config.team.disposalTimeoutMs < 0) {
      throw std::runtime_error(
          "team.maxMembers / maxTasks / maxPendingMessagesPerMember / "
          "maxMessageBytes 必须是正整数, disposalTimeoutMs 必须非负");
    }
  }

  // 跨字段校验: 开了溢出裁剪却没给目录, 到运行期才发现就晚了 (那时一次工具结果已经产出)。
  if (config.enableSpill && config.spill.maxInlineBytes > 0
      && config.spill.spillRoot.empty()) {
    throw std::runtime_error("启用溢出裁剪必须配置 spill.spillRoot");
  }
  return config;
}

AgentConfig AgentConfig::fromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    throw std::runtime_error("无法读取 agent 配置文件: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return fromJson(buffer.str());
}

}
