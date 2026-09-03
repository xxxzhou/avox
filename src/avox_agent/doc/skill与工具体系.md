# skill 与工具体系 (tool / skill / avox_cmd)

> 合并自《工具与链总纲》与《诊断能力架构》。整体架构见 [avox_agent新架构总览.md](avox_agent新架构总览.md),
> 提示词注入的缓存代价权衡见 [agent开发注意事项.md](agent开发注意事项.md)。
>
> **skill 实现对齐 dsh (`@deepseek-ai/dsh-skill`)**: 命名、front-matter 契约、渲染格式、目录与
> 加载工具语义与 dsh 逐项对应。同一份 `SKILL.md` 资产两侧共用, 可互读。

## 1. 核心原则: 一切皆 ToolDefinition

cmd / tool / skill 最终都是 `ToolDefinition`(name / description / parameters / run / 并发声明 / 渲染意图),
由 `registerBuiltinTools` (`tools/BuiltinTools.cpp`) 统一注册进 `core/ToolRuntime`。工具是**值**(工厂产出的
struct)而不是类层次——要声明的就是名字、schema、怎么跑、能不能并发、怎么渲染,没有需要继承的行为。

| 概念 | 是什么 | 怎么变成 ToolDefinition |
|------|--------|------------------------|
| **cmd** | avox_cmd 子命令(play/record/input/ops/…) | `makeCmdTool` (`tools/CmdTool.cpp`) 按 ArgParser 生成 |
| **tool** | 通用工具(read / grep / run_code / skill + dsh 同名基础工具面 write / edit / glob / pwsh / todo_write / read_image) | 各自的 `make*Tool` 工厂 |
| **skill** | 能力单元(诊断 SOP 等纯指令资产) | 由唯一 `skill` 工具 (`tools/SkillTool.cpp`) 按名加载 |

> **cmd 自动成工具**:avox_cmd 本就是为 agent 写的。`ArgParser` 统一参数模型——`parse(argv)` 给 cli、
> `fromJson` 给 agent、`toJsonSchema` 导出 schema,三用同一份参数定义。加一个 cmd 就自动多一个工具。

同一能力 AI 有两条路:**直接调 cmd/tool**(`ops -w 窗口 -t 文字 -a click` 一步到位)或**调 skill**。
对齐 dsh: skill 是**纯指令资产**, 只加载正文, 由模型按指令用 read/grep/run_code 执行, 工具不运行任何东西。
C 导出层另有 `runSkill(name)` 不经 LLM 直接读一条 skill 的渲染正文(同一份 `<skill_content>`)。
**没有 execute 工具, 也不为每条 skill 生成同名工具** —— 加载只有 `skill` 一个入口。

## 2. skill 实体 (`skills/Skill.hpp`)

每条 skill 是 `Skill` 的**实例**(object,不是子类),字段对齐 dsh 的 `SkillDefinition`:

```
Skill (class)
  ├── name                           kebab-case 标识 (isSkillName 校验)
  ├── description                    路由描述 (必填)
  ├── whenToUse                      可选补充路由指引
  ├── invocation{modelInvocable,userInvocable}  调用开关 (默认两侧都可调)
  ├── source / provider              来源桶 "bundled" / 提供方 "filesystem"
  ├── resourceBase{kind,path,url,description}   相对资源基址 (加载自目录 → directory)
  ├── path                           来自磁盘时的 SKILL.md 绝对路径
  ├── content                        front-matter 之后的正文 (Markdown 指令)
  └── renderSkillContent()           渲染成模型可见的 <skill_content> 块 (对齐 dsh)
```

skill 工具与 `runSkill` 导出都返回 `renderSkillContent()` 的同一份正文: `<skill_content name=...>`
含 `<skill_resources>`(资源基址提示)与 `<skill_instructions>`(正文逐字嵌入)。

## 3. 加载: loadSkills (`SkillRegistry.cpp`)

