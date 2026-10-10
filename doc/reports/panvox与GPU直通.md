# panvox 应用层与 GPU 直通专题

> 状态: 有效 · 上次核对: 2026-10-10 · 权威源: 记忆库(memory/)+panvox/avox 仓提交号
> 汇编 panvox 壳层崩溃/部署、GPU 直通撕裂战役、字幕、远控与原生渲染方向

一句话背景: panvox 侧的崩溃与撕裂几乎全部收敛于「**跨线程/跨 API 的同步缺口**」
与「**部署位与构建位不同步**」两类; 本篇按这两类加方向性决策组织。

## 1. GPU 直通撕裂战役 (09-21 → 09-24, 已定谳)

- **终局定性** (09-22, panvox-tear-two-races): 同文件 sample 干净 + panvox 直通
  撕 + CPU 读回干净 → 撕裂在**消费侧两级无同步**: 级 C(ANGLE 合成) 与 级 B
  (blit vs avox 下帧写, 负载型)。此前「内容坏上游」论证被推翻。
- **已实锤修复**: 守卫 seek(0) `9139426` + 镜像 release 门闸 `2883526`/`32018a8`;
  ⚠️ avox 帧闸 `1912c43`/`02616b4` 已于 09-24 彻底废除(对碎块无效且致卡顿)。
- **二分排查法** (09-23): 七个消费侧假设逐一证伪; xproc01 跨进程用户判干净 →
  锁定 avox×ANGLE 同进程并发。最后嫌疑=RM demux 停顿跳包→RV40 缺参考
  (1-2s 自愈≈GOP), 待同刻 dxt+逐帧 hash 对质。
- **受控复现法**: 受控 panvox 实例(PANVOX_AUTOPLAY + AUTOPLAY_RESUME=点位) +
  8 线程 spin.py CPU 负载 40s → 同位同形散块破损; 无负载 60 帧全净。
  负载脚本抓窗用 avox-test capture_window(其 CLI 第二参数是进程名不是 PID)。
- **RMVB 时钟生态**补充: cook 合成 ts 致 HUD 1.09x 快走——「为何只有这片」的答案。

## 2. 原生渲染方向 (09-23 拍板 → 09-24 落地)

- **纹理直通已整条退役，改原生窗口直渲**（Windows accent=6 真透明 + 其他端 PlatformView）。
  起因（慢性碎块 / 起播 1s 花屏 / 跨 API 排序未建立）、平台路线、落地时间线与验收，见
  [纹理直通与原生窗口.md](纹理直通与原生窗口.md)（本篇不复述）。
- Windows 事实: PlatformView=Android/iOS/mac ✅ Windows ❌; 官方多窗口 API
  实验性仅桌面。
- **外部 DS 滤镜桥设计草案** (09-23, 未实施): 仿 PotPlayer(自研+内嵌 FFmpeg+
  DS graph); 只做 transform 桥(NV12 系统内存), 自研 source/sink 夹滤镜 +
  未注册加载 + 白名单; 坑=零拷贝 UAF/seek 冲刷/崩溃域。

## 3. 崩溃两案

- **截帧并发 crash** (09-21, 已查实未修): ① VkCommand::submit 裸提交无 queue 锁
  (截帧 vs 渲染并发 UB); ② screenShot 1s 超时 vs fetchFrame 无界 fence 等待
  构成 UAF。定帧方法: minidump + dbghelp 手解。
- **flush 空指针 + 音轨枚举悬垂** (09-25, 已修未提交后并入): FFmpeg8 flush 无
  internal 守卫 + 命令线程跨线程 flush → 撤同步改 owner 线程消费 + bCtxOpened
  守卫; getAudioTracks 返回悬垂 → getSourceInfoSafe shared_ptr 托管, shim 8 处全换。
  ⚠️ 同文件曾挂并行会话施工, 提交要分摊。

## 4. 字幕与文本

- **GBK 字幕乱码** (09-21): 某 RMVB 改名的 mp4 实无字幕流, panvox autoPick
  兜底挂了同目录无关的 GBK 字幕文件, utf8 allowMalformed → U+FFFD 菱形。
  SDK 有 CharsetConvert 但 Dart 层没走; 修复方案两条已给未动手(改造 autoPick
  或接 CharsetConvert)。

## 5. 部署与启动闸

- **启动闸** (panvox 095c03b): run_panvox.ps1/panvox.cmd 启动前哈希同步引擎 dll
  堵旧版; 完整部署走 deploy_runtime.ps1(六份 avox.dll 全刷); dll 内无版本值
  (运行时拼), 用 mtime+HEAD 标识。
- **装机位≠出包位** (mac): 必须先退在跑实例再 ditto 到 ~/Applications/panvox.app;
  测功能一律测装机位(记忆: mac-vt-seek 案例中 11:26 的 dylib 才含修复)。
- **一键复现**: PANVOX_AUTOPLAY / PANVOX_AUTOPLAY_RESUME=ms 环境变量直落点位;
  PrintWindow 对 GPU 直通窗口=黑屏死路; obj-vs-commit dll 考古法可救「明明修了
  还复现」。
