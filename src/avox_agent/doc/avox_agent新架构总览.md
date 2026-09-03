# avox_agent 新架构总览(DSH 移植)

> 范围:`src/avox_agent/` 新移植的 `core/ compose/ policy/ adapter/ provider/` 五个目录。这是把 **deepseek-harness(DSH)** 的 agent-loop 核心整套移植成 C++ 的产物。
>
> **本文讲会话日志以外的全部部分 + 整体装配**;会话日志(SessionTypes/Surface/Session/SessionCodec/SessionPersistence)的深读见 [`core会话日志架构.md`](core会话日志架构.md)。两份合起来 = 整个新架构。
>
> DSH 源仓库:`D:\Work\github\deepseek-harness`(`packages/`)。

---

## 0. 一张图看懂分层

```
                         ┌─────────────────────────────────────┐
   配置/装配              │  compose/   显式装配 (取代 DSH 插件加载器)  │
   (agent.json → 活 agent)│   AgentConfig · AgentHost · DiagnosticAgent │
                         └──────────────┬──────────────────────┘
                                        │ 持有 / 安装策略
              ┌─────────────────────────┼─────────────────────────┐
              ▼                         ▼                         ▼
   ┌──────────────────┐      ┌───────────────────┐     ┌──────────────────┐
   │  policy/ 策略插件  │      │  core/ 驱动与状态   │     │  adapter/ 适配层   │
   │ 压缩·路由·审批·   │注册到 │  ReactLoopAgent    │借用 │ LlmProviderAdapter│
   │ 超时·循环卫生·溢出│←扩展点│  (turn/step 相位机) │←接缝│ FreeModelPool      │
   └──────────────────┘      │  Agent·Session·    │     │ DeploymentLoader   │
              ▲               │  Inbox·ToolRuntime │     └────────┬───────────┘
              │               │  SystemPrompt·     │              │ 桥接
              └───────────────│  Abort/Scope/Dispatch│             ▼
                              └─────────┬──────────┘     ┌──────────────────┐
                                        │ LlmProvider     │ provider/ (旧,保留)│
                                        │ 接缝            │ IAgentProvider     │
                                        └────────────────▶│ ChatProvider       │
                                          (core/Llm.hpp) └──────────────────┘
                                                             │ IHttpTransport
                                                             ▼
                                                        SSE/HTTP
```

**三条贯穿全图的线:**
1. **接缝(`core/Llm.hpp` 的 `LlmProvider`)**:驱动只认这个抽象接口,不认任何具体厂商。`adapter/LlmProviderAdapter` 把旧的 `provider/ChatProvider` 适配进来。免费模型轮换、重试、路由降级因此能作为**策略**挂在扩展点上,而不是焊死在 SSE 分帧代码里。
2. **扩展点(`core/Dispatch.hpp` 的 `Notify/Chain/Serial`)**:所有可插拔能力(策略、提示词段、工具、监听器)都注册到扩展点上。这是 DSH 的 `ctx.on/emit/waterfall/serial` 在 C++ 里的极简替代——没有插件加载器,装配顺序写在 `compose/` 的显式代码里。
3. **日志即状态(`core/Session`)**:会话历史、待办 inbox、审批策略、请求 header 全是日志的投影。崩溃重启能完整恢复。详见 [`core会话日志架构.md`](core会话日志架构.md)。

---

## 1. DSH ↔ avox 对应表

