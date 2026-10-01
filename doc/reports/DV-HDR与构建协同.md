# DV/HDR 战役与构建、多端协同纪要

> 状态: 有效 · 上次核对: 2026-09-30 · 权威源: 记忆库(memory/)（原 VT路径补RPU-HDR解析 文档已删, 战役实施史归 git, 本篇为纪要权威）
> 前半: DV/HDR 战役全程; 后半: 构建工具链陷阱、mac/多端协同、自动化值守与流程纪律

## A. DV/HDR 战役 (09-29 → 09-30, P0-P5 全结账)

### A1. 战役目标与架构

硬解腿(DX11VA/VT/MediaCodec/Vulkan)只解码不交 RPU/HDR SEI, FFmpeg 也不给硬解帧
挂 DOVI/HDR side data; 产品口径=硬解为主且必须解对颜色 ⇒ **喂包处旁路扫描自提**。
方案: `MetaExtractor`(SEI 137/144 + NAL62 RPU) + vendor 门面 `dovi_rpu_wrap`
(FFmpeg `ff_dovi_rpu_parse` 不在任何发布导出表 → 拷源码 vendor 进 avox, 不动
FFmpeg 仓)。dvProfile 容器→解码器全链接线。

### A2. 核心发现: dx11cs DV 矩阵转置 bug (e9d9d54)

`Dx11CSVideoRender` DV 分支 `dvApplyCols` 把**列主序** UBO(`dvNl[c][r]=M[r][c]`)
按 `dot(列, v)` 应用 = **Mᵀ×v** → G 通道被色度行吞成 0, 画面**品红 (255,0,255)**。
⚠️ 亮度均值把品红误读成「整体变暗 -100」, 排查半轮错向——**先逐帧 RGB 采样再下
结论**。修=按行点积。09-29「三腿逐块吻合」没抓到它因为是同链自洽对比(转置两侧自消)。
判据结果: A 场对照 -100.04→**+0.19 PASS**; Windows/mac 硬解腿差分**逐位一致**
(A 场 +0.19/+0.25, B 场 +3.46/+3.46)。

### A3. 验证链与定标

- 探针参考值: RPU 首帧 `l1max=1000.6 l1min=0.0003` = source_max_pq 3079/min_pq 7
  换算档(pqToNits(3079/4095)); 场景切换 249.7/399.7 派发到渲染 UBO peak。
- 双腿 DoviMeta 逐字段对质完全一致(bl=10/cld=23/piv/poly/nl/lof/lin)。
- **批C 阈值单源化** (avox-test `a57e7ee`): THRESH_DELTA 4.0→3.0——四平台腿组合
  实测效果带 3.46~4.05、噪声 ±0.1, 旧 4.0 定标于膝点调整前(+7.03 时代)。
  var 判据(-1.3~-1.7)保持 FAIL=素材判别力上限, 与阈值无关。
- 旧豁免作废: 「VT 硬解不产 DOVI 元数据」「硬解下 DV 链死在解码层」。
- 素材口径: gen_dv_l1gate.py, `code10(n)=64+round(940·pq(n))` 有限量程。

### A4. 三平台接入结论 (零引擎改动)

Windows Dx11VDecoder / mac IOSVDecoder / Android AndVDecoder 均**只 override
onPreDecoder 不 override decoderImp** → MetaExtractor 自动接入。
Android `libavcodec.so`(ffmpeg9 重编)无 `ff_dovi_rpu_parse` 导出 → vendor 必需。
P5 回归 73 PASS/4 dav 既有挂, 零新增失败。夜测 soak 双段 5h03m 零崩溃。

### A5. 排查链沉淀(硬解 DV 渲染差分)

帧格式认定(P010 GPU 纹理→dx11cs 先转 RGBA→VK 合成; 软解 CPU yuv420P10 直进
VK V5) → UBO dump(状态全对) → 输入纹理 staging 回读(码值干净) → 输出 RT 回读 →
**逐帧 RGB 采样定谳**。⚠️ MSYS 只转换命令行参数里的 /tmp 路径, python 代码内要
cygpath -m; 共享纹理 TYPELESS 建 staging 会静默失败需落具体格式。

## B. FFmpeg 与库仓

- **五平台重编** (`f0ce52b`, 09-25): flv 修复+P0/P1 扩充(decoder 164);
  坑: WSL vulkan 头 1.3.275<9.0 用 khronos CPATH 解 + 树内 config.h 挡树外
  android 构建。
