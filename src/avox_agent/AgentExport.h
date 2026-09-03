#pragma once

// ============================================================================
// avox_agent 的对外导出层 (SWIG 友好)。
//
// 三条设计决定:
//
// 1. **事件流用一个回调 + JSON**, 不给每个事件类型一个虚函数。
//    事件词汇表注定会增长 (每加一个模型可见输入就要加一个事件类型)。给每类一个虚函数,
//    每次增长都破 ABI, 所有语言绑定重编译; JSON 穿边界不会 —— 新事件类型对老绑定就是一个
//    它不认识的 typeName, 而 ignorable 标记正好告诉它能不能安全跳过。
//    高频与人人都要的少数几个 (token / 工具调用 / 轮次结束) 另给专用回调, 长尾走 JSON。
//
// 2. **配置用一个 JSON 字符串**, 不用一堆 setXxx。
//    配置面很大 (超时、溢出阈值、压缩比例、审批策略、重复阈值、并发上限), 每加一项都要动
//    虚表。一个 JSON 让校验在 C++ 一处完成, 不必在每个绑定语言里重复。
//
// 3. **提供工具走进程外, 不走 FFI 回调**。
//    本层只暴露「驱动 agent」与「审批应答」。让外部语言实现工具的正确通道是脚本型 skill
//    与工具服务器 (子进程), 理由: GIL 死锁面、崩溃隔离退化、取消无法到达静止、以及每加一项
//    工具能力就破 ABI。审批是唯一例外 —— 它的形状 (问一句、等一个枚举) 规避了全部四个坑。
//
// 生命周期契约 (SWIG %newobject 把释放交给 GC, 而本对象的析构有 quiescence 语义):
//   * shutdown() 是收敛点。返回后保证: 无驱动线程、无在跑的工具、日志已 flush。
//     此后观察者与应答方对象可安全释放。
//   * 析构会兜底调 shutdown(), 但宿主语言在解释器退出期触发的 GC 里, 等待线程可能挂死。
//     **宿主必须显式 shutdown()** (Python 侧用 with 语句)。
//   * openAgent 返回**借用指针**, 由 host 拥有 —— agent 的生命周期必须嵌套在 host 内
//     (它引用 host 的注册表与日志写入器), GC 顺序不确定就会 use-after-free。
//   * ISessionObserver / IApprovalUi 是借用指针, 生命周期须覆盖到 shutdown() 返回。
// ============================================================================

#include <cstddef>

#include "avox/AvoxDef.h"

namespace avox {

// 会话事件观察者。
class ISessionObserver {
 public:
  virtual ~ISessionObserver() = default;

  // 任意一条会话事件。
  // typeName 是稳定的 wire 名 ("turn/start" / "tool/call" / ...)。
  // eventJson 是完整事件行 (含 seq / time / data)。
  virtual void onSessionEvent(size_t seq, const char* typeName,
                             const char* eventJson) {}

  // 文本分片的快路径 (高频; 同一条分片也会经 onSessionEvent 到达)。
  virtual void onToken(const char* text) {}

  // 推理分片 (仅展示, 不进模型历史正文)。
  virtual void onReasoning(const char* text) {}

  // 模型请求了一次工具调用 (argsJson 是模型原样产出的完整入参)。
  //
  // 有专用回调而不是让消费者自己从 onSessionEvent 的 JSON 里挖: 工具调用是每个 UI 都要
  // 渲染的东西, 让每个绑定各写一遍 JSON 解析是重复劳动, 也容易与内部字段名脱节。
  virtual void onToolCall(const char* toolName, const char* argsJson) {}

  // 一次工具调用的结果。ok = 未失败。
  virtual void onToolResult(const char* toolName, const char* resultText, bool ok) {}

  // 一轮结束。content = 本轮最后一条助手回复的文本; error 非空表示本轮失败。
  virtual void onTurnEnd(const char* content, const char* error) {}

  // 状态翻转: 0 = idle, 1 = running。
  virtual void onStatus(int status) {}
};

// 审批应答方 (唯一允许的跨语言回调)。
class IApprovalUi {
 public:
  virtual ~IApprovalUi() = default;

  // 返回 0 = 一次性允许, 1 = 拒绝, 2 = 取消, 3 = 不可用。
  //
  // 词汇表里没有「永久允许」: 一次询问的答案只能是一次性授权, 持久偏好属于会话策略。
  virtual int ask(const char* toolName, const char* callId, const char* reason) = 0;
};

// 一个打开的会话。
class IAgentSession {
 public:
  virtual ~IAgentSession() = default;

  // ---- 输入: 两条队列 × 是否唤醒的三个预设 ----

  // 排一个普通的后续轮次并唤醒驱动。
  virtual void followup(const char* text) = 0;
  // 为最近的一个 step 投递插话 (工具跑到一半时用户改主意, 不必打断整轮)。
  virtual void steer(const char* text) = 0;
  // 为下一次 pre-step 排入上下文, 不唤醒驱动。
  virtual void inject(const char* text) = 0;