| avox 文件 | DSH 来源 | 作用 |
|----------|----------|------|
| `core/Abort.hpp` | Web `AbortController/AbortSignal` + `packages/core` | 取消令牌(写端/只读端分离) |
| `core/Scope.hpp` | `packages/core/scope` | 分层注册(全局层 + 每作用域层,事件向上流) |
| `core/Dispatch.hpp` | cordis `on/emit/waterfall/serial` | 扩展点派发原语 |
| `core/SessionTypes.hpp` | `packages/core/session/src/types.ts` | 事件词汇表(19 事件 + 信封) |
| `core/Surface.hpp` | `packages/core/session/src/surface.ts` | 模型可见表面投影 |
| `core/Session.hpp` | `packages/core/session/src/index.ts` | append-only 事件日志 |
| `core/SessionCodec.*` | `packages/session/session-persistence` 编解码 | 事件 ↔ JSONL |
| `core/SessionPersistence.*` | `packages/session/session-persistence` 后端 | JSONL 落盘 + 加载 |
| `core/Inbox.hpp` | `packages/core/agent/src/inbox.ts` | 待处理输入队列(日志投影) |
| `core/Llm.hpp` | `packages/llm`(Service Definition) | LLM 接缝 + `BlockAssembler` |
| `core/SystemPrompt.hpp` | `packages/core/system-prompt` | 提示词装配(section/context/variable) |
| `core/ToolTypes.hpp` | `packages/core/tools` 类型部分 | 工具定义/结果/决策词汇 |
| `core/ToolRuntime.hpp` | `packages/core/tools` ToolRuntime | 工具注册表 + 三阶段管线 |
| `core/ToolPresentation.hpp` | `packages/core/tools/presentation.ts` | UI 渲染意图(纯函数) |
| `core/Agent.hpp` | `packages/core/agent/src/runtime-types.ts` | Agent 句柄 + 扩展点词汇 |
| `core/ReactLoopAgent.*` | `packages/core/agent-loop/src/agent.ts` | turn/step 驱动(相位机) |
| `policy/CompactionPolicy.*` | `packages/compaction/compaction-basic` | 上下文压缩(挂 agent/pre-step) |
| `policy/ModelRoutePolicy.*` | `dsh-llm-retry` 做法 | 模型路由 + 请求重试(agent/request, agent/request-error) |
| `policy/ApprovalService.*` | `packages/interaction/user-approval` | 工具审批(tools/pre-execute) |
| `policy/TimeoutPolicy.*` | `packages/guard/timeout-policy` | 工具超时(tools/execute) |
| `policy/RepeatToolPolicy.*` | `packages/guard/repeat-tool-reminder` | 循环卫生(tools/post-execute) |
| `policy/SpillPolicy.*` | `packages/spill/spill-policy` | 结果溢出裁剪(tools/post-execute, prepend) |
| `compose/*` | (取代 `cordis.yml` + profile) | 显式装配 |
| `adapter/LlmProviderAdapter.*` | (新) | 旧 `IAgentProvider` → 新 `LlmProvider` |
| `adapter/FreeModelPool.*` | (新) | `FreeModels` → `ModelPool` |
| `adapter/DeploymentLoader.*` | (新) | 读 `agent.json` → 两份配置 |
| `provider/*` | (旧,现代化重构产物) | OpenAI Chat Completions 协议层(保留作传输) |

> DSH 有 50+ 个 package(sandbox/lsp/mcp/e2b/web/...),avox 只移植了 agent-loop 核心所需的子集。

---

## 2. 原语层:`core/Abort` · `Scope` · `Dispatch`

这三个是所有上层共用的地基,各自独立、无依赖。

### Abort — 取消令牌(`core/Abort.hpp`)
移植 Web 标准 `AbortController/AbortSignal` 分离:`AbortController`(写端)与 `AbortSignal`(只读端)分开,把 signal 交给下游 ≠ 把取消权交出去。

- **三条语义**(`Abort.hpp:12-15`):① 第一个原因胜出(取消是一次性事实);② 取消不抛弃已启动的工作(等它跑到静止是调用方责任);③ 两源可 fuse 成一个(`FusedAbort`,工具超时 = 调用方取消 + 计时器)。
- `AbortSignal::throwIfAborted()` 抛 `AbortError`,驱动在 turn 边界捕获 → 记成 `TurnEndAborted`。所以取消是**控制流**,不是错误。
- 旧 `cancel/CancelToken` 已删除:它只是 transport 层的 socket 中断器(registerSocket),语义比 `AbortSignal` 窄。socket 的 shutdown 现在由 LLM 适配层在 `AbortSignal` 回调里经 `IHttpTransport::SocketHook` 做 —— 取消语义一处定义,传输细节留在传输层。

