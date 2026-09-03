# Avatar 消费端改进计划（参考 AIRI）

> 衔接 [视频驱动Avatar设计.md](视频驱动Avatar设计.md) 的 Phase 3/4：感知管线（Phase 1/2 帧管线 + ONNX 推理）已交付，
> 本计划把"消费端渲染"补齐。参考项目 AIRI（../airi，Web/TS）的消费端完整度约 85%，逐件移植其架构与参数；
> 感知管线本身 avox 领先（AIRI 的 mediapipe 驱动未开 blendshape 头、仅 devtools 实验页），不作参考。

## 0. 现状与差距

| | 现状 | 目标 |
|---|---|---|
| 表情标准 | ARKit52，但音频/视频两条路径顺序不同，消费端两处硬编码索引 | 统一 canonical 顺序 + 按名驱动 |
| 驱动合成 | 单源直驱（视频 52 路或音频 52 路，二选一） | 多源合成链：基础层 + 情绪叠加 + 眨眼/视线，ownerId 仲裁 |
| 待机 | 静置僵硬 | 自动眨眼 + 视线游移 + 程序化 idle 动作 |
| 情绪通道 | `[EMO:x]` 标记 → 2D 嘴形/简单联动 | 情绪枚举 + intensity → ARKit52 预设叠加，带缓动与自动回落 |
| 渲染形态 | GLB（ARKit 名 blendshape）+ agent 场景 2D 嘴贴图 | VRM 1.0/0.x 全支持 + 3D avatar 统一走合成链 |
| 身体 | retarget.gd 方向对齐（已通骨架路径） | 补 pole/翻转拒绝 |

**总原则**
1. 消费端逻辑全部放 Godot 侧 GDScript（迭代快，与现有 `tools/src/avatar/main.gd` 风格一致）；SDK C++ 只动 M0 的顺序归一。
2. 里程碑独立可验收，M0 → M1 → M2 → M3 → M4 顺序依赖，M0/M1 是其余的地基。
3. 明确不做：AIRI Web 渲染端桥接（WebSocket blendshape 流，对端工作量大且收益存疑）；MAGIC 在线生成（先用离线 bake 达成 90% 效果）。

---

## M0 — ARKit52 顺序归一 + 命名表收口（地基，~1 天）

**问题**：音频路径（wav2arkit，jawOpen=24）与视频路径（mediapipe，index0=`_neutral` 占位，jawOpen=25）顺序不同；
消费端各自硬编码——`tools/src/avatar/main.gd:17` 的 `ARKIT52_CSV`（视频表）与 `tools/src/agent/main.gd:557` 的裸索引 24/33（音频表）。

**改动**
- C++：在 `src/avox/AvoxAvatar.h` 定义 canonical 顺序（采纳 mediapipe 表，含 `_neutral` 占位，共 52）：
  名表常量 + `getArkit52Names()` 导出；`Wav2ArkitFace` 输出处做一次性索引重排到 canonical（纯查表）。
- Godot：新建 `tools/src/avatar/arkit52.gd` 共享名表（消费端唯一名表来源），avatar / agent 场景全部按名查索引，删除两处硬编码。

**验收**：同一 avatar GLB 下音频/视频两路驱动嘴形一致；代码内无 ARKit 裸索引（grep `blendshape\[2[0-9]\]` 零命中）。

---

## M1 — AvatarDriver 驱动合成链 + 眨眼/视线（核心，2-3 天）

**目标**：多驱动源按"通道 + 优先级"合成为每帧 52 维输出，替换"单源直驱"。
**AIRI 参考**：Live2D 每帧插件链的相位与覆盖顺序（`stage-ui-live2d/src/composables/live2d/motion-manager.ts:253-595`）；
驱动器 ownerId 抢占模型（`stage-ui-live2d/src/stores/motion-control.ts:236` 的 `setPose(ownerId)` + `claim/release-exclusive`）。

**新文件** `platform/godot/tools/src/avatar/avatar_driver.gd`（RefCounted 纯逻辑，可单测）
- `register_source(owner_id, channel, priority)` / `set_bs(owner_id, channel, arr52)` / `release(owner_id)`
- 通道与合成顺序：
  1. `bs_base`：音频或视频 52 路基础值（同一时刻只认一个 owner，后到 claim）
  2. `emotion`：情绪预设叠加（M2 接入，加法 + 权重）
  3. `blink`：Multiply 叠加在 `eyeBlinkLeft/Right` 上
  4. `gaze`：覆盖 `eyeLook*` 8 路
