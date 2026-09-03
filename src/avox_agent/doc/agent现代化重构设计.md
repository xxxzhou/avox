# agent 现代化重构设计

把 `avox_agent` 的 LLM 协议层从「OpenAI Chat Completions 焊死」抽象成可插拔的 **Provider**，并配套收敛三处散落机制（中断 / HTTP / 工具结果）。目标是能接 OpenAI 新一代 **Responses API** 与 **Anthropic 原生**协议，同时让 SSE 解析可离线测试。

> **状态:已落地,其中一部分被后续重构取代。** 本文写于 `AgentClient` 时代。它提出的 Provider 与 IHttpTransport 两处收敛保留至今(`provider/`、`transport/`);`CancelToken` 已被 `core/Abort` 的 `AbortSignal` 取代并删除;`AgentClient` / `AgentContext` / `ToolBase` / `ToolRegistry` 已被按 deepseek-harness 分层重做的 `core/` + `policy/` + `compose/` 取代并删除。文中出现的这些名字是**当时的现状描述**,不是现在的代码。
>
> 当前实现看 [../README.md](../README.md)、[avox_agent新架构总览.md](avox_agent新架构总览.md)、[core会话日志架构.md](core会话日志架构.md);本文保留作决策依据(为什么要有 Provider 接缝、为什么中断要收敛成 CancelToken)。

> 本文讲**架构设计**(What/Why)。现行的工具体系见 [skill与工具体系](skill与工具体系.md),整体架构见 [avox_agent新架构总览](avox_agent新架构总览.md)。

## 背景与动机

当前 `AgentClientSSE.cpp` / `AgentPayload.cpp` 把 OpenAI Chat Completions 协议焊死在三处:

| 耦合点 | 位置 | OpenAI 专有内容 |
|--------|------|----------------|
| 请求构建 | `AgentPayload.cpp::buildRequestPayload` | `messages` + `tools`(FC JSON) + `max_tokens`/`stream` |
| SSE 解析 | `AgentClientSSE.cpp::handleSSEResponse` | `choices[0].delta.{content,reasoning_content,tool_calls}` + `[DONE]` |
| 工具循环 | `handleToolCallLoop` | round-2 的 `assistant.tool_calls` + `role:tool.tool_call_id` |

由此带来四个问题:

