# avox_agent — Agent 对话 Shell

独立可执行 `avox_agent.exe`, 双击进入多轮 AI 对话 shell。自动加载 `assets/config/agent.json` 默认配置 (顶层 `now` 指定, 缺失回退第一个), 支持 `/use` 切换; 对接 OpenAI 兼容的视觉语言模型。

交互逻辑 (`AgentShell`) 折进 `avox.dll`, `avox_agent` 仅一行薄壳 (经 `agentShellRun`), 与 `avox_cli` 架构一致。

## shell 只做渲染

对话循环整体只有两步:投递输入 + 等静止。turn/step 推进、工具执行、历史落盘、压缩、循环卫生全在驱动 (`ReactLoopAgent`) 与策略 (`policy/`) 里。

```
输入 → agent->followup(UserMessage) → agent->whenIdle(-1) → 观察者回调渲染
```

装配走 `composeDiagnosticAgent` —— 与 C 导出层、测试是同一份装配,shell 没有自己的一套。

## 用法

```
avox_agent
=== avox_agent — Agent 对话 Shell ===
当前配置:
  name     : openrouter
  provider : openai
  url      : https://openrouter.ai/api/v1
  model    : auto
  模型池   : 15 个免费模型可用
会话:
  日志     : C:\Users\mfjt5\.dsh\sessions\--D-Work-github-avox--\session-1786945222\session.jsonl
  事件数   : 0
  历史条数 : 0
策略:
  超时     : 开
  循环卫生 : 开
  溢出裁剪 : 开 (32768 字节)
  审批     : 关
  压缩     : 开
  模型路由 : 开
agent> 你好
收到
agent> /quit
```

## 交互命令与快捷键

| 输入 | 说明 |
|------|------|
| `/help` | 命令清单 |
| `/list` `/ls` | 列配置, 输入编号切换 (回车保持) |
| `/use <name>` | 切换配置 (**开新会话**) |
| `/add` | 交互式添加一个新配置并切换 |
| `/resume [id]` | 从会话日志恢复; 不带参数列出可恢复会话 |
| `/status` | 当前配置 + 会话统计 + 生效策略 |
| `/clear` | 开新会话并清屏 |
| `/compact` | 立刻请求一次压缩 (占一次维护相位) |
| `/events [n]` | 打印最近 n 条会话事件 (默认 20) |
| `/raw` | 切换输入模式 (raw 自管行编辑 / cooked 系统行编辑) |
| `/quit` `quit` `exit` | 退出 |
| 其它文本 | 作为提问发送 (多轮, 流式) |

快捷键 (raw 输入模式): `Enter` 提交 · `Shift+Enter` 多行 · `↑/↓` 历史 · `Tab` 补全 · `Ctrl+C` 生成中断 / 空闲退出。

`/clear` 不"清空"任何东西:会话日志是 append-only 的,它只是开一个新会话。旧会话仍可 `/resume` 回来,**含完整工具调用与结果** —— 这是旧 track 文件恢复做不到的。

## 并发结构

```
[输入线程]   持续读 stdin, 整行入 inputQueue; EOF 置标志唤醒主循环
[主循环]     消费队列 → 投递 → whenIdle → 渲染
[驱动线程]   ReactLoopAgent 推进 turn/step, 经会话观察者回调渲染
```

单一 stdin 读者:输入线程独占,主循环只从队列取,避免并发读 conhost 行编辑器冲突。输入线程 detach 不 join —— 主循环退出时它可能仍阻塞在 `ReadConsoleW`。

`eof` 只在**队列排空后**才终止主循环:stdin 是管道或文件时,输入线程会一口气读完全部行再置 eof,先看 eof 会把已读进来的命令整批丢掉。`quitShell` 相反 —— 用户按 Ctrl+C 要求立刻退出,残留行必须作废。

### Ctrl+C 语义

生成中 → `agent->cancel(CancelByUser)`(驱动收敛,`whenIdle` 返回)+ 标记本轮被中断;空闲 → 退出。cooked 模式由 `SetConsoleCtrlHandler` 调,raw 模式由 `readLineRawWin` 经 `setCtrlCInterceptor` 钩子调 (raw 关了 `ENABLE_PROCESSED_INPUT`, 收不到 `CTRL_C_EVENT`)。

POSIX signal handler 只置 atomic 标志 (async-signal-safe), 主循环 `wait_for` 轮询消费。

## 终端层

| 层 | 位置 | 内容 |
|----|------|------|
| **公共** | `src/avox_cmd/CmdHelper.{hpp,cpp}` | VT/raw 开关、line editor (光标/IME/历史/补全/多行)、ANSI 着色、清屏、Ctrl+C 钩子 |
| **agent 特有** | `src/avox_agent/terminal/` | 流式 markdown 渲染、throbber |

着色/历史/补全归公共层,`avox_cli` 同步受益。markdown 渲染是自写轻量状态机(在/出代码块、列表缩进),因为要流式、还要与 throbber 交错。

命令历史持久化到 `logs/avox_agent_history`;会话日志走 DSH (jsonl 落盘, 含完整工具历史, 可 /resume)。

## 配置

见 [../README.md](../README.md#配置--assetsconfigagentjson):模型配置(多份 + `now`)与策略节点(顶层可选)。

**url/apiPath 拼接** — 主机 = `scheme://host:port`, 路径 = `url 中 / 之后的部分` + `apiPath`:
`url=https://api.openai.com/v1` + `apiPath=/chat/completions` → `https://api.openai.com` + `/v1/chat/completions`。

## 构建与冒烟

```bash
python build_windows.py     # cmake -DAVOX_ENABLE_AGENT=ON -DAVOX_ENABLE_CLI=ON
```

产物 `build/.../avox_agent.exe`, 运行需 `assets/config/agent.json`(双击靠 `AssetLoader` 从 exe 同级 `assets/` 或 `%LOCALAPPDATA%/avox/` 加载)。

shell 默认拒绝非交互终端(raw line editor 进不去,补全与历史都失效,那样的交互会让人以为是 bug)。要把一串命令喂进去做冒烟回归,设 `AVOX_AGENT_ALLOW_NONTTY=1` 并重定向 stdin:

```bat
set AVOX_AGENT_ALLOW_NONTTY=1
avox_agent.exe < smoke.txt
```

`smoke.txt` 一行一条命令即可(注意 `/list` 与 `/resume` 会消费下一行当作编号)。

## 待办

- 终端宽度软换行 + 长输出分页 + 主题
- 原生 Anthropic 协议 (`/v1/messages` + `x-api-key` + `content_block_delta`), 按 `provider` 分支
- `/image <path> [prompt]` 多模态
- `/approve` 交互审批 UI (`ApprovalService` 已就位, shell 尚未接应答方)