- `compose() -> PackedFloat32Array(52)`；消费端沿用现有 `_drv` 名表映射写 `set_blend_shape_value`。

**首批移植两件套**
- 自动眨眼：闭眼 75ms / 睁眼 150-300ms / 间隔 3-8s 随机（AIRI AutoEyeBlink，`motion-manager.ts:290-481`）
- 视线游移：`eyeLook*` 微动 saccade + 头部小幅跟随，可选鼠标/镜头注视（AIRI `useLive2DIdleEyeFocus` / `useIdleEyeSaccades`）

**接入**：avatar 场景（视频路）与 agent 场景（音频路）统一走 AvatarDriver。

**验收**：说话时眨眼不打断口型；两路 base 源切换无跳变（claim 交接 500ms handoff，AIRI `motion-manager.ts:551-556`）；静置 1 分钟有生命感。

---

## M2 — 离散情绪应用层 + 标记协议升级（1-2 天）

**AIRI 参考**：9 值情绪枚举 + `EmotionPayload{name, intensity}`（`stage-ui/src/constants/emotions.ts`、`pipelines-audio/src/llm-streaming-control/src/payloads.ts:6-16`）；
情绪缓动 + 自动回落（`stage-ui-three/src/composables/vrm/expression.ts:96-176` 的 easeInOutCubic + `setEmotionWithResetAfter(3000ms)`）。

**改动**
- `avatar_driver.gd` 内加情绪层：`EMOTION_BSPRESET` 表——9 值枚举（happy/sad/angry/think/surprised/awkward/question/curious/neutral）→ ARKit52 组合（如 happy → mouthSmileL/R + cheekSquintL/R + browOuterUpL/R ×intensity）。
- 情绪队列：入队 → easeInOutCubic 上升 → 保持 3s → 自动回落；同通道后到抢占。
- 标记协议升级：`[EMO:happy]` 扩展为 `[EMO:happy:0.8]`（旧格式兼容），同步更新 `assets/agent/system_prompt_avatar.md`；
  `tools/src/agent/main.gd` 的 `_strip_markers` 解析 intensity 并推给 AvatarDriver 情绪通道。
- agent 场景从 2D 嘴形 scale 升级为 3D avatar 走 AvatarDriver（jawOpen 驱动，复用 M0/M1 成果；RMS 包络回退保留）。

**验收**：agent 对话中情绪有强弱、出现/回落平滑；旧 `[EMO:happy]` 无 intensity 时默认 0.7 不回归。

---

## M3 — VRM 消费端（原设计 Phase 4 脸部部分，3-5 天）

**改动**
- 接入 godot-vrm 插件（V-Sekai，Godot 4.x）：VRM 0.x/1.0 导入、springbone、expressions。当前 addons 只有 `avox_godot`，无 VRM 支持。
- ARKit52 → VRM 映射（`tools/src/avatar/vrm_map.gd`）：
  - VRM 1.0 带 ARKit 自定义表情的模型：52 路全量直驱（按表情名匹配 canonical 名表）；
  - VRM 0.x preset（A/I/U/E/O/Blink/Joy/Angry/Sorrow/Fun…）：近似映射表（jawOpen→A、mouthFunnel→O、eyeBlink→Blink、brow+mouth 组合→Joy/Angry/Sorrow），保底可看。
- 身体：`retarget.gd` 对接 VRM humanoid 骨骼（T-pose/A-pose rest 差异处理），补 pole vector 与翻转拒绝
  （AIRI `model-driver-mediapipe/src/three/apply-pose-to-vrm.ts` 的 `minDotBeforeReject` + pole 处理，DEFAULT_ALPHA=0.35 可直接借用）。

**验收**：加载 VRM 1.0 样例，音频口型 + 眨眼 + 情绪 + 摄像头身体驱动全链路；VRM 0.x 模型口型/眨眼保底可用。

---

## M4 — 待机动作离线 bake（2-3 天，探索性）

**方案**：Python 离线拟合 VAR/AR-HMM 生成 idle 姿态帧序列 → 导出 Godot Animation 资产 → 运行时 crossfade 循环播放。
**AIRI 参考**：`motion-driver-magic`（VAR order 20 / AR-HMM 5 状态，`src/types.ts:38`）；`model-driver-magic-live2d` 的 13 轴 pose 定义（`src/pose.ts:4-25`）与 `skipMouthOpen`（`driver.ts:74-81`，生成动作时口型通道归零避免打架）。

