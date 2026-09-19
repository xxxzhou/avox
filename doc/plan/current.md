# 当前计划

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


本文档记录当前正在进行的开发计划, **只列未完成项** (已完成计划随功能落地, 见各专项文档与 git 历史)。
夜间轮进度明细见 [夜间施工进度.md](夜间施工进度.md) (唯一状态源)。

## 2026.09 计划 (M1 收口, panvox 双仓对齐)

backlog 全景与逐项施工方案见 [backlog/README.md](backlog/README.md)。

- [ ] A-1 字幕链收尾: 信号暴露已落地(方案A, `1608887`: SubtitleEncoding 枚举 +
      ISubtitle::getFileEncoding, SWIG 四语言随构建同步); 剩 **ASS 样式/延迟接口**(T3) →
      候选枚举(T5) → PGS e2e(T4, 缺真实样片); ass 插件路径编码自愈未做(见 a01) → [a01](backlog/a01-ass-pgs.md)
- [ ] A-2 秒起播: 埋点+probe 降档已落地(`d7ebd1d`/`ed75f49`: 本地索引容器快档 3-14ms,
      保底字段缺失回退补查); 剩 **T3 seek 精确化** → T4 基线回归用例 → [a02](backlog/a02-fast-start-seek.md)
- [ ] A-3 VP9/WEBM: **三平台硬解已落地**(Win D3D11VA `1c195f5` 本机实测 hw 命中 /
      Android `19e4729` 编译过待真机 / Apple `19e4729` 待 mac 编译+真机; 软解为回退项,
      VDecoderTask 选型失败自动回退); 剩 Android/Apple 真机验证 + W38 原始样片定性(外部) → [a03](backlog/a03-vp9-webm.md)
- [ ] A-6 FFmpeg 9.0.1 收口: Windows 离线回归 33/33 已过; 剩 **Android/iOS/macOS/Linux
      四平台回归 + UE 链路验证**(本机做不了, 待 CI/真机) + T5 回写 panvox; T6 16KB 页对齐(P2) → [a06](backlog/a06-ffmpeg9.md)
- [ ] A-5 avox_remote: T1 契约设计定稿(`44cf620`) + **T2 IOParseDav 已落地**(`29c2cd6`:
      range 读+4MB 预读窗口+seek, dav://davs:// 自动路由, 本机实测 PASS); 剩
      T3 直链失效重试/续播 → T4 目录缓存 → T5 用例; 附带发现 IOParseSmb 有同款
      EOF 后 seek 静止隐患待修(见 a05) → [a05](backlog/a05-remote-vfs.md)

## 2026.09 计划 (离线超分转码)

实时超分上限不足, 走 `createRecorder(true)` 离线解码→增强→编码出片 (只走 Real-ESRGAN,
BSD-3-Clause 可商用), 配合 panvox 媒体库「画质增强」挂机任务 → [ai/离线超分转码方案.md](ai/离线超分转码方案.md)
(P0 管线验证基本收口: 样例、U/V 色差修复产物级复验、离屏空输出不崩、G13 收尾卡死根因已修)

- [ ] P0 零头: ~~G1 源色彩空间透传~~ · ~~G13 排空有界兜底(muxer 层; 录制层 close 重构并行会话在途)~~ ·
      ~~G14 纯 SEI 访问单元丢弃~~ · ~~enhancetest frames=0~~ · ~~G15 口径(增强优先+warn 明示)~~ ·
      ~~FindRealEsrgan.cmake~~ 均已落地(`843b4b2`, 09-19); 剩 **模型权重入库/LFS**(分发方案待定) →
      [ai/离线超分转码方案.md](ai/离线超分转码方案.md)
- [ ] P1 panvox 集成: pvx_enhance shim (镜像 pvx_aisub job 模型, 含 stop_at_ms 试转预览) + Dart 画质增强任务队列 (方案 §5 T6/T8)