### Scope — 分层注册(`core/Scope.hpp`)
解决"一个进程多个 agent(主+子+多会话),各自需要自己的工具/提示词/变量/限制,但走同一个注册表"。

- `Scope` 自带 `parent` 指针(`Scope.hpp:40`),按对象身份(地址)比较。`ScopeKey = const Scope*`,全局层用 `nullptr`。
- `NamedEntries<V>`(具名,一层一名,重名抛)、`AnonymousEntries<V>`(匿名,累加)、`ScopedLayers<L>`(全局层 + 每作用域层)。
- **覆盖保持原位置**(对齐 `Map.set` 语义),于是遮蔽不改变装配顺序 → KV cache 前缀稳定。
- 事件派发方向:**沿作用域链向上流,永不向下**。父能观察子组合出的 agent,兄弟互不可见。
- `ScopedRegistration`:RAII 包装,成员析构即撤销注册(`Scope.hpp:69`)。

### Dispatch — 扩展点派发(`core/Dispatch.hpp`)
DSH `ctx.on/emit/waterfall/serial` 的极简替代。三种模式,区分本身就是设计:

| 模式 | 对应 DSH | 语义 | 异常 |
|------|----------|------|------|
| `Notify<P>` | emit | 只观察,不能否决 | 隔离(不影响其余监听器与调用方) |
| `Chain<P,R>` | waterfall | 责任链,每环拿 `next` 委托或短路 | **不隔离**(一环失败 = 决策不完整,传播给调用方) |
| `Serial<P>` | serial | 按序通知,靠副作用表达意见(往 inbox 塞) | 隔离 |

`Chain::on(handler, owner, prepend)`:`prepend` 给"先让下游定稿再统一处理"的策略用(结果溢出裁剪)。

---

## 3. 能力层:`core/SystemPrompt` · `ToolRuntime`

### SystemPrompt — 提示词装配(`core/SystemPrompt.hpp`)
**两条注册通道的区别是缓存代价,不是静态/动态**(`SystemPrompt.hpp:8-18`):

| 通道 | 进哪 | 改它的代价 |
|------|------|-----------|
| `section` | system prompt **前缀** | 从变动 token 起整个 KV cache 复用失效 |
| `context` | 历史**尾部**一条 user 消息 | 前缀一个字节不动,缓存全保留 |

判据是**变化频率**:一年不改的领域知识 → section;每轮可能变的运行时状态 → context。

- order 约定(`SystemPrompt.hpp:42-45`):-100 harness 身份 / 0 部署人格 / 100-199 工具指引 / 200+ 策略声明。
- `PromptAssembly` = `{system, toolsJson, contextSections}`,变量插值 `{{name}}` 严格(引用未注册变量即抛,`SystemPrompt.hpp:126`)。
- `RuntimeContextProjection`(`SystemPrompt.hpp:142`):把易变信息放历史尾部 + **只在与上次不同时才产出**,于是历史 append-only 不堆重复消息。构造时从日志反找最后一条本投影消息,resume 不重复注入。

### ToolRuntime — 工具三阶段管线(`core/ToolRuntime.hpp` + `ToolTypes.hpp` + `ToolPresentation.hpp`)

**三阶段 prepare / dispatch / commit 的四条硬理由**(`ToolRuntime.hpp:8-14`):
1. 持久日志必须与模型看到的顺序一致(tool/call 与 tool/result 配对)。
2. 准入策略要**串行可阻塞**(审批要等人),不能与执行重叠。
3. 工具体纯 I/O、彼此不可见,**可以**重叠。
4. 结果按模型顺序提交,用只沿连续下标前进的游标。

三个扩展点各能改什么(`ToolRuntime.hpp:15-20`):