1. **协议不可换**——OpenAI 2025 推出 [Responses API](https://developers.openai.com/api/docs/guides/migrate-to-responses)(`/v1/responses`,`input→output` 结构,单请求多轮 tool use,reasoning summary 事件),以及 Anthropic 原生 `/v1/messages`,都接不进来。
2. **工具成败二态**——`IAgentTool::execute(args, bool& success)` 只有成/败,loop 无法据结果做重试/换路。
3. **中断逻辑散落**——`cancelRequested` + `activeSock` + `sockMtx` 散在 `AgentClient` 成员里。
4. **httplib 直接耦合**——SSE 解析无法脱离真实网络做离线测试。

## 设计目标与铁律

照开源框架 [corvus](https://github.com/Ambar-Gupta22/corvus) 的理念做四项**纯接口现代化重构(P0~P3)**:

```
P0  Provider 抽象      协议相关(payload/SSE/工具消息格式)收进 Provider
P1  ToolResult 接口化  bool& success → execute 返回 IToolResult*(STL-free, 调用方 delete)
P2  CancelToken        散落中断状态 → 一个 CancelToken 对象
P3  IHttpTransport     httplib seam, 支持 Mock 离线测试
```

**铁律:重构 ≠ 改行为**。第一版只实装 `ChatCompletionsProvider`(现有逻辑原样搬入),OpenAI 路径行为**字节级不变**。Anthropic / Responses 只预留 Provider 接口位置 + factory fallback,不做实装。本次不涉及 SWIG 导出。

### Responses / Anthropic 的定位(只预留不实装)

| 协议 | 端点 | 生态兼容性 | 本次 |
|------|------|-----------|------|
| Chat Completions | `/v1/chat/completions` | OpenAI/DeepSeek/Ollama/千帆/lm-studio 全兼容 | **实装(搬现有)** |
| Responses | `/v1/responses` | 仅 OpenAI 官方 + vLLM;DeepSeek/Ollama 不支持 | 预留接口 |
| Anthropic | `/v1/messages` | Anthropic 官方 | 预留接口 |

走「OpenAI 兼容」生态注定了**主力必须仍是 Chat Completions**。Responses/Anthropic 预留是为了将来加 Provider 时 AgentClient 零改动。

## P0 — Provider 抽象(核心)

**把协议相关收进 Provider,协议无关留 AgentClient。**

```
AgentClient (协议无关)              Provider (协议相关)
┌─────────────────────┐            ┌──────────────────────┐
│ worker 线程          │            │ buildPayload         │
│ CancelToken 中断     │  req ────→ │ onSseLine            │
│ IHttpTransport 传输  │  ←─── 事件 │ isStreamEnd          │
│ 观察者通知           │            │ (OpenAI/Anthropic/   │
│ 工具执行/预算        │            │  Responses 各一)     │
└─────────────────────┘            └──────────────────────┘
```

### 关键决策:ProviderRequest 必须语义化

**不能塞 OpenAI 格式的 Json 进 ProviderRequest**——否则等于把 OpenAI 协议焊死进接口,Anthropic/Responses Provider 又得反向适配,丧失抽象意义。Provider 自己把语义结构序列化成本家 wire 格式。

```cpp
// src/avox_agent/provider/IAgentProvider.hpp
struct ProviderPart { PartType type; std::string data; };          // 复用 AgentExport.h 的 PartType
struct ProviderMessage {
  AgentRole role;                                                  // System/User/Assistant
  std::vector<ProviderPart> parts;
  std::string assistantToolCallsWire;  // round-2+ assistant 消息填(Provider 自行序列化)
  std::string toolCallId;              // role=Tool 关联用
};
struct ProviderRequest {
  std::vector<ProviderMessage> messages;   // 会话状态(跨 round 累积)
  std::string model; float temperature=0.7f; int maxTokens=16384;
  std::string toolsDefinitionJson;         // OpenAI FC JSON(其它 Provider 将来自身转格式)
  std::string routingHint;                 // ChatCompletionsProvider 追加到最后一条 user
};
```

> 注意:`ProviderRequest.messages` 取代今天的 wire round-trip——当前 `handleToolCallLoop` 从首轮 payload 反 dump 出 history 再 parserJson 回来续写(`AgentClientSSE.cpp:298-315`),P0 改为直接维护语义消息列表,消除字节漂移。

### 统一 SSE 语义事件流

三种协议的 SSE 事件格式完全不同(OpenAI `choices[].delta` / Anthropic `content_block_delta` / Responses `response.*.delta`)。抽象的精髓是把它们统一成一套语义事件:

```cpp
enum class SseEventKind { Text, Reasoning, ToolCallBegin, ToolCallArgs, Done, Error };
struct SseEvent {
  SseEventKind kind = SseEventKind::Text;
  std::string text;          // Text/Reasoning
  int toolIndex = -1;        // ToolCall*
  std::string toolId, toolName, argsFrag;
};
```

**SSE framing(行切分 / `data: ` 剥离 / 空行)留 Agent 主循环**(三协议通用);Provider 只收已剥前缀的 dataLine:

```cpp
class IAgentProvider {
 public:
  virtual const char* name() = 0;                                         // "openai-chat"/"anthropic"/"openai-responses"
  virtual std::string buildPayload(const ProviderRequest& req) = 0;       // 首轮 + round-2 共用
  virtual bool onSseLine(const std::string& dataLine, std::vector<SseEvent>& out) = 0;
  virtual bool isStreamEnd(const std::string& dataLine) = 0;              // [DONE]/message_stop/response.completed
};
```

### ChatCompletionsProvider(实装,搬现有)

| 方法 | 搬自 | 不变量 |
|------|------|--------|
| `buildPayload` | `buildRequestPayload` + round-2 续发拼装 | system 注入、routing hint 追加、`tool_call_id` 一一对应、`stream:true` |
| `onSseLine` | `AgentClientSSE.cpp` 的 delta 解析 | reasoning 三键名 fallback、按 index 累积 tool_call |
| `isStreamEnd` | `[DONE]` 判定 | — |

### 协议无关 SSE 主循环(handleSSEResponse 重构后)

```
onChunk(data):
  行切分 + 剥 "data: "
  → provider.isStreamEnd(line)?  continue
  → provider.onSseLine(line, events)
  → 分发事件:
       Text        → fullContent += ; notifyToken ; progressed=true
       Reasoning   → notifyReasoning            (不计 progressed! 保纯推理预算)
       ToolCallBegin→ toolCallMap[idx] ; notifyToolCall(name,"") ; progressed=true
       ToolCallArgs → 累积 + 增量 notifyToolCall ; progressed=true
  预算: progressed 刷新 lastProgress; 纯推理超 reasoningTimeoutSeconds 断流
```

`handleToolCallLoop`(协议无关):执行 toolCallsDump 每个 tool_call(拿 ToolResult)→ 追加 `ProviderMessage` 到 `req.messages` → `handleSSEResponse(req)` 续发。

### Provider 工厂

```cpp
std::unique_ptr<IAgentProvider> makeProvider(const AgentConfig& cfg);
// 未填/"openai" → ChatCompletionsProvider
// "anthropic"/"openai-responses" → fallback 到 ChatCompletionsProvider + LOGFLF warn(暂未实装)
// 未知 → fallback + warn
```

**v1 绝不抛错**(避免破坏现有 agent.json)。`config.provider` 字段从「仅语义标注」变为 Provider 选择的真值来源。

## P2 — CancelToken(最先做)

收敛 `cancelRequested` + `activeSock` + `sockMtx` 三者:

```cpp
// src/avox_agent/cancel/CancelToken.hpp
class CancelToken {
 public:
  void requestCancel();              // 仅置标志(KeyboardInterrupt 路径: 此刻无活跃 socket)
  void cancel();                     // 置标志 + shutdown 活跃 socket(Ctrl+C 路径: 解除阻塞 recv)
  bool isCancelled() const noexcept;
  void registerSocket(intptr_t s);   // httplib set_socket_options / transport 回调调
  void clearSocket();
  void reset();                      // 每轮 SSE 入口调
 private:
  std::atomic<bool> flag{false};
  std::mutex sockMtx;
  std::atomic<intptr_t> sock{-1};
};
```

跨平台 shutdown(`SD_BOTH` / `SHUT_RDWR`)收进 `cancel()` 内部 `#ifdef _WIN32`。

**区分两条路径**:外部 Ctrl+C → `cancel()`(标志+shutdown);Python 子进程 KeyboardInterrupt → `requestCancel()`(只标志,此刻 SSE 已结束无活跃 socket)。`lastInterrupted`(Python 打断语义标记)不并入 CancelToken,留 AgentClient。

## P3 — IHttpTransport seam(第二个做)

把 httplib 抽成可 mock 接口。**已确认 `httplib.h` 只在 `AgentClient.cpp` + `AgentClientSSE.cpp` 两处 include**,P3 只接管 SSE 主链路的 `cli.Post`,不扩面接管 `available()` 探活和 `vaildHttps` 诊断(继续直接用 httplib)。

```cpp
// src/avox_agent/transport/IHttpTransport.hpp
struct HttpSseResult { bool gotResponse=false; int status=0; std::string errReason; };
class IHttpTransport {
 public:
  using Receiver  = std::function<bool(const char*, size_t)>;
  using SocketHook = std::function<void(intptr_t)>;
  virtual void setTimeouts(int connSec, int readSec) = 0;
  virtual HttpSseResult postSse(const std::string& baseUrl, const std::string& path,
                                const std::string& payload, const std::string& bearerKey,
                                const SocketHook& onSocket, const Receiver& onChunk) = 0;
};
```

`HttpSseResult` 三字段对应今天 `!res`(连接失败)/`res`(有响应)/`status` 三分支。`HttplibTransport` 搬 `AgentClientSSE.cpp:89~242` 的 Client 装配 + Post。

**MockTransport**(测试桩):持预录 SSE 字节流按 chunk 喂 onChunk,连接时调一次 onSocket,onChunk 返 false 立即停。**这是 P0 字节对比的自动化抓手**。

## P1 — ToolResult 接口化(第三个做)

`IAgentTool::execute(args, bool& success)` → 返回 `IToolResult*`。**纯接口化,行为零变化**。

设计要点:导出 `.h` 的 STL-free 铁律不允许再放 `std::string`。故 `IToolResult` 是纯虚接口(`getContent()` 取内容),实体 `ToolResultImpl`(持 `std::string`)在 `.hpp`/`.cpp` 层(`tools/ToolResult.hpp` 的 `makeToolResult*` 工厂 new 出)。`execute` 返回堆指针,**调用方(AgentClient/ToolRegistry)消费完即 delete**;`IToolResult` 不跨 C 导出边界(不在 swig `common.i`),无 `%newobject`,godot 侧只用 `onAgentToolResult` 回调,零改动。

```cpp
// src/avox/AvoxAgent.h (公共导出层, STL-free: 无 std::string)
enum class ToolOutcome { Ok, Fatal };   // 只两态: 不预留 Retryable/Timeout(避免诱使下游写新分支)
class IToolResult {
 public:
  virtual ~IToolResult() = default;
  virtual ToolOutcome getOutcome() const = 0;
  virtual const char* getContent() const = 0;   // 实现层 ToolResultImpl 持实体
  bool ok() const noexcept { return getOutcome() == ToolOutcome::Ok; }
};
class IAgentTool {
  virtual IToolResult* execute(const char* argumentsJson) = 0;   // 调用方 delete; 替代 execute(args, bool& success)
};
```

### 关键事实:success 不是真相

`handleToolCallLoop` 给观察者的 `ok` **实际走字符串前缀判定**,不读 success:

```cpp
bool toolOk = toolResult.rfind("FAIL:", 0) != 0 && toolResult != "(cancelled)";
```

且各工具自设的 `success` 含义还不一致(CmdTool 看 exitCode;ExecuteTool 即使未知 skill 也 true;SkillTool 恒 true)。**P1 必须保留这条字符串判定**(读 `IToolResult::getContent()`)作为观察者 `ok` 来源,否则行为退化。`outcome`(`getOutcome()`)只做类型化记录,P1 不引入新分支语义。

### 逐工具 outcome 映射(保证旧 success ⟺ 新 outcome)

| 工具 | 旧 success 逻辑 | 新 outcome |
|------|----------------|-----------|
| RunCodeTool | 不以 `FAIL:` 开头 | Ok / Fatal |
| CmdTool | exitCode==0 | Ok / Fatal |
| SkillTool | 恒 true | 恒 Ok |
| ExecuteTool | 恒 true(含未知 skill) | 恒 Ok |
| ReadTool | 缺参/打不开=false | Fatal / Ok |
| GrepTool | 缺参/打不开/正则非法=false | Fatal / Ok |

### Windows SEH:接口化后 C2712 自动消失

原 `execute` 返回含 `std::string` 的 `ToolResult`,含 `__try` 的函数若有需展开对象会触发 C2712,故拆双层(`invokeTool` + `sehExecuteTool`,`ToolResult& sink` 出参)。接口化后 `execute` 只返回**平凡指针 `IToolResult*`**(实体在堆上、不在栈上展开),C2712 不成立——`sink` 出参与双层拆分均可删除,收成单个 `__try`:

```cpp
static DWORD sehExecuteTool(ToolBase* t, const char* args, IToolResult*& out) {
  __try { out = t->execute(args); return 0; }
  __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
```

崩溃隔离语义不变;SEH 命中 fatal 时由调用方用 `makeToolResultFatal(...)` 补一个 `IToolResult*` 占位,保证 `out` 非空、调用方统一 `delete`。

## 实施路线图

按依赖排序,避免返工:

```
P2 CancelToken ──→ P3 IHttpTransport ──→ P1 ToolResult ──→ P0 Provider
   最小/最封闭      P0 坐在 transport       独立,P0 改         最大重构,三 seam
   先收敛中断       seam 上,先有它          tool loop 时希     就位后一次性写
                   才能写协议无关循环       望已是 ToolResult   协议无关 SSE/tool loop
```

**不推荐 P0 先做**:P0 v1 无用户可见行为变化(字节等价),「用户最关心」的接口预留只是头文件里未实装子类的注释。先做 P0 反而会因 P2/P3/P1 未就位而「先耦合写一遍再返工抽 seam」的双倍改动。

### 新增文件

| 路径 | P |
|------|---|
| `cancel/CancelToken.hpp/.cpp` | P2 |
| `transport/IHttpTransport.hpp`、`HttplibTransport.hpp/.cpp`、`MockTransport.hpp` | P3 |
| `provider/IAgentProvider.hpp`、`ChatCompletionsProvider.hpp/.cpp`、`ProviderFactory.hpp/.cpp` | P0 |

### 修改文件

- `avox/AvoxAgent.h`(P1:加 ToolOutcome/IToolResult 接口 + 改 execute 返 IToolResult*)
- `AgentClient.hpp/.cpp`(P2/P3/P0 三轮)
- `AgentClientSSE.cpp`(P2/P3/P1/P0,P0 主战场)
- `AgentPayload.cpp`(P0:语义化 + 搬走 wire 构建)
- `tools/ToolBase.hpp/.cpp`、`ToolRegistry.hpp/.cpp`(P1)
- 6 个工具 `.cpp`(P1:改 execute 返 IToolResult*,经 `makeToolResult*` 工厂 new ToolResultImpl)
- `src/CMakeLists.txt`:加 3 条 `add_sub_path`(cancel/transport/provider);BoringSSL 守卫目标改 `HttplibTransport.cpp`

> `add_sub_path` 单目录 glob 不递归(`cmake/AVOXHelper.cmake:43`),新增子目录必须各加一条。

## 行为不变风险清单(R1~R9)

| # | 风险 | 守恒要求 |
|---|------|---------|
| R1 | 流式 tool_call 按 index 累积 | 同 chunk 先 ToolCallBegin 后 ToolCallArgs,name 非空才 Begin |
| R2 | reasoning 三键名 fallback | `reasoning_content`→`reasoning`→`thinking` 首个命中 break,只 emit 一个 |
| R3 | 纯推理预算 | 只有 Text/ToolCall* 算 progressed,Reasoning 不算(否则预算永不触发) |
| R4 | round-2 wire | `tool_call_id` 一一对应、去 index 补 `call_<i>` id 必须保留(对接千帆严格后端) |
| R5 | TrackRecorder 的 success | 改用 `result.ok()`,严格保持,勿错改成字符串判定 |
| R6 | cancelToken.reset() | 只在新 SSE 轮入口,不在 tool loop 入口 |
| R7 | onChunk cancel 短路 | onChunk 返 false + Post 返回后短路两层都改 cancelToken;MockTransport 也尊重返 false |
| R8 | buildPayload 字段 | 恒带 `stream:true`,字段与今天一致 |
| R9 | httplib 收敛 | 不再出现在 AgentClient.cpp/AgentClientSSE.cpp(只 HttplibTransport.cpp) |

## 验证(每 P 完成后不退化)

构建:`python build_windows.py`(成败看 output「项目构建完成」,别信 exit 0)。

- **P2**:双击 avox_agent 发对话→生成中 Ctrl+C→立即停 + 标「(已中断)」;run_code 跑 `while True:pass`→Ctrl+C→KeyboardInterrupt 路径停。
- **P3**:真实 LLM 跑带工具对话(HTTP 200/非200/连接失败三路径);MockTransport 喂预录 SSE 断言 tool_call 累积/预算/cancel。
- **P1**:6 工具正常/异常路径逐个验 outcome + content 与今天一致;`notifyToolResult` 的 ok 仍走字符串判定;Windows SEH 隔离仍生效(mock 工具抛 AV 不崩进程)。
- **P0(最严)**:
  1. 同一 ProviderRequest 下,改前 `buildRequestPayload` 与改后 `ChatCompletionsProvider::buildPayload` 输出 JSON **逐字节对比**(首轮 + round-2)。
  2. MockTransport 喂同段真实录制 SSE,改前改后 notify* 调用序列逐个对比。
  3. 真实 DeepSeek/Ollama/千帆各跑一轮带 tool_call 对话,round-2 续发 200 不返 400。
  4. agent.json provider 改「anthropic」→fallback + warn 不崩。
  5. 触发词路由核对 routing hint 仍追加到最后一条 user(不进 system,保 KV cache)。

## 范围边界(不做)

- Anthropic 原生 / Responses API **实装**(只预留 Provider 接口 + factory fallback)。
- SWIG / Python 导出(纯 C++ 内部重构)。
- P1 引入 Retryable/Timeout **分支语义**(纯类型化;Fatal 换路留作后续)。
- IHttpTransport 接管 `available()` / `vaildHttps`(P3 不扩面)。

## 实现状态（P0~P3 全部完成, 2026-08）

四项重构全部落地, `python build_windows.py` 通过("项目构建完成")。OpenAI 路径逐函数对照保字节级一致。

### 与原规划的偏离

| 点 | 原规划 | 实际实现 | 原因 |
|----|--------|---------|------|
| 消息角色 | `ProviderMessage.role: AgentRole` | 新增 `ProviderRole {System,User,Assistant,Tool}` | AgentRole 无 Tool, 但 round-2 工具结果消息需 role:tool(原规划注释自相矛盾) |
| routing 注入 | `ProviderRequest.routingHint` 字段, Provider 追加 | 删该字段, Agent `applyRouting` 在 build\*Request 时一次追加到 user.data | 留 Provider 则 round-2(req.messages 末尾是 tool)触发兜底独立 user 消息, 与旧 round-2(user 内含 routing)字节不同; 移 Agent 一次追加, round-2 继承 user.data 已含 routing, 字节一致。且 routing 是 avox skill 业务非 LLM 协议 |
| SseEvent | 无 type 字段 | 加 `toolType` | ToolCallBegin 带 type(默认 function)保 toolCallsDump 字节 |
| makeProvider | `(const AgentConfig&)` | `(const std::string& providerName)` | 避免 ProviderFactory.hpp ↔ AgentClient.hpp 循环 include |
| R9 httplib 收敛 | "不再出现在 AgentClient.cpp/AgentClientSSE.cpp" | 仅 SSE 主链路收敛到 HttplibTransport; `available()`/`vaildHttps` 仍直接用 httplib | P3 范围边界明确不扩面接管探活/诊断 |

### 字节一致性(代码审查, 非 LLM 实跑)

- **首轮**: `buildPayload`(system 注入 + messages 序列化 + tools)字段顺序/值同旧 `buildRequestPayload`。
- **round-2**: `req.messages=[user(text+routing), assistant, tool]` → `buildPayload` 注入 system → `[system, user, assistant, tool]`, 同旧 `handleToolCallLoop` 内联 newPayload; assistant.tool_calls 经 `assistantToolCallsWire`(dump→parse 还原保字节), tool.tool_call_id 一一对应(R4)。
- **消除 wire round-trip**: 旧 `parserJson(payload)` 反解 history 续写改为直接维护语义 `req.messages` 累积。
- **SSE**: `onSseLine` 事件经 `handleSSEResponse` 分发, tool_call 累积/流式展示/预算逐行对应旧 onChunk(唯一差异: 极罕见的空 tool_calls 数组不再计 progressed, 仅影响预算刷新时机)。

> LLM 实跑验证(真实 DeepSeek/Ollama/千帆 round-2 续发 200、provider=anthropic fallback warn、routing 仍追加到最后一条 user)待接入测试环境后补。

## 参考

- [corvus](https://github.com/Ambar-Gupta22/corvus) — C++17 AI agent runtime(架构参考:Provider 抽象 / ToolResult 枚举 / CancelToken / HttpTransport seam)
- [llm-cpp toolkit](https://github.com/Mattbusel/llm-cpp) — 26 单头文件(llm-compress 对应 AgentContext 预算压缩)
- [OpenAI Responses API 迁移指南](https://developers.openai.com/api/docs/guides/migrate-to-responses)
- [vLLM Responses API 兼容性](https://docs.vllm.ai/en/latest/serving/online_serving/openai_compatible_server/)