- **dll 与 lib 必须成对更新** (`c08adb4`, 09-25 夜): dll 19:05 重编序号+1 而 lib
  停 09-19 → avox 全序号导入: avsubtitle_free 旧158→新158=TAK 解析器,
  PGS 字幕释放即崩; **cli 不渲字幕故不崩=最大迷惑点**。修=按名字重生成导入库+重链。
  手法: minidump 手解/旧 lib 序号对照。
- **avc_*→avox_* 改名收尾** (09-24): 兜底删净(a2023b3)缺失即 FATAL_ERROR;
  库仓去 LFS 直存 git(b55de360); manifest 90 链接零坏链; ⚠️ 库仓禁
  `git add --renormalize .`。

## C. 构建工具链陷阱(全家族)

| 陷阱 | 症状 | 解 |
|---|---|---|
| build_common.py `-G` 内嵌双引号 (line ~419) | cmake 配置失败 "Ignoring extra path" | 手工 configure 一次绕过; 修复待拍板 |
| VS 源文件级 INCLUDE_DIRECTORIES 是**替换**语义 | `<ctime>` 报 clock_t 不是成员(误判工具集) | 引内部头的文件收进独立目录; 全局 include 会盖真头致 7000+ 错 |
| MSVC `/I` 覆盖 `INCLUDE` 环境变量 | 命令行出现任一 /I, 整条 INCLUDE 被忽略 | 单文件语法检查工具 tools/syntax_check_msvc.py |
| add_sub_path `file(GLOB)` 只收 .cpp/.mm | 新文件进不了构建图(假成功) | 加源文件后 FORCE 重配; 纯 C 单列+LANGUAGE C |
| 公共头改动后 install/include 不刷 | shim 按旧头编 → 虚表错位 0xC0000005 | cmp 内容(勿信 mtime); 重刷 install |
| MSB6001/MSB6003 "Path…PATH" 字典冲突 | shim 构建炸 | 纯 python subprocess 归一化 env |
| Git Bash 双引号反斜杠路径 | `ls "D:\Work\..."` 静默失败 | 一律正斜杠 `D:/Work/...` |
| MSYS 只转换命令行参数路径 | python 代码内 /tmp 不转 | cygpath -m 取真实路径 |

## D. mac / 多端协同

- **环境**: mac 仓在 `/Volumes/pssd/work/github/{avox,avox-test,panvox}`(非
  ~/Work); brew 在 ~/.homebrew 非标位, 非交互 ssh PATH 写 ~/.zshenv; sleep=0;
  build_mac.py 只产 libavox.a, runner 需单独编(目标名 `playtest`, 产物
  playtest.app; macplaytest 是遗留名)。
- **双机纪律**: pull --rebase 禁 force push; git pull 撞 Cannot-rebase 用
  fetch+merge --ff-only 绕; **并行会话抢跑/代收, 先查远端再动手**。
- **网络断点**: clash 面板 UA 反爬(须 clash-verge UA+flag=clashmeta); TUN 假直连
  (/32 route); GitHub 断时用 git bundle+scp+`-c submodule.recurse=false`。
- **WSL**: 已迁 D:\wsl\Ubuntu; 仓在 ~/github; root 拉起被 Wayland 拒须用户拉起;
  fonts-noto-cjk 修中文乱码。
- **Linux 对齐**: panvox Linux 能打开(8c8671e), 终态配置=Vulkan ON 中间态
  (全关编不过 FSR); WSLg Vulkan 硬解 ✅。
- **多机构建/测试互斥**: 回归前查 MSBuild/link 进数与 dll mtime; 夜测(LNK1104)
  与构建抢 install 产物会互伤。

## E. 自动化值守与流程纪律

- **CronList 死循环教训** (09-20): 「停不掉」的自动化立即改走 Bash+人工处置,
  别反复 CronList; 无法经 CronUpdate 停用的请用户在 Automations 页删。
- **夜班体系** (09-19): 两仓夜间任务板+分工; 收账先读夜间协调日志; LNK1104
  构建锁与夜班提交纪律。
- **mpv/vlc 千提交审计** (09-26, automation): 每 30 分钟各扫 20 提交对照风险;
  发现 AudioFrame writeBytes 仅 assert、SRT parseTime 不查返回值不认点号、
  VkDecoder 空壳等护栏缺口。
- **流程纪律**: 修就完全修不留兼容兜底(09-24 口径: 删过渡兜底+入口硬校验+喂坏
  输入验收+旧名清零); 文档一律 doc/ 不建 docs/(0924); 一份事实一个权威源+状态头;
  DVD/蓝光本版不做(0923 拍板, 散装 vob/m2ts 可播); 首发定稿 Apple 双端
  BYOK-only, 繁简转换砍(0920); 提交首行 ≤50 字说清模块+动作。
- **进度口径**: 进度问题只信当日 plan 文档, 勿引旧核账快照。