| 扩展点 | 能改 | 对应 Chain |
|--------|------|-----------|
| `tools/pre-execute` | allow / deny(reason) / ask(reason);**不可改入参** | `Chain<PreToolPayload, PreToolDecision>` |
| `tools/execute` | around-dispatch,**只能改 signal**(超时用) | `Chain<AroundToolPayload, ToolResult>` |
| `tools/post-execute` | accept(替换 content/追加上下文) / block(变失败) | `Chain<PostToolPayload, PostToolDecision>` |

另有 `ToolGuard`(单调守卫,pre-execute 之后、工具体之前):**只有 deny 没有 allow**,注册顺序无法把拒绝翻回许可(`ToolTypes.hpp:211-215`)。

- **失败与拒绝走结果对象,不走异常**(`ToolTypes.hpp:8`):`ToolOutcome` 枚举(Ok/Denied/Timeout/Aborted/AbortedBeforeDispatch/Crashed/Fatal)+ `code`。旧实现靠 `"FAIL:"` 字符串前缀判定(SEH 崩溃、参数错、被审批拒、超时对上层同形)→ 现在由 outcome+code 承载。
- `ExecutionMode::Exclusive`(默认,GUI 自动化必须)/ `Parallel`。调度器**不做跨调用冲突分析**,安全性靠工具老实声明。
- schema **按名字 code-unit 字典序**(`ToolRuntime.hpp:110`),与 locale 无关 → 跨机器顺序一致 → KV cache 前缀稳定。只白名单 name/description/parameters。
- `ToolPresentation.hpp`:渲染意图属工具自己声明(纯函数,只依赖 args+meta),因为 UI 在实时流和日志回放两条路画同一张卡。无法从模型文本重建的结构(grep 命中行号、诊断时间轴)经 `ToolResult::meta` 存进 `tool/result` 事件。

---

## 4. 驱动层:`core/Agent` · `ReactLoopAgent`

### Agent — 句柄,不是驱动器(`core/Agent.hpp`)
Agent **暴露最小驱动原语**(投递输入、取消、等静止),turn/step 推进完全由内部 `ReactLoopAgent` 拥有。刻意没有公开 `step()`(`Agent.hpp:8-10`)。

输入语义只有两个维度(进哪条队列 × 是否唤醒),三个预设(`Agent.hpp:111-129`):

| 方法 | 队列 | 唤醒 | 场景 |
|------|------|------|------|
| `followup(msg)` | NextTurn | ✓ | 普通后续轮次 |
| `steer(msg)` | NextStep | ✓ | 工具跑到一半用户改主意(不打断整轮) |
| `inject(msg)` | NextStep | ✗ | 给下一次 pre-step 排模型可见上下文 |

`withSession(fn)`(`Agent.hpp:70`):Session 非线程安全,多个组件写它(驱动写边界/消息、审批写审批对、压缩写替换、宿主 send 写 inbox splice)都经这里串行化。**fn 内不得再调本 agent 方法(死锁),也不做阻塞长任务**。

**五个 agent 级扩展点**(`AgentExtensionPoints`,`Agent.hpp:215`):

| 扩展点 | 模式 | 作用 |
|--------|------|------|
| `agent/pre-step` | Chain | 是否进入这一步 + 带哪些消息进 |
| `agent/request` | Chain | 替换本次请求的模型配置(**不能改 messages**) |
| `agent/request-error` | Chain | 失败由谁恢复(retry / terminal) |
| `agent/turn-stopping` | Serial | turn 即将关闭(反对靠 steer 塞 inbox,数据决定) |
| `agent/error` · `agent/status` · `agent/created` · `agent/disposed` | Notify | 上报/状态翻转/生命周期 |

### ReactLoopAgent — 相位机驱动(`core/ReactLoopAgent.hpp` + `.cpp`)

