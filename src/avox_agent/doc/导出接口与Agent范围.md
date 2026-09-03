# 导出接口与 Agent 范围

> 本文是**范围契约**:哪些东西属于通用框架、哪些属于本项目的具体 agent、哪些能穿出进程边界给别的语言用。
>
> 实现见 [`../AgentExport.h`](../AgentExport.h);分层总览见 [../README.md](../README.md)。

## 两层职责边界

| 能力 | 框架层 (`core/` + `policy/`) | 具体 agent (`compose/` + `tools/` + `skills/`) |
|------|------------------------------|-----------------------------------------------|
| 会话日志 / 投影 / 循环 / 取消 | ✅ 通用 | — |
| 工具注册表与调度 | ✅ `ToolRuntime` / `Dispatch` | 注册哪些工具 (`BuiltinTools`) |
| system prompt 机制 | ✅ `SystemPrompt` 分段拼接 | 装配哪些段 (persona / 能力目录 / skill 目录) |
| 行为增强 | ✅ 六条 policy, 每条可单独关 | 开哪几条、阈值多少 (`AgentConfig`) |
| LLM 协议 | ✅ `core/Llm` 语义接口 | `provider/` + `adapter/LlmProviderAdapter` 具体实现 |
| 部署选择 (模型 / 路径 / 阈值) | — | `adapter/DeploymentLoader` 读 `agent.json` |
| 装配 | — | ✅ `composeDiagnosticAgent` 唯一定义处 |

**铁律:框架无立场。** `core/` 里不出现任何部署选择 —— 没有默认模型、没有默认阈值、没有硬编码工具名。所有这些进 `AgentConfig`,由 cordis 式的装配层一次性喂进去。

**装配只有一处。** shell、C 导出层、测试全调 `composeDiagnosticAgent`。曾经 shell 与导出层各写一遍装配,端到端首跑就撞上导出层漏 `setLlmProvider` —— 一份装配写两遍,两遍就会不一样。

## 跨语言导出 — `AgentExport.h`

三条设计决定:

### 1. 事件流用一个 JSON 回调 + 少数专用回调

```cpp
class ISessionObserver {
  virtual void onSessionEvent(size_t seq, const char* typeName, const char* eventJson) {}
  virtual void onToken(const char* text) {}
  virtual void onReasoning(const char* text) {}
  virtual void onToolCall(const char* toolName, const char* argsJson) {}
  virtual void onToolResult(const char* toolName, const char* resultText, bool ok) {}
  virtual void onTurnEnd(const char* content, const char* error) {}
  virtual void onStatus(int status) {}
};
```

不给每个事件类型一个虚函数:事件词汇表注定增长(每加一个模型可见输入就要加一个事件类型),每次增长都破 ABI,所有语言绑定重编译。JSON 穿边界不会 —— 新事件类型对老绑定就是一个它不认识的 `typeName`,而 `ignorable` 标记正好告诉它能不能安全跳过。

高频与人人都要的少数几个(token / 工具调用 / 轮次结束)另给专用回调:让每个绑定各写一遍 JSON 解析是重复劳动,也容易与内部字段名脱节。

### 2. 配置用一个 JSON 字符串

```cpp
AVOX_EXPORT IAgentHost* createAgentHost(const char* configJson);
```

不用一堆 `setXxx`:配置面很大(超时、溢出阈值、压缩比例、审批策略、重复阈值、并发上限),每加一项都要动虚表。一个 JSON 让校验在 C++ 一处完成,不必在每个绑定语言里重复。`configJson` 就是 `agent.json` 的内容。

### 3. 提供工具走进程外,不走 FFI 回调

本层只暴露「驱动 agent」与「审批应答」。让外部语言实现工具的正确通道是**脚本型 skill** 与**工具服务器子进程**,理由四条:

- GIL 死锁面:工具在驱动线程跑,回调进 Python 要拿 GIL,而 Python 侧可能正等 `waitIdle`。
- 崩溃隔离退化:同进程回调里的段错误带走整个 host。
- 取消无法到达静止:`cancel` 打不断一个卡在外语言解释器里的调用。
- 每加一项工具能力就破 ABI。

审批是唯一例外 —— 它的形状(问一句、等一个枚举)规避了全部四个坑:

```cpp
class IApprovalUi {
  // 0 = 一次性允许, 1 = 拒绝, 2 = 取消, 3 = 不可用
  virtual int ask(const char* toolName, const char* callId, const char* reason) = 0;
};
```

词汇表里没有「永久允许」:一次询问的答案只能是一次性授权,持久偏好属于会话策略(`approval.requireApproval`)。

## 生命周期契约

SWIG `%newobject createAgentHost` 把释放交给 GC,而本对象的析构有静止 (quiescence) 语义:

- `shutdown()` 是收敛点。返回后保证:无驱动线程、无在跑的工具、日志已 flush。此后观察者与应答方对象可安全释放。
- 析构会兜底调 `shutdown()`,但宿主语言在解释器退出期触发的 GC 里,等待线程可能挂死。**宿主必须显式 `shutdown()`**(Python 侧用 `with`)。
- `openAgent` 返回**借用指针**,由 host 拥有 —— agent 的生命周期必须嵌套在 host 内(它引用 host 的注册表与日志写入器),GC 顺序不确定就会 use-after-free。
- `ISessionObserver` / `IApprovalUi` 是借用指针,生命周期须覆盖到 `shutdown()` 返回。
- `waitIdle(timeoutMs)` 务必给**有限**超时并在等待期间释放 GIL,否则解释器连 Ctrl+C 都收不到。

## 为什么 core 不导出

avox 用静态 CRT (`/MT`)。跨 DLL 传 `std::string` / `std::vector` 会在两份堆之间来回,释放时崩溃。所以:

- `core/` 的类**不加 `AVOX_EXPORT`**。
- 跨边界只走 `AgentExport.h`:裸指针 + `const char*` + POD。
- 需要直接用 core 类型的测试(如 `sessiontest`)把 core 源文件**编进自己的目标**,不链 dll。

## 输入通道

三条队列 × 是否唤醒驱动,对应三个方法:

| 方法 | 进哪条队列 | 唤醒驱动 | 用途 |
|------|-----------|---------|------|
| `followup(text)` | next-turn | 是 | 普通的下一轮提问 |
| `steer(text)` | 最近 step | 是 | 工具跑到一半用户改主意, 不必打断整轮 |
| `inject(text)` | 下次 pre-step 上下文 | 否 | 补充上下文, 不主动触发一轮 |

`inject` 不唤醒是关键区别:它只是"下次请求时也把这个带上",不代表用户要一个回答。

## 脱离模型跑 skill

```cpp
AVOX_EXPORT const char* runSkill(const char* skillName, const char* userInput);
```

返回 `{"skill", "output"}` JSON,内部缓冲托管,下次调用即失效,**不需要**释放。`freeSkillResult` 是历史遗留的空实现,保留只为不破坏已在调它的外部代码;新代码不要调。

用途:手测一条 skill、外部语言直接复用 avox 的诊断能力而不付一次模型往返。