**改动**
- `script/godot/idle_bake.py`：拟合 + 生成 + 导出（.res Animation，含胸腔呼吸微动）。
- 数据源待定：Mixamo idle mocap，或自录（IBody + retarget 输出 13 轴）；先小数据集验证管线。
- 运行时：idle 动画作为 AvatarDriver 的 `idle` 通道（骨骼/位移），与 `bs_base`/`blink`/`gaze` 天然不冲突；说话/情绪期间降低权重。

**验收**：静置形象自然晃动不僵硬；与说话、情绪、眨眼无冲突。

---

## M5 — 体验打磨（可选，视效果）

- MAGIC 在线生成 GDScript 移植（仅当 bake 效果不足）。
- BeatSync 音乐节拍联动（AIRI `useMotionUpdatePluginBeatSync`）——与播放器场景打通。
- Live2D/Spine/MMD 等其他渲染后端：**不做**（avox 消费端在 Godot，3D/VRM 路线已覆盖需求）。

## 依赖与节奏

```
M0 (1d) ──> M1 (2-3d) ──> M2 (1-2d) ──> M3 (3-5d) ──> M4 (2-3d) ──> M5 (?)
```

M0-M2 只动 Godot + 一处 C++ 索引重排，不碰播放器/SDK 主线，风险隔离。

## AIRI 参考文件索引

| 主题 | 文件 |
|---|---|
| Live2D 每帧插件链（合成顺序权威） | `packages/stage-ui-live2d/src/composables/live2d/motion-manager.ts` |
| 驱动器抢占/独占 | `packages/stage-ui-live2d/src/stores/motion-control.ts` |
| 眨眼/视线/呼吸实现 | 同 motion-manager.ts（AutoEyeBlink/BreathControl），VRM 侧 `stage-ui-three/src/composables/vrm/animation.ts` |
| 情绪枚举/映射表 | `packages/stage-ui/src/constants/emotions.ts` |
| 情绪缓动+自动回落 | `packages/stage-ui-three/src/composables/vrm/expression.ts` |
| 流式标记协议 | `packages/pipelines-audio/src/llm-streaming-control/`（act/delay/call parser） |
| pose→VRM retargeting（pole/翻转拒绝） | `packages/model-driver-mediapipe/src/three/apply-pose-to-vrm.ts`、`pose-to-vrm.ts` |
| MAGIC 程序化动作 | `packages/motion-driver-magic/`、`packages/model-driver-magic-live2d/` |
| VRM 口型微调参数 | `packages/stage-ui-three/src/composables/vrm/lip-sync.ts`（ATTACK/RELEASE/CAP/静音门限/winner+runner） |

## 落地记录

### 2026-08-31 — M0 完成，M1 完成，M2 协议层完成

**M0（顺序归一）**：查证两条路径顺序真相——HF `myned-ai/wav2arkit_cpu` 模型卡给出权威 52 表：
wav2arkit 原生序 = `browDownLeft(0)..noseSneerRight(50), tongueOut(51)`，与 mediapipe 表的 51 个实名
**完全同序**，差异只是两端书挡（音频路尾多 `tongueOut`，视频路头多 `_neutral` 占位）。
`assets_manifest.json` 旧备注「与 wav2arkit 输出同序」已修正（51 实名同序、插件重排后全表同序）。
- `src/avox/AvoxAvatar.h`：canonical 名表注释 + `getArkit52NamesCsv()` 导出（实现在 `src/avox/avatar/VideoFace.cpp`）
- `plugins/avox_avatar/Wav2ArkitFace.cpp`：输出整体 +1 重排对齐 canonical（`_neutral` 补零，`tongueOut` 丢弃）
- `platform/godot/tools/src/avatar/arkit52.gd`：消费端唯一名表（`names()/index_of()`）
- avatar / agent 两场景 + `samples/functest/facetest.cpp`（jawOpen 24→25 等 5 索引）全部按名表解析，裸索引清零
- 验证：增量构建通过；`facetest -g` 三项 PASS（端到端/实时性 65ms/秒音频/输出合理），canonical 名表导出打印正常

**M1（合成链）**：
- `avatar_driver.gd`：通道合成（`bs_base` 独占 claim + 换源 500ms 交叉淡入；`blink`/`gaze` max 叠加；`emotion` 加法封顶）
- `auto_blink.gd`（75ms 闭 / 150-300ms 睁 / 3-8s 随机）、`idle_gaze.gd`（1.5-4s saccade，120ms 趋近，8 路 eyeLook）
- avatar 场景接线：采集帧进 base 通道，`_process` 60fps compose 后统一写 mesh（首帧到达才应用，防全零清空）