**三种相位**(`ReactLoopAgent.hpp:9-12`):
- `Idle` — 无驱动在跑。
- `Maintenance` — 非 turn 维护任务独占(压缩、clear)。**对外仍报 Idle**(否则 UI 为后台压缩闪一下 running)。
- `Running` — 驱动持有 turn/step 与本次活动的取消令牌。

**线程模型(与 DSH 最大差异)**(`ReactLoopAgent.hpp:18-25`):DSH 是单线程 async;avox 是**一个常驻 driver 线程跑 turn/step**,宿主线程经 send/cancel/whenIdle 交互。**一把 mtx 同时保护相位、inbox 与 session**;长任务(模型请求、工具执行)一律锁外跑。由此:会话观察者回调运行在 agent 锁内,**不得回调进 agent 方法**。

**唤醒 latch 规则**(`ReactLoopAgent.hpp:14-17`):活驱动自己 claim 队列(不 latch);维护中/abort 后到达的唤醒才 latch;**disposed 原因永不 latch**(否则析构等一个永远不来的新 turn)。

---

## 5. 驱动主循环(精确版,`ReactLoopAgent.cpp`)

这是整套架构的心脏。读这一节就能定位"某个事件在哪儿产生"。

```
driverMain()                    持锁等 wakeCv → setPhase(Running) → kick()
  └─ kick()                     while (runTurn(controller)) {}
       └─ runTurn():            见下
```

### `runTurn()`(`ReactLoopAgent.cpp:362`)

1. `append(TurnStartData{turn})`。
2. **`turnGuard` (ScopeExit)** → 每条退出路径都 `append(TurnEndData{turn, reason})`(正常/取消/异常皆然,`ReactLoopAgent.cpp:378`)。
3. **step 循环**:
   - `preStep(target, turn, step)`(`:486`):
     - `inbox.claim(target, turn)`(纯删除,不记取消)。
     - `systemPrompt->assemble()`(**锁外**,段求值可能读 skill 目录磁盘)。
     - `runtimeContext->project(contextText)` → 与上次不同才产出一条 user 消息。
     - 跑 `agent/pre-step` 链 → `PreStepReject`(→ TurnEndBlocked)或 `PreStepEnter{messages}`。
   - `append(StepStartData)` + **`stepGuard` (ScopeExit) → `append(StepEndData)`**。
   - 把 claim 到的消息逐条 `append(UserMessageData, SurfaceAppend)`。
   - `runStep(assembly, turn, step)`(`:541`):
     - **重试循环(同一 step 内)**:`buildRequest` → `llm->stream(handler)` → 装配 → 有工具则执行。
     - `handler.onChunk`:**每分片** `append(AssistantChunkData)`(token 级回放保真)+ 喂 `BlockAssembler`。回调里不检查取消(provider 拿了 signal,从回调抛会穿 provider 栈=泄漏源)。
     - 流结束:`finish.kind` 为 Error/Aborted → 跑 `agent/request-error` 链 → retry(continue)或 throw(失败终结);否则 `append(AssistantMessageData, SurfaceAppend{sources=分片seqs})` + usage。
     - MaxTokens → 返回 `TurnEndMaxTokens`(粘性:后续正常步骤不能降级);无 tool calls → `TurnEndCompleted`;有 → `executeToolCalls` → concluded 返回 Completed,否则 nullopt(继续下一步)。
   - **max-tokens 粘性**:`:424-428`。
   - step 结束且 `nextStep` 空 → 跑 `agent/turn-stopping`。
   - **重读 inbox** 决定是否再开一步(`:452`,数据决定,监听器顺序不影响)。
4. 异常:`AbortError` → `TurnEndAborted`(rethrow,kick 捕获,loop 继续);其他 → `TurnEndError` + `reportError`(rethrow)。**驱动边界是错误的终点,失败不杀 loop**(`:343-348`)。
5. 返回是否再开一个 turn(看 `inbox.hasPending()`),换新取消令牌(`:470-479`)。

### `buildRequest()`(`ReactLoopAgent.cpp:629`)

