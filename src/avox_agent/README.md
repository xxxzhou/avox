# avox_agent — AI Agent 模块

对接 OpenAI 兼容的视觉语言模型 (VLM),让 AI 通过**原生工具**(read/grep/execute/run_code/cmd 等)做多媒体诊断与自动化。能力单元是 **skill**(Claude Code 风格:`assets/agent/skills/<name>/SKILL.md` + 可选纯 `.py`)。

架构按 [deepseek-harness](https://github.com/deepseek-ai/deepseek-harness) 的分层重做,类名与结构名尽量对齐以便后期同步:会话日志是唯一真相 (`core/`),行为增强是可插拔策略 (`policy/`),装配只有一处 (`compose/`)。总览见 [doc/avox_agent新架构总览.md](doc/avox_agent新架构总览.md),会话日志细节见 [doc/core会话日志架构.md](doc/core会话日志架构.md)。

## 分层

```
core/     会话与循环骨架 (不含任何部署选择)
policy/   可插拔行为增强 (每条都能单独关掉)
compose/  装配唯一定义处
adapter/  接现有 avox 设施 (agent.json / httplib provider / 免费模型池)
tools/    工具实现 (统一 ToolDefinition 工厂)
skills/   skill 注册中心 + SKILL.md 解析
provider/ LLM 协议层 (payload 构建 / SSE 解析)
```

### core/ — 会话日志是唯一真相

| 文件 | 作用 |
|------|------|
| `SessionTypes` | 事件类型与载荷 (wire 名稳定, 新事件可带 `ignorable`) |
| `Session` / `SessionCodec` | 追加事件 + JSON 往返 (一份 `.jsonl` 同时是 resume 种子、回放 fixture、审计流水) |
| `SessionPersistence` | 落盘与装载 (崩溃遗留的未闭合 turn 补写 `interrupted`) |
| `Surface` | 由事件流投影出模型可见历史 —— **模型可见 ⟺ 已记日志** |
| `Agent` / `ReactLoopAgent` | 驱动线程 + ReAct 循环 (pre-step → 请求 → 工具 → 收敛) |
| `Inbox` | 三条输入通道 (followup / steer / inject) |
| `Dispatch` / `Scope` / `Abort` | 工具调度、生命周期作用域、取消收敛 |
| `ToolRuntime` / `ToolTypes` / `ToolPresentation` | 分层工具注册表 + `ToolDefinition` + UI 渲染意图 |
| `SystemPrompt` | 分段式 system prompt (按 order 拼, KV cache 前缀稳定) |
| `Llm` | 模型请求/响应的语义类型 |

### policy/ — 每条都能单独关掉

| 策略 | 作用 |
|------|------|
| `TimeoutPolicy` | 工具超时 |
| `RepeatToolPolicy` | 重复调用同一工具的循环卫生 |
| `SpillPolicy` | 超大工具结果落盘,历史里只留摘要与路径 |
| `ApprovalService` | 工具审批 (默认关; 开了不给应答方 = fail closed) |
| `CompactionPolicy` | 上下文压缩 (按 context window 比例触发) |
| `ModelRoutePolicy` | 失败重路由 (配合免费模型池) |

### compose/ — 装配唯一定义处

`composeDiagnosticAgent` 是唯一装配处:shell、C 导出层、测试全走它。曾经 shell 与导出层各写一遍,端到端首跑就撞上导出层漏 `setLlmProvider`。

`AgentConfig` 把所有可调参数收成配置字段(超时、溢出阈值、压缩比例、并发上限……),配错**响亮地失败**,不静默回退默认值。

## 核心:**一切皆 skill**

skill = **纯指令资产**(正文是给 LLM 的 SOP,LLM 用 read/grep/run_code 自取)。实现对齐 dsh(`@deepseek-ai/dsh-skill`):kebab-case 命名、front-matter 契约、`<skill_content>` 渲染、目录与 `skill` 加载工具语义与 dsh 逐项对应,同一份 `SKILL.md` 资产两侧共用。

| skill | 形态 | 做什么 |
|-------|------|--------|
| `analyze-log` | 纯指令 | 分析已采集的 avox 播放日志(IO/解码/同步/卡顿根因) |
| `hysp-pc-log` | 纯指令 | 分析 hysp_pc main.log(建设备表 + webcamId 反查请求) |
| `diagnose-play` | 纯指令 | 用 play 工具采集日志 → 按 analyze-log 指引分析 → 重播建议 |
| `avox-python-api` | 纯指令 | avox Python 高层 API 速查(Image/Vision/Player/...); 写自动化/图像/视觉 python 时查 |

- **SKILL.md front-matter**(对齐 dsh skill-filesystem):`name`(kebab-case) / `description` 必填,`whenToUse` 可选,`disable-model-invocation` / `user-invocable` 可选布尔,其余未知 key 容忍(不报错不存储)。由 C++ 自写解析器读(无 YAML)。
- **脚本 asset**:纯 `.py`,优先 `from avox import ...`(高层封装, 见 `avox-python-api` skill; 进阶才回退 `import AvoxWrapper as _pw`) + `def run(input="", **kw)` + `if __name__=="__main__"`,**无** agent 元数据 —— 同一份既能用 run_code 跑,也能外部 `python x.py` 跑。
- **加载只有一个 `skill` 工具**(对齐 dsh):模型先看 system prompt 里的 skill 目录(只列模型可调项,一行 `` `- `name`: description` ``),选中后调 `skill(name=<skill名>)` 加载完整 `<skill_content>` 正文再按指令行动;工具不执行任何操作。**没有 execute 工具,也不为每条 skill 生成同名工具**。
- **无触发词路由**:avox 原 `triggers` / `routingHint` / `compress_hint` / `compactionHint` 是 avox 私有扩展,全量对齐 dsh 后一并删除 —— 路由交给模型按目录 description 判断。

## 工作方式(原生 function calling)

```
followup(text) → Inbox → 驱动线程唤醒
  ↓
pre-step: Surface 投影出模型可见历史 (system 前缀含稳定 skill 目录)
  ↓  request/header 事件落盘 = 这次请求给模型看了什么, 事后可逐字复核
ChatProvider::buildPayload 序列化 wire (system 分段拼接 + tools 定义)
  ↓
模型原生 tool_calls (流式分片按 index 累积) → assistant/chunk 事件
  ↓
Dispatch 按 ToolConcurrency 调度 (默认 Exclusive; read/grep 为 Parallel)
  ↓  policy 链: 审批 → 超时 → 循环卫生 → 溢出裁剪
ToolRuntime::execute → read / grep / run_code / cmd / skill(name=...)
  ↓
tool/result 事件 → Surface 回灌 role:tool
  ↓
模型分析 → assistant/message → turn/end
```

- **skill 工具只加载正文**(对齐 dsh):校验 `name` 合法 → 查注册表 → 返回 `renderSkillContent()` 的 `<skill_content>` 块,模型按正文指令行动,工具不跑任何操作;失败(名非法/未知/模型不可调)返回 `Error: …`。
- **脚本执行走 run_code**:写自动化脚本用 `run_code`(可传 `script=` 跑 `.py` 的 `run(input)`)或 C 导出 `runSkill`;skill 目录与正文只给指令,不直接 spawn。

## Python 执行 (同一份纯 .py, 不绑定版本)

avox 不嵌入 CPython,用 **SubprocessRunner**(spawn 机器 python + 管道收 stdout),编进 avox.dll 核心。好处:① 不绑定 Python 版本(3.10~3.14 都行);② 崩溃隔离;③ 构建不需要 Python Development;④ 部署不带 python314.dll。

- **avox 内执行**:脚本型 skill + `run_code` 工具 + `avox_cli python` 都走 `getPyRunner()` 单例。优先 `AVOX_PYTHON` 环境变量(可含参数如 `py -3.13`),否则 PATH 上 `python`(Win)/`python3`(POSIX)。
- **外部独立运行**:`python skills/<name>/<script>.py <input>` 直接跑。`avox_cli` / `avox_agent` 首次运行调 `ensurePythonPath()`,往每个系统 Python 的 `site-packages` 写 `avox.pth` 指向 `<install>/plugins`(并设 `AVOX_HOME`),故外部 `python` 开箱 `import avox`。(目前 Windows;Linux 需 `export PYTHONPATH=<install>/plugins`。)

> 纯指令 skill **不依赖** Python;只有脚本型 skill 执行时才调 `getPyRunner()`(python 不可用时 graceful 失败 "FAIL: no python found")。

## 入口

| 入口 | 用途 |
|------|------|
| `avox_agent` | Agent 对话 shell (双击进入, 多轮对话, 见 [doc/agent_shell.md](doc/agent_shell.md)) |
| `createAgentHost(configJson)` | C 导出 (`AgentExport.h`),外部语言经 SWIG 驱动 agent |
| `runSkill(skillName, userInput)` | C 导出,脱离 VLM 直接跑 skill (手测/SWIG/外部)。返回 `{"skill","output"}` JSON |

### 跨语言 (`AgentExport.h`)

三条设计决定(理由见该头文件):

1. **事件流用一个 JSON 回调 + 少数专用回调**(onToken/onToolCall/onTurnEnd/…),不给每个事件类型一个虚函数 —— 事件词汇表注定增长,每次增长都破 ABI。
2. **配置用一个 JSON 字符串**,不用一堆 `setXxx` —— 校验在 C++ 一处完成。
3. **提供工具走进程外**(脚本型 skill / 工具服务器子进程),不走 FFI 回调 —— GIL 死锁、崩溃隔离退化、取消到不了静止。审批是唯一例外的跨语言回调。

`shutdown()` 是收敛点:返回后无驱动线程、无在跑的工具、日志已 flush。宿主语言**必须显式调**(Python 侧用 `with`),别指望 GC 顺序。

> core 的类**不加 `AVOX_EXPORT`**:avox 用静态 CRT (`/MT`),跨 DLL 传 `std::string`/`vector` 会堆不匹配崩溃。跨边界只走 `AgentExport.h` 的裸指针 + `const char*`;测试(如 `sessiontest`)直接把 core 源文件编进去。

## 配置 — `assets/config/agent.json`

现有的多配置格式一行不用改:

```json
{
  "openrouter": {"url": "https://openrouter.ai/api/v1", "apiPath": "/chat/completions",
                 "apiKey": "...", "model": "auto", "provider": "openai"},
  "zen":        {"url": "https://opencode.ai/zen/v1", "apiPath": "/chat/completions",
                 "apiKey": "...", "model": "auto", "provider": "openai"},
  "now": "openrouter"
}
```

两个免费模型池来源的区别只在「怎么判免费」与家族权重表:

- **openrouter**: `/models` 带 `pricing`,按 `pricing.prompt == "0"` 过滤;
- **zen** (OpenCode Zen, coding-agent 网关): `/models` 不带 pricing,按命名约定过滤
  (`-free` 后缀, 另有 `big-pickle` 限时免费无后缀)。免费模型全部走 `/chat/completions`,
  与现有 provider 路径吻合。Zen 池拉取失败时建议 `modelRoute.fallbackModel` 填 `big-pickle`。

「是不是一个模型配置」按**值是对象且含 `url` 字段**判定,不维护保留键黑名单 —— 后者每加一个策略节点都要同步,漏一次就把策略当成模型配置。

策略参数作为**顶层可选节点**追加,缺失走默认值:

```json
{
  "persona": "你是 avox 多媒体诊断助手。",
  "maxParallelToolCalls": 1,
  "timeout":     {"enabled": true, "defaultTimeoutMs": 0},
  "repeatGuard": {"enabled": true, "thresholds": [3, 5], "argumentsPreviewChars": 100},
  "spill":       {"enabled": true, "maxInlineBytes": 32768, "spillRoot": "logs/spill",
                  "skipTools": []},
  "approval":    {"enabled": false, "policy": "ask", "requireApproval": []},
  "compaction":  {"enabled": true, "thresholdRatio": 0.8, "keepTailNodes": 3},
  "modelRoute":  {"enabled": true, "autoKeyword": "auto", "maxAttempts": 6,
                  "retryableCodes": [], "fallbackModel": ""}
}
```

`/status` 会打印哪些策略生效。

## 文件结构

```
src/avox_agent/
├── AgentExport.{h,cpp}        # 对外导出层 (SWIG 友好): createAgentHost / runSkill / agentShellRun
├── AgentShell.{hpp,cpp}       # 交互 shell (输入线程 + Ctrl+C 收敛 + throbber + 补全 + markdown)
├── cli/main.cpp               # 一行壳: 调 agentShellRun()
├── core/                      # 会话与循环骨架 (见上表)
├── policy/                    # 可插拔策略 (见上表) + PolicySupport 公共支撑
├── compose/
│   ├── AgentConfig.{hpp,cpp}  # 部署配置解析 (配错响亮失败)
│   ├── AgentHost.{hpp,cpp}    # 宿主: 注册表 + 日志写入器 + 会话生命周期
│   └── DiagnosticAgent.{hpp,cpp}  # composeDiagnosticAgent —— 装配唯一定义处
├── adapter/
│   ├── DeploymentLoader.{hpp,cpp}  # 读 agent.json 多配置格式
│   ├── LlmProviderAdapter.{hpp,cpp}# core/Llm 接口 ← provider/ 实现
│   ├── FreeModelPool.{hpp,cpp}     # 免费模型池 (配合 ModelRoutePolicy)
│   └── FreeModels.{hpp,cpp}        # 免费模型清单拉取
├── provider/                  # LLM 协议层 (payload 构建 / SSE 解析)
│   ├── IAgentProvider.hpp     # 接口 + 语义结构 (ProviderRequest / Message / SseEvent)
│   ├── ChatProvider.{hpp,cpp} # OpenAI 兼容 (DeepSeek/Ollama/千帆/openrouter)
│   └── ProviderFactory.{hpp,cpp}
├── transport/                 # HTTP 传输 seam (httplib 抽象, SSE 主链路可 mock)
├── skills/
│   ├── Skill.{hpp,cpp}        # skill 实体 + executeAndRender + runSkill 导出实现
│   └── SkillRegistry.{hpp,cpp}# 注册中心 + loadSkills (扫 skills/*/SKILL.md)
├── tools/                     # 工具实现, 统一 ToolDefinition 工厂
│   ├── BuiltinTools.{hpp,cpp} # 一处注册全部内建工具 + skill 包装
│   ├── ToolArgs.hpp           # 模型入参解析 (工具边界必须校验)
│   ├── SkillTool / RunCodeTool / ReadTool / GrepTool   (avox_cli 命令不再包成工具,
│   │                                                     用法由 assets/agent/skills/avox-cli 承载)
├── terminal/MarkdownRenderer  # 终端 markdown 渲染
└── doc/                       # 设计文档

assets/agent/skills/<name>/SKILL.md (+ 可选 <name>.py)   # skill 资产
```

> Python 执行桥在核心层,不在 avox_agent:`src/avox/AvoxBase.h` (IPyRunner) + `src/avox/module/SubprocessRunner.hpp`。

## 测试

`samples/functest/`:

| 目标 | 覆盖 | 需要 key |
|------|------|----------|
| `sessiontest` | core 会话日志不变式(seq 连续性、投影规则、压缩、重写限制、resume 保真、崩溃补写……) | 否 |
| `agenttest` | 端到端: 经 C 导出层跑一轮真实模型往返 | 是 |

```sh
cd build/windows/avox/install/AMD64/Release
./sessiontest.exe        # 纯不变式, 秒级
./agenttest.exe "问题"   # 走 agent.json 的 now 配置
```

shell 本身需交互式终端;要把一串命令喂进去做冒烟,设 `AVOX_AGENT_ALLOW_NONTTY=1` 后重定向 stdin。

## 构建

```bash
cmake .. -DAVOX_ENABLE_AGENT=ON   # 默认 ON; 需 WebRTC(BoringSSL) 或 OpenSSL 3.0+
# Python 执行: SubprocessRunner 编进 avox.dll, 不需额外 option; 机器 PATH 上需有 python 3.10+
```

> CMake 的源文件 glob 在配置期展开:**增删源文件后必须让 CMake 重配**(改 `src/CMakeLists.txt` 或在 build 目录跑 `cmake .`),否则新文件不参与编译、删掉的文件仍在工程里。