  // 带图片附件的 followup: 每张图经附件仓 publish 成 sha256 引用后, 与文字合成一条
  // UserMessage (TextBlock + N×ImageBlock) 入队唤醒 —— 图以一等公民直进模型上下文,
  // 不绕 see-image 工具轮次。imagesJson 为数组字符串, 元素:
  //   {"data":"<base64,不带 data: 前缀>", "mediaType":"image/png", "name":"可选"}
  // mediaType 白名单 image/png|jpeg|webp|gif; 字节/像素/单边限额由附件仓执行
  // (与 dsh attachment-local 同源)。
  //
  // 返回 0 = 成功; 非 0 = 失败, 原因经 lastInputError() 取 —— 接口层不出异常
  // (插件侧禁了 C++ 异常), 错误传递风格与 createAgentHost 的 error 出参一致。
  virtual int followupImages(const char* text, const char* imagesJson) = 0;
  // 最近一次 followupImages 失败的原因; 返回内部缓冲借用指针, 下次调用即失效。
  virtual const char* lastInputError() = 0;

  // ---- 控制 ----

  // cause: 0 = user, 1 = parent, 2 = hook, 3 = disposed。
  virtual void cancel(int cause) = 0;

  // 阻塞到静止; timeoutMs < 0 为无限等。返回是否已静止。
  //
  // 宿主语言务必给有限超时并在等待期间释放 GIL, 否则解释器连 Ctrl+C 都收不到。
  virtual bool waitIdle(int timeoutMs) = 0;

  // 0 = idle, 1 = running。
  virtual int status() = 0;

  // 立刻做一次压缩 (占用维护相位)。返回是否被受理。
  virtual bool compactNow() = 0;

  // ---- 观察 ----

  virtual void addObserver(ISessionObserver* observer) = 0;
  virtual void removeObserver(ISessionObserver* observer) = 0;

  // ---- 读日志 (回放 / UI 重建) ----

  virtual size_t eventCount() = 0;
  // 返回值由内部缓冲托管, 下次调用即失效。
  virtual const char* eventJson(size_t seq) = 0;
  virtual const char* trackPath() = 0;
};

// Agent 宿主。
class IAgentHost {
 public:
  virtual ~IAgentHost() = default;

  // 收敛点。见本文件头部的生命周期契约。幂等。
  virtual void shutdown() = 0;

  // 打开会话。sessionId 为空则新建; 非空且已有日志文件则 resume (含完整工具历史)。
  //
  // 返回**借用指针** (由 host 拥有, 不要 delete); 失败返回 nullptr, 原因见 lastError()。
  virtual IAgentSession* openAgent(const char* sessionId) = 0;
  virtual void closeAgent() = 0;

  // 注册一段静态 system prompt 片段 (进 KV cache 前缀)。
  //
  // 建议在 openAgent 之前调用: 会话开始后修改会使缓存前缀从变动处失效。运行期要告诉模型
  // 什么, 用 IAgentSession::inject。
  virtual bool addPromptSection(const char* name, int order, const char* text) = 0;

  // 借用指针; 为空 = fail closed (需要审批的工具全被拒)。
  virtual void setApprovalUi(IApprovalUi* ui) = 0;

  virtual const char* lastError() = 0;
};

extern "C" {

// 创建宿主 (SWIG %newobject, 目标语言 GC 释放; C++ 侧 delete)。
//
// configJson 就是 agent.json 的内容 (多配置 + now)。校验失败返回 nullptr, 原因经
// lastAgentHostError() 取。
AVOX_EXPORT IAgentHost* createAgentHost(const char* configJson);

// 最近一次 createAgentHost 失败的原因。
AVOX_EXPORT const char* lastAgentHostError();

// avox_agent 可执行的入口 (main 仅一行壳调它)。
AVOX_EXPORT int agentShellRun();

// 脱离模型直接跑一条预定义 skill。返回 {skill, output} JSON。
//
// 返回值由内部缓冲托管, 下次调用即失效, **不需要**释放。
AVOX_EXPORT const char* runSkill(const char* skillName, const char* userInput);

// 列出 config/providers.json 里所有可对话 (chat=true) 模型条目 ——
// 与 AgentShell 的 /list /ls 等价, 用于在不带 shell 的前端 (网页/Electron) 里挑对话模型。
//
// 输出 JSON 数组, 每个元素字段:
//   provider        厂商名 (providers.json 顶层 providers 键)
//   model           模型名 (厂商下 models 键)
//   name            provider/model 拼接, 便于一行展示
//   free            true = 免费档
//   ready           true = 密钥非占位符, 可直接发请求; false = 密钥待用户替换
//   needsApiKey     true = apiKey 是占位符或空
//   apiKeyHint      占位密钥对应的申请指引 (无则 "")
//   apiUrl          端点基址
//   apiPath         端点路径
//   contextWindow   上下文窗口 tokens (0 = 未配置)
//   imageInput      支持图像理解
//   imageOutput     支持文生图
//
// 返回值由内部缓冲托管, 下次调用即失效。失败 (JSON 解析出错等) 返回 "[]"。
AVOX_EXPORT const char* avoxListProviders();
}

}