1. `session->requestHeader()`(折叠日志最后一条 header)→ `requestProposal()` 删掉 adapter 物化的默认值(`:70`,跨路由保留用户显式值)。
2. 跑 `agent/request` 链 → 提议配置。
3. `llm->prepareCall()` 解析路由 + 物化默认值 → `PreparedLlmCall`。
4. `append(RequestHeaderData)` —— **只在变化时**:本实例首请求(Initial/Resume)或与上次不同(Change)。一次不动提示词的会话应只有一条 header 事件 = KV cache 前缀稳定判据(`:687-691`)。
5. `append(RequestContextData)` —— 只在路由/容量变化时(`:697-702`)。
6. `session->deriveMessages()` 填进请求(`:704`)。

### `executeToolCalls()`(`ReactLoopAgent.cpp:737`)

每个 call:`append(ToolCallData)` → **三阶段**(锁外)`prepare/dispatch/commit` → `append(ToolResultData, SurfaceAppend{source=callSeq})` + `result.additionalContexts` 进 next-step inbox。取消时为剩余每个 call 补写合成 call/result 对(`appendSkippedToolCall`)——保证每个 tool_call 有对应 result,否则下一次请求在后端侧非法(`:800-807`)。

> 完整 turn 的事件流时序图见 [`core会话日志架构.md` §7](core会话日志架构.md)。

---

## 6. 策略层:`policy/`

每个策略是一个 `install*` 函数,返回撤销器,装配时由 `AgentHost::installConfiguredPolicies()` 按配置安装。**硬规则:策略里不允许硬编码可调参数**(`PolicySupport.hpp:9`),全是 Config 字段从 `agent.json` 可改。

| 策略 | 挂在 | 防的故障 | 关键取舍 |
|------|------|----------|----------|
| `CompactionPolicy` | `agent/pre-step` | 上下文爆炸 | **不删历史**,写 surface replace 节点;切点保证工具配对平衡;用上次真实 promptTokens 判占用 |
| `ModelRoutePolicy` | `agent/request` + `agent/request-error` | 单点故障/限流 | auto 关键字触发模型池轮换;重试**同 step 内**;只对 RATE_LIMITED/UPSTREAM_ERROR 换模型(连接失败不换) |
| `ApprovalService` | `tools/pre-execute`(+ context) | 安全空洞(run_code/CmdTool 零审批) | **没有"永久允许"**;只有 allowed-once 继续;无应答方 fail closed;审批对必须被 turn 包住 |
| `TimeoutPolicy` | `tools/execute`(around) | 合作式工具挂死 | **不抛弃已启动工具体**;用自己的计时器判定(否则用户取消被误报超时);结束恢复上游 signal |
| `RepeatToolPolicy` | `tools/post-execute` | 模型同参反复敲工具打转 | 参数深度 key 排序比较;计数放 post(被拒调用也计数);**只观察补充从不否决**;用户插话后清链 |
| `SpillPolicy` | `tools/post-execute`(**prepend**) | 工具结果无界回灌(漏了最该截的 req.messages 全文) | 先 next() 让下游定稿再裁剪;notice 字节先预扣;落盘失败保原文+warn,绝不变成功为失败 |

`PolicySupport.hpp` 共享工具:`scopeOfExecution`、`flattenPlainText`(只处理纯文本结果)、`combineDisposers`(逆序撤销)、`makePluginMessage`。

> `ModelPool`(`ModelRoutePolicy.hpp:29`)是抽象接口(池来源与策略无关);`FreeModelPool`(adapter/)把现有 `FreeModels` 包进来。

---

## 7. 装配层:`compose/`

DSH 用 `cordis.yml` + profile + 插件加载器 + 类型图生成器让用户不改代码换组合;C++ 的折衷是**显式装配函数 + 配置开关**(`AgentHost.hpp:5-9`)。

### AgentConfig(`compose/AgentConfig.hpp`)
所有可调参数从 `agent.json` 来,取代散在代码里的字面量(kBudgetSec/kMaxShow=4096/truncateChars=100/...)。**配错响亮失败,不做静默回退**(`AgentConfig.hpp:9-11`)。`fromJson`/`fromFile`。