## F. DV 战役分阶段台账(2026-09-29 → 09-30)

| 阶段 | 内容 | 结果 |
|---|---|---|
| P0 | NAL 类型表补全(H265 NAL 62/63) | ✅ |
| P1 | MetaExtractor + dovi vendor + 接线; 硬解端到端验证 | ✅ (e9d9d54) |
| —— | 收尾中发现并修复 dx11cs dvApplyCols 转置 bug | ✅ 同上 |
| P2 | mac VT 接入核查+构建+装机验证 | ✅ 零引擎改动 |
| P2 尾 | dv-shot 四用例翻硬解 (f95a972) | ✅ 4/4 |
| P3 | Android 核查(3.1/3.2); 3.3 真机待设备 | 部分 |
| 批C | B 场阈值单源化 4.0→3.0 (a57e7ee) | ✅ |
| P5 | 全量回归 73P+4dav 既有 (报告 in avox-test out/scheduled) + adr-0009 §4 追记三 (5ecea3c) | ✅ |

夜测: soak_1 满额 14400s + soak_2 尾段 3414s, 合计 ~5h03m 零崩溃
(frames 513,583 / reopens 1,783), RSS 判稳(暖机后线性 ~+13MB/h, 留长测观察)。

## G. 多端产物与部署速查

| 平台 | 引擎产物 | 部署位 | 注意 |
|---|---|---|---|
| Windows | build/windows/avox/install/AMD64/Release | panvox deploy_runtime 六份全刷+md5 对账 | 构建与夜测互斥 |
| mac | build/macos/avox/install/aarch64/Release | ~/Applications/panvox.app (ditto, 先退在跑实例) | 装机位≠出包位; runner 目标名 playtest |
| Android | build_android.py (NDK 26.1.10909125) | 真机 adb | P3.3 待设备 |
| Linux | build_linux.py | panvox Linux 可打开 | Vulkan ON 中间态 |
| iOS | build_ios.py | — | 随 P2 模式可推 |

## H. 自动化值守条目(历史→现状)

| 自动化 | 任务 | 现状 |
|---|---|---|
| automation-bebcec02 (本会话) | 每30min 读 sess_b415126e 续 VT-RPU 计划 | 在跑, 战役已收口待用户拍板停 |
| automation-49ecd7d9 (监控会话) | 盯 sess_09e3c860 + 安排测试 | 已转空闲守望(新产物才恢复) |
| automation-32e0f0e6 | mpv/vlc 千提交审计 | 55 批自停, 产出护栏缺口清单 |
| automation-6752ca7d | Linux 对齐值守 | R14 零挂账后收口 |
| 夜班体系 (0919) | 两仓夜间任务板 | 板面规则: 终局后空转, 解除=删终局行 |

教训集中一条: **自动化停不掉时改 Bash 人工处置, 勿反复 CronList**;
「有新产物才恢复」的空闲守望是停与不停之间的第三态。

## I. 用户口径与拍板存档(涉及长期行为)

- 修就完全修不留兼容兜底 (09-24)
- 文档一律 doc/, 不建根目录 docs/ (09-24)
- DVD/蓝光本版不做, 口径「下版本支持」(09-23)
- 首发定稿 Apple 双端 + BYOK-only; 繁简转换砍 (09-20)
- 硬解为主、软解兜底、硬解必须解对颜色 (DV 战役动因)
- 能播偶尔抖可接受(外链长片回退基线, 9d3b5d3)
- 提交首行 ≤50 字说清「模块+动作」, 细节进 body

## J. 关联记忆文件指针

dovi-hard-decode-rpu-transpose-fix-0930 · avox-metaextractor-watch-automation-0930 ·
ffmpeg-flv-rebuild-three-platforms-0925 · ffmpeg-dll-ordinal-mismatch-crash-0925 ·
avox-library-model-rename-0924 · mac-build-chain-0924 · mac-dev-machine-setup ·
mac-clash-sub-recovery-0926 · mac-github-broken-bundle-sync-0926 ·
wsl-migration-disk-cleanup-0924 · three-machines-sync-0925 ·
win-mac-sync-buildstamp-0927 · night-watch-ef5c020c-0923 · avox-night-0919-setup ·
mpv-vlc-commits-audit-automation-0926 · linux-parity-watch-automation-0925 ·
fix-completely-no-compat-fallbacks · docs-dir-forbidden-use-doc ·
avox-panvox-alignment-release-plan · dvd-bluray-deferred-next-version ·
gitbash-backslash-path-silent-fail · avox-0919-status-batch ·
lan-webrtc-remote-desktop-assessment-0928(远控评估)
