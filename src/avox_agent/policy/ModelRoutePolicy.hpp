#pragma once

// ============================================================================
// 模型路由与请求重试。
//
// 对齐 dsh 的做法: 选模挂 agent/request, 重试挂 agent/request-error (dsh 那边是
// dsh-llm-retry 插件), 驱动自己只认 retry / terminal 两种回答。
//
// 搬迁的对象: 旧实现 (已删) 把 auto 免费模型轮换与 429/5xx 重试硬编码在 handleSSEResponse 的
// for (attempt...) 循环里, 那个函数同时负责 SSE 分帧、事件分发、预算判定、模型选择、重试
// 判定与错误上报。搬出来之后分帧函数只做「分帧 + 分发 + 记分片」, 而选模策略可以单独测。
//
// 重试**发生在同一个 step 内**的循环里 (驱动负责), turn/step 编号不变 —— 于是日志里能
// 看出「同一步重试了三次」, 而不是凭空多出两个 step。
//
// 退避在本策略的重试臂里执行 (dsh dsh-llm-retry 插件的做法): 指数退避 + 对称抖动,
// 等待可被 abort 唤醒; 默认次数/延迟对齐 dsh 统一的 5 次重试默认 (0ca0f3d0b8)。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Scope.hpp"

namespace avox {

// 可轮换的模型池。
//
// 抽象成接口而不是直接用现有的 FreeModels: 策略只需要「给我下一个可用模型」与「这个模型
// 刚被限流」两个动作, 池的来源 (OpenRouter /models、本地配置、别的聚合站) 与策略无关。
class ModelPool {
 public:
  virtual ~ModelPool() = default;

  // 池是否需要刷新 (缓存过期)。
  virtual bool stale() const = 0;
  // 刷新池 (失败应当自行记录并保持旧内容)。
  virtual void refresh() = 0;
  // 当前可用模型数; 0 表示池为空或拉取失败。
  virtual size_t count() const = 0;
  // 取下一个可用模型 (跳过冷却中的); 无可用返回空串。
  virtual std::string pickNext() = 0;
  // 标记一个模型刚被限流, 进入冷却。
  virtual void markRateLimited(const std::string& model) = 0;
};

struct ModelRoutePolicyConfig {
  // 触发轮换的模型名。
  std::string autoKeyword = "auto";

  // 一个 step 内最多尝试多少次 (含首次请求)。
  //
  // 有上限是为了避免 5xx / 限流风暴时把池里几十个模型全试一遍 —— 每次尝试都是一次完整
  // 的往返。默认 6 = 首次 + 5 次重试, 对齐 dsh 统一的 DEFAULT_MAX_RETRIES=5
  // (0ca0f3d0b8, 旧默认 2)。
  int maxAttempts = 6;

  // 可重试的失败码。
  //
  // 用语义码而不是 HTTP 数字: provider 适配器负责把上游状态归一化成这些码, 于是策略不必
  // 认识 HTTP 细节, 换后端时也不用改这里。
  //
  // 对齐 dsh DEFAULT_RETRYABLE_CODES (EMPTY_RESPONSE/RATE_LIMIT/SERVER/TIMEOUT/TRANSPORT):
  // RATE_LIMIT→RATE_LIMITED, SERVER→UPSTREAM_ERROR, TRANSPORT→CONNECTION_FAILED,
  // TIMEOUT→REASONING_TIMEOUT; EMPTY_RESPONSE 无 avox 码 (空 200 流按正常完成处理)。
  // 连接失败/流卡死**重试但不换模型** —— 换模型对网络问题没用, 退避后原样再来。
  std::vector<std::string> retryableCodes{
      "RATE_LIMITED", "UPSTREAM_ERROR", "CONNECTION_FAILED", "REASONING_TIMEOUT"};

  // 退避参数 (dsh BackoffConfig 同名默认): 本地指数退避 initial*2^(N-1) 封顶 max,
  // 乘 [1-ratio, 1+ratio] 对称抖动再封顶; provider Retry-After ≤ max 时采原值 (无抖动)。
  int backoffInitialMs = 500;
  int backoffMaxMs = 10000;
  double backoffJitterRatio = 0.1;

  // 池拉取失败时的兜底模型 (空则不兜底)。
  //
  // 存在的理由: 池空时如果原样把字面量 "auto" 发出去, 后端只会回一个含义不明的 400。
  std::string fallbackModel;
};

// 安装模型路由策略。pool 为空时只保留重试能力 (不做轮换)。
Disposer installModelRoutePolicy(AgentExtensionPoints& points, ModelPool* pool,
                                ModelRoutePolicyConfig config);

}