### AgentHost(`compose/AgentHost.hpp`)
持有的东西(声明序 = 构造序 = 依赖序,销毁逆序,`AgentHost.hpp:10-20`):
`ToolRuntime` → `SystemPrompt` → `AgentExtensionPoints` → `ApprovalService` → 策略撤销器 → `ReactLoopAgent`(openAgent 创建)→ `SessionWriter`。

- **销毁顺序反过来很关键**:agent 先停(驱动 join、工具跑完),再拆策略与注册表——否则跑着的工具访问已析构的注册表。
- 装配期:`setLlmProvider`/`setModelPool`/`setApprovalAnswerer`/`defineTool`/`addPromptSection`/`setRuntimeContext`/`installConfiguredPolicies`。
- `openAgent(sessionId)`:空=新建(时间戳命名);非空且 track 文件存在=resume(恢复历史+inbox+给崩溃遗留 turn 补 interrupted)。单会话宿主。

### DiagnosticAgent(`compose/DiagnosticAgent.hpp`)
**装配的唯一定义处**,两个消费者共用:`AgentShell`(交互式)、`AgentExport.cpp`(跨语言导出 `createAgentHost`)。之前两边各写一遍,导出层漏了 `setLlmProvider`(`DiagnosticAgent.hpp:10-12`)。

**顺序依赖**(`DiagnosticAgent.hpp:14-18`):
1. LlmProvider 必须在 `installConfiguredPolicies` 前(压缩策略要发摘要请求)。
2. skill 注册必须在工具注册前(execute 的 schema 取决于 skill 集合)。
3. 工具注册必须在 openAgent 前(首个请求的 tool schema 在那时装配)。

`ComposedAgent`(RAII):`AgentDeployment` + `FreeModelPool`(仅 openrouter+auto) + `LlmProviderAdapter` + `AgentHost` + 撤销器。`reset()` 按依赖逆序拆。

---

## 8. 适配层 `adapter/` + 旧 `provider/`

**过渡态**:现在有两套 LLM 抽象并存。

| 层 | 角色 | 状态 |
|----|------|------|
| `core/Llm.hpp` (`LlmProvider`) | 新 DSH 接缝(语义:`LlmRequest`/`LlmFinish`/`PreparedLlmCall`) | **活路径** |
| `adapter/LlmProviderAdapter` | 把旧 `IAgentProvider`+`IHttpTransport` 适配成 `LlmProvider` | 桥接 |
| `provider/` (`IAgentProvider`/`ChatProvider`/`ProviderFactory`) | 旧 OpenAI Chat Completions 协议层(ProviderRequest/Message/SseEvent) | 保留作传输 |

`LlmProviderAdapter`(`LlmProviderAdapter.hpp`)复用旧代码一个没浪费(ChatProvider 的 payload/SSE 解析、HttplibTransport),只新增三件事(`:9-16`):① `Message`→`ProviderMessage` 转换含 tool_calls wire 生成;② `SseEvent`→`ContentBlock` + 维护 index→id 映射;③ 失败归一化成语义码(RATE_LIMITED/UPSTREAM_ERROR/CONNECTION_FAILED/REASONING_TIMEOUT)供 `ModelRoutePolicy` 判重试。

`DeploymentLoader`(`adapter/DeploymentLoader.hpp`):读现有 `agent.json` 多配置格式,装出 `AgentDeployment`(= `AgentConfig` + `LlmProviderAdapter::Config`)。新架构策略参数作为顶层可选节点追加,与现有配置共存。是不是模型配置按"值是对象且含 url"判定,不维护保留键黑名单。

`FreeModelPool`(`adapter/FreeModelPool.hpp`):把 `FreeModels`(纯逻辑,OpenRouter 免费模型过滤/排序/冷却/轮换)包成 `ModelPool`。原由 AgentClient 喂的 HTTP 现在归本类。

