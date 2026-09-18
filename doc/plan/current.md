# 当前计划

> 状态: 进行中 · 上次核对: 2026-09-18 · 权威源: -


本文档记录当前正在进行的开发计划, **只列未完成项** (已完成计划随功能落地, 见各专项文档与 git 历史)。
夜间轮进度明细见 [夜间施工进度.md](夜间施工进度.md) (唯一状态源)。

## 2026.09 计划 (M1 收口, panvox 双仓对齐)

backlog 全景与逐项施工方案见 [backlog/README.md](backlog/README.md)。

- [ ] A-1 字幕链收尾: G 组离线 9/9 已绿、编码自愈已接线; 剩 **信号暴露**(方案 A 返枚举+扩字段 vs 方案 B 加回调, 待拍板, 动 ABI 需同步 SWIG 四语言) → ASS 样式/延迟接口 → 候选枚举 → PGS e2e(缺真实样片) → [a01](backlog/a01-ass-pgs.md)
- [ ] A-2 秒起播: 指标埋点已完成(`d7ebd1d`, 实测 open→首帧 189ms / seek→首帧 13ms); 剩 **T2 probe 降档**(必须留 pix_fmt/fps 保底字段, 否则打穿 A-12/A-3) → T3 seek 精确化 → [a02](backlog/a02-fast-start-seek.md)
- [ ] A-3 VP9/WEBM: 已复现——软解播放+seek 正常(首帧 49ms/seek 13ms), **W38 未复现**; 三平台硬解注册表确无 VP9; 剩 **路线拍板**(三平台硬解: Win 1-2天/And 半天/Apple 需真机, vs 固化软解回归用例: 半天; W38 原始样片向提出方索要) → [a03](backlog/a03-vp9-webm.md)
- [ ] A-6 FFmpeg 9.0.1 收口: Windows 离线回归 33/33 已过; 剩 **Android/iOS/macOS/Linux 四平台回归 + UE 链路验证**(本机做不了, 待 CI/真机) + T5 回写 panvox → [a06](backlog/a06-ffmpeg9.md)
- [ ] A-5 avox_remote(建议提前): panvox P-4 刮削硬依赖, M1 末期启动 → [a05](backlog/a05-remote-vfs.md)

## 2026.09 计划 (离线超分转码)

实时超分上限不足, 走 `createRecorder(true)` 离线解码→增强→编码出片 (只走 Real-ESRGAN,
BSD-3-Clause 可商用), 配合 panvox 媒体库「画质增强」挂机任务 → [ai/离线超分转码方案.md](ai/离线超分转码方案.md)
(P0 管线验证基本收口: 样例、U/V 色差修复产物级复验、离屏空输出不崩、G13 收尾卡死根因已修)

- [ ] P0 零头: **G1 源色彩空间透传**(仍未实现, `TranscodeRecorder.cpp:223` 仍硬编码 bt601+full) · G13 超时兜底(收尾加超时, 超时强制 `av_write_trailer` 保 moov) · G14 产物开头纯 SEI 访问单元丢弃(`IOMuxer::naluDropAble` 扩 VCL 判定) · enhancetest frames=0 埋点(推荐 onRenderOut 补 image 分支, 3 行) · G15 增强 auto 与显式 setOutVideo 优先级拍板 · 模型权重入库/LFS(`assets/models/**` 仍在 .gitignore) + `FindRealEsrgan.cmake` 过期路径
- [ ] P1 panvox 集成: pvx_enhance shim (镜像 pvx_aisub job 模型, 含 stop_at_ms 试转预览) + Dart 画质增强任务队列 (方案 §5 T6/T8)