- **SkipSign 部署漏插件路径修复 → 沙盒下 ORT 插件加载失败 → 画质增强假增强**
  (mac, 10/10 定谳): `PANVOX_SKIP_SIGN=1` 流程(Podfile script_phase, 交
  xcodebuild 统一签)把 install_name_tool `-change`(@rpath/libonnxruntime →
  @loader_path)与 codesign 一并跳过 → 插件靠构建机绝对 rpath 兜底; 沙盒构建
  启动期安全作用域书签未恢复, /Volumes/PSSD 不可读 → avox_onnx/avox_sherpa
  dlopen 失败; 数秒后书签恢复, shim 预检自 dlopen 成功放行任务(与引擎模块态
  脱节) → 引擎 ORT 缺席 `fallback plain:quality enhance` 纯转码出「假增强版」
  (秒完成、画质反而更差)。判据 = 启动段 `dll load failed` 只挂依赖
  onnxruntime 的两个插件 + `CpuQEnhancer load model failed`; 裸进程 dlopen
  探针测不出(差异在沙盒+书签时序), vmmap 看进程内 onnxruntime 加载路径。
  修 = deploy_macos_runtime.sh 路径修复与 SkipSign 解耦(未提交) + 插件构建
  BUILD/INSTALL_RPATH `@loader_path`(avox cmake/avox_module.cmake, 未提交) +
  ModuleMgr 失败日志补 dlerror(未提交); 修后启动段 6 插件全 regedit success。

## 6. 远控与周边

- **RustDesk 1.4.9 局域网直连** (09-28, 已装好用): Win 控 Mac 填
  192.168.3.41 + rd-mac-0928 走 21118; 升级后连不上先查 clash TUN 假 IP 污染
  local-ip-addr(已修)。市场结论: 通用远控红海别做。
- **局域网 WebRTC 远控评估** (09-28, 未实施): webrtc_nosym.lib 已含
  desktop_capture/DXGI 采集+光标合成, IRtcPlayer v2 传输面齐(DataChannel/
  sendOnly/ISignalChannel); 缺口三小块=采集桥+输入注入+信令; 局域网免 STUN/TURN。
- **并行会话互踩守则**: 同仓多会话时提交分摊、工作区探针勿动勿提交([vdbg] 事故)、
  代收先查远端、装机会位归属提前约定。

## 相关文档

- panvox 侧权威: panvox 仓 docs/ (ADR-0009 HDR 策略、native-video-view 设计)
- 素材/复现: avox-test `capture_window`、PANVOX_AUTOPLAY 系列

## 7. 撕裂战役时间线(蒸馏)

| 阶段 | 结论/动作 | 状态 |
|---|---|---|
| 09-21 | 「内容坏上游」论证(30ms 延迟读仍撕) | 后被推翻 |
| 09-21 | 截帧并发 crash 定位(queue 锁+fence UAF) | 查实未修 |
| 09-22 | 两竞态定谳: 级 C ANGLE / 级 B blit 负载型 | 消费侧 |
| 09-22 | fence 值合并 + UINT64_MAX 遗留顺带修 | 已落 |
| 09-23 | 二分: xproc01 跨进程排除 → 同进程并发 | 七假设证伪 |
| 09-23 | 守卫 seek(0) + 镜像 release 门闸 | 已落 |
| 09-24 | avox 帧闸(1912c43)废除——无效且致卡顿 | 已废除 |
| 09-23 | 主推方向=Windows 原生窗口直渲(真透明) | 09-24 已落地 |

判别实验配方: dx11windowtest + 8 线程负载; 「1-2s 自愈」= GOP 粒度信号,
指向 demux 跳包缺参考而非渲染突发——症状自愈时长要先换算成 GOP 数再归因。

## 8. 壳层问题「三段对质」模板(蒸馏)

1. **同文件 cli 直播**: 引擎侧定性(正常=壳层嫌疑↑)。
2. **同文件受控 panvox**: PANVOX_AUTOPLAY+RESUME 点位复现。
3. **CPU 读回 vs 屏幕抓帧同刻对比**: 引擎输出干净+屏幕坏=消费链。
外加: dll 哈希对账(run_panvox 闸)+ obj-vs-commit 考古(「明明修了」八成是
部署位旧 dll)。

## 9. 已知未修项

| 项 | 内容 | 风险 |
|---|---|---|
| 截帧并发 crash | VkCommand 裸提交无 queue 锁 + screenShot 超时 vs 无界 fence UAF | 截帧与渲染并发即触发 |
| GBK 字幕 | autoPick 兜底挂无关 srt + allowMalformed 菱形 | 偶发 |
| mpeg4 黑屏 | 软解 yuv420p × 离屏 GPU 直通(嫌疑未除) | 偶发 |
| RM demux 跳包 | RV40 缺参考 1-2s 自愈(≈GOP) | 特定片源 |

## 10. 关联记忆文件指针

panvox-launch-gate · panvox-0921-screenshot-concurrent-crash ·
panvox-gpu-passthrough-tear · panvox-tear-two-races-0922-verdict ·
panvox-tile-corruption-bisect-0923 · panvox-rv40-load-tear-repro ·
native-view-player-plan · panvox-gbk-subtitle-mojibake ·
panvox-crash-flush-sourcinfo-fixed-0925 · panvox-http-seek-buffering ·
panvox-mpeg4-blackscreen · avox-dshow-external-filter-design ·
lan-webrtc-remote-desktop-assessment-0928 · rustdesk-win-mac-lan-0928 ·
review-fixes-0924-parallel-session · dx11sharedtest-open-fail-outputlayer-null