**M2（协议层部分）**：
- `emotion_layer.gd`：9 情绪（对齐 AIRI 枚举，收 `thinking`→`think` 别名）→ ARKit52 预设组合，
  easeInOutCubic 缓入 300ms → 保持 3s → 自动回落 500ms
- 标记协议升级：`[EMO:happy:0.8]`（强度可选，旧格式兼容）；`set_emotion` agent 工具收 `intensity`；
  `system_prompt_avatar.md` 同步 9 值枚举
- 发现：agent 场景已有 `set_emotion`/`play_gesture` 工具通道（`_exec_avatar_tool`），表情工具化已半成品

**M2 剩余（agent 3D 接入）+ avatar_view 组件化**：
- 新增 `avatar_view.gd`（共享 SubViewportContainer 组件）：轨道相机 + 三灯深底环境 + 模型加载
  （GLB/GLTF + blendshape 扫描 + 相机框选）+ AvatarDriver 全链（base/眨眼/视线/情绪，
  内部 60fps compose 写 mesh），API：`load_model / set_base / apply_emotion / reset_blendshapes /
  get_skeleton / model_loaded 信号`。`own_world_3d=true` 与宿主世界隔离。
- avatar 场景重构为复用该组件：删除场景内 viewport/相机/灯光/轨道/扫描/合成代码（约 200 行），
  body retargeting 留在场景侧（经 `view.get_skeleton()/get_avatar_root()` 取骨骼）。
- agent 场景 3D 接入：右半区嵌入 avatar_view（同款默认模型路径），加载成功隐藏 2D 简笔脸
  （加载失败自动回退）；TTS wav2arkit 帧推 base（`set_base("audio", ...)`），停播归零闭嘴，
  face 未就绪时 RMS 包络近似 jawOpen 驱动 3D 口型；`[EMO:name:intensity]` / `set_emotion`
  工具 → `apply_emotion` 全链生效；情绪标签钉面板左上（3D 模式可见）。
- 验证：三个脚本 headless parse OK；avatar 场景运行冒烟 300 帧（模型 4 mesh / 匹配 204 blendshape、
  骨骼可驱动）；agent 场景运行冒烟 300 帧无脚本错误。
- 识图验收（截图目检）：avatar 场景 3D 渲染正确（脸部框选/深底/rim 逆光/状态栏 204 bs）；
  agent 场景 3D 形象接管右半区、「情绪: neutral」标签钉左上、左侧对话 UI 完好，
  **待机视线游移经两帧对比确认生效**（eyeLook 通道驱动双眼侧视）。
  修一个真 bug：`model_loaded` 信号在 `_build_avatar_panel` 前段发出时 `_avatar_2d_col` 尚未创建
  （信号时序竞态），2D 简笔脸未隐藏叠在 3D 脸上——面板构建末尾补 `is_loaded()` 显式判断解决。

**待做**：M5 打磨（可选）。

### 2026-08-31（第四轮）— M4 完成（待机动作离线 bake）

**选型**：不做 AnimationPlayer 轨迹（骨名因模型而异、与 retarget 合成麻烦），改为
「Python 离线 VAR 烘焙曲线 → 运行时轻量采样器直接写骨骼」；**只驱动 Spine/Spine1 两根
retarget 不碰的躯干骨** —— 与视频驱动 retarget (Spine2/Neck/Head/四肢) 按骨段天然分工，
与口型/眨眼/视线 (blendshape 通道) 互不相干。

- `script/godot/idle_bake.py`（numpy）：VAR(24) 最小二乘拟合 → 伴随矩阵谱半径稳定化
  （近单位根 + 高阶过拟合实测谱半径 2.03，>0.97 整体缩放 AR 系数防生成发散）→ 45s@30fps
  循环曲线 → JSON（millirad 整数，16KB，峰值 spine 2.0°/mid 1.4°）。
  数据源两路：默认合成训练集（OU 慢漂移 + 共模晃动 + 呼吸正弦，量级按真人 idle 标定）；
  `--bvh <file>` 接真实 mocap（提取 Spine/Spine1 本地欧拉角，重采样 30fps）。
  产物 `tools/src/avatar/idle_bake.json` 已入库。