旧顶层源文件已删除:`AgentClient.{hpp,cpp}`、`AgentContext.{hpp,cpp}`、`TrackRecorder.{hpp,cpp}`、`AgentHelper.hpp`、`tools/ToolBase.*`、`tools/ToolRegistry.*`、`tools/ToolResult.hpp`、`skills/CompressRegistry.*`、`adapter/LegacyToolAdapter.*`、`src/avox/AvoxAgent.h`。它们的职责分别落到 `core/`(会话与循环)、`policy/`(行为增强)、`compose/`(装配)、`core/ToolRuntime`(注册表)。历史与工具轨迹现在都在会话日志里,不再有只读旁路副本。

---

## 9. 一次请求的端到端数据流

```
用户 send(msg)/followup(msg)
  └─ AgentHost.agent (ReactLoopAgent)
       inbox.append(NextTurn, msg)           [记 agent/inbox/spliced]
       wakeDriver
  └─ driverMain → runTurn
       append(turn/start)
       preStep:
         inbox.claim                         [取走本 step 消息]
         systemPrompt.assemble               [section 拼前缀 + context 投影到尾部]
         agent/pre-step 链                   [压缩策略在此判定要不要先压缩]
       append(step/start)
       append(user/message ×N)               [SurfaceAppend]
       runStep → buildRequest:
         requestHeader fold → agent/request 链 → prepareCall
         append(request/header, 仅变化时)
         deriveMessages()                    [投影 surface → Message[]]
       llm->stream:
         onChunk → append(assistant/chunk) ×N + BlockAssembler
         (adapter: Message→ProviderMessage→ChatProvider.buildPayload→SSE→SseEvent→ContentBlock)
       append(assistant/message, SurfaceAppend)  [+usage]
       若有 tool calls → executeToolCalls:
         append(tool/call)
         prepare[审批/守卫] → dispatch[工具体+超时] → commit[溢出裁剪/循环卫生]
         append(tool/result, SurfaceAppend)  [+meta]
         additionalContexts → inbox(NextStep)
       append(step/end)
       (无工具/工具声明结束 → turn 结束;否则下一步)
       agent/turn-stopping 链
       重读 inbox
       append(turn/end, reason)
  └─ SessionWriter (observer) 把每条事件写 JSONL (chunk 不立即 flush)
```

每一步"做什么 → 调谁 → 记什么事件"的逐项对照,见上文 §5 + [`core会话日志架构.md` §3-§4](core会话日志架构.md)。

---

## 10. 定位口诀

- **驱动循环在哪?** → `ReactLoopAgent::driverMain/kick/runTurn/runStep`(`core/ReactLoopAgent.cpp:318/342/362/541`)。
- **某事件在哪 append?** → §5(grep `sessionOwned->append` 在 ReactLoopAgent.cpp)。
- **想插一个策略/hook?** → 注册到对应扩展点(`core/Agent.hpp:215` 的 5 个 + `core/ToolRuntime.hpp:137` 的 4 个)。
- **免费模型轮换/重试在哪?** → `policy/ModelRoutePolicy` + `adapter/FreeModelPool`(不再是 SSE 分帧代码里)。
- **审批/超时/溢出/循环卫生?** → `policy/` 对应文件(§6 表)。
- **会话历史/inbox/压缩怎么持久?** → [`core会话日志架构.md`](core会话日志架构.md)。
- **怎么把 agent.json 变成活 agent?** → `compose/DiagnosticAgent::composeDiagnosticAgent` → `DeploymentLoader` → `AgentHost`。
- **新 LlmProvider 接缝 vs 旧 ChatProvider?** → `core/Llm.hpp` + `adapter/LlmProviderAdapter.hpp`(§8)。
- **新增一个工具?** → `ToolRuntime::define`(`compose/AgentHost::defineTool` 或作用域层),声明 `timeoutMs`/`executionMode`/`presentCall`/`presentResult`。