1. 扫 `skills/*/SKILL.md`(根:`AssetLoader::getSystemConfigPath()+"/agent/skills"` 再 `"assets/agent/skills"`);
2. `parseSkillMd` 解 front-matter: 首行 `---` … 行 `---`, `key: value` 切分;
3. 契约(对齐 dsh skill-filesystem): `name`/`description` 必填, `whenToUse` 可选,
   `disable-model-invocation` / `user-invocable` 可选布尔, **其余未知 key 容忍**(不报错不存储);
4. 校验: `isSkillName(name)` 不满足 / 缺 name 或 description → 整条丢弃并记 warn 日志;
5. 组 `Skill`: `source="bundled"`, `provider="filesystem"`, `resourceBase={kind:"directory",path:<目录>}`,
   `path=<SKILL.md 绝对路径>`, `content=正文`;
6. 同名不覆盖(`if (reg.find(c.name)) continue;`);
7. `builtinSkillRegistry()` Meyers 单例,首次访问即 loadSkills(每条装载记 info 日志,便于回归检查)。

**开闭**:加一条 skill = 写一份 `assets/agent/skills/<name>/SKILL.md`,重启即被扫描注册。
不动引擎、装配层、system prompt、压缩策略。人工写 skill 必须填好 name(kebab-case)/description,
否则装载期直接丢弃。

## 4. 注入: 只有 system prompt 目录 (KV cache 决定)

| SkillRegistry 方法 | 去哪 | 为什么 |
|------|------|--------|
| `systemPrompt()` | system prompt 工具指引段(section,前缀) | 能力目录整会话不变,进 KV cache 前缀 |

目录语义对齐 dsh:
- 只列模型可调(`invocation.modelInvocable`)的 skill;
- 每条一行 `` `- `name`: description` ``(description 做文本转义), name 用反引号包裹;
- 附加载指引: 目录只是摘要, 先调 skill 工具传 `name=<skill名>` 加载完整指令再行动。

**删除了 `routingHint` / `compactionHint`**: avox 原「触发词路由」与「压缩取舍指引」是 avox 私有扩展,
dsh 没有这两个概念, 全改后一并移除(触发词匹配本就该交给模型按目录描述判断)。

## 5. 调用链

```
LLM tool_calls (skill{name: analyze-log})
  → core/ToolRuntime (prepare 审批 / dispatch 并发 / commit 按模型顺序)
  → SkillTool → 校验 isSkillName / find / modelInvocable
       ├─ 失败: Error: ... (名不合法 / 未知 / 模型不可调)
       └─ 成功: 返回 skill->renderSkillContent() (模型按正文指令行动)
  → append tool/result 事件 → surface 回灌 role:tool 消息
```

## 6. 能力膨胀: skill retrieval(方向)

cmd / skill 上百时全发 schema 会 token 爆炸 + 选不准。方向是**目录/schema 分离**:system prompt 留全量
能力目录(name + 一句话 desc,轻),tools schema 只发按用户意图检索出的 top-K(重)。核心固定工具
(read/grep/run_code/skill + dsh 同名基础工具面)始终全发,场景型(cmd/skill)按 description 检索。检索器先关键字匹配,
skill 到几百再上 embedding。

## 7. 与现有模块的关系

- **core/ToolRuntime** (`core/ToolRuntime.hpp`): 三阶段工具管线,本体系的注册终点;
- **skills/SkillRegistry** (`skills/SkillRegistry.hpp`): skill 注册与目录;
- **tools/SkillTool** (`tools/SkillTool.cpp`): 唯一 skill 加载工具(对齐 dsh tool-skill);
- **compose/DiagnosticAgent**: 装配期把 skill 目录拼进 system prompt 前缀(skill 注册须在工具注册前);
- **C 导出 runSkill** (`skills/Skill.cpp`): 脱离模型直接读一条 skill 的渲染正文。