- `idle_motion.gd`：加载曲线 → bind 骨架缓存 rest 四元数（role spine/mid；VRM 经
  humanoid 映射 spine/chest·upperChest）→ 每帧采样（帧内插值 + 循环接缝 crossfade）→
  `set_bone_pose_rotation(rest × 欧拉偏移)`；`amplitude`（调试放大）/`weight`（场景压低）可调。
- `avatar_view.gd`：_ready 加载曲线，load_model 后 `_bind_idle()`（缓存 Skeleton3D），
  _process 末尾 tick；`idle()` 暴露实例。GLB 与 VRM 全兼容（VRM 走 humanoid 名）。
- 验证：无头测试三模型 PASS；**识图验收**（amplitude×10 目检）：t=5s 躯干扭转+头偏 vs
  t=10s 回正双臂趋平——两帧姿态明显不同，微动确认活着；avatar/agent 两场景 300 帧零脚本错误。
- 实际运行幅度 1.0（±2° 微动 + 呼吸），自然不僵硬；调参位：`idle_bake.py` 顶部
  LIMITS/BREATH 常量 + `idle.amplitude`。

**遗留**：真实 mocap（BVH/自录）重烘焙更自然的漂移特征；vrm_show/test_vrm 为常驻调试工具保留。

### 2026-08-31（第三轮）— M3 完成（VRM 消费端）

**选型修正**：放弃接入 godot-vrm 插件（EditorImportPlugin 型 addon，运行时加载用户 VRM 不适用，
且引入版本兼容负担），改为**运行时自解析 VRM 扩展 JSON**（GLTFDocument 本就能加载 .vrm 的
mesh/骨骼/morph，缺的只是扩展语义）——自包含 ~250 行 GDScript，零依赖。

- 新增 `vrm_map.gd`：解析 VRMC_vrm(1.0)/VRM(0.x) 表情组 → **按索引绑定**（VRM morph 名因导出器而异，
  索引绑定比名字扫描可靠）归一到 canonical ARKit52。三条路径：
  ① 1.0 custom 带 ARKit 名 / 0.x 自定义组 → 52 全量直驱（kind="arkit"）；
  ② 官方 preset 近似（kind="preset"）：jawOpen→aa、mouthFunnel→oh、mouthPucker→ou、
  eyeBlink→blinkLeft/Right、8 路 eyeLook→4 组 look preset（双眼 max 合成）；
  ③ 非 VRM → 走原 morph 名字扫描。另导出 emotion_binds（happy/angry/sad/surprised/relaxed/
  awkward→preset 情绪保底）与 humanoid（角色名→骨骼名，供 retarget）。
  关键坑：Godot 的 `GLTFState.get_scene_node` **不映射 mesh 节点** → mesh 定位走
  「节点名→MeshInstance3D」（导入后名字 = glTF 节点名）；本版本无 `get_scene_nodes()` 方法；
  0.x bind weight 是 0~100 百分制；`Array.resize` 填 null 需补空数组。
- `avatar_view.gd`：load_model 自动识别 VRM（JSON 扩展嗅探），VRM 走 `_apply_vrm`
  （52 维直写 + look 组合 + 情绪 preset 按 emotion_layer 时间轴权重直写）；新增 `set_orbit_dist`/
  `vrm_info`/`vrm_humanoid`。emotion_layer 增加 `current_weight/current_intensity`。
- `retarget.gd`：加 `_name_map` 重映射（Mixamo 风格 canonical 名 → 实际骨骼名），VRM humanoid
  角色表经 `vrm_humanoid()` 注入 —— VRM 的 J_Bip_* 骨骼名可被身体驱动；帧间突变拒绝/置信度过滤
  原有（对应 AIRI 的 minDot 方案）。
- 测试：`test_vrm/test_vrm.gd`（headless，SceneTree 脚本 root 在 _initialize 未建 → 挂首帧
  process_frame；样本 SeedSan + VRM1_Twist 手动下载，均 VRM 1.0：binds=5 emotions=6
  humanoid=51/54 全过，非 VRM 回归过；样本不入库见 tools/.gitignore）。**识图验收**：
  Seed-san 渲染正常、jawOpen=1 假声源 → 嘴大张（口型链路铁证）、happy vs 中性对比可见、
  眼动侧视确认。
- 回归：avatar/agent 两场景 --quit-after 300 帧零 SCRIPT ERROR。

**遗留**：VRM springbone（头发/裙摆物理）未接（静态），后续可评估移植 godot-vrm 的 springbone
模块；VRM 0.x 样本未实测（解析按规范实现，与 1.0 共享主逻辑）。
