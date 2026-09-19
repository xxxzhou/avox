# 当前计划

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


本文档记录当前正在进行的开发计划, **只列未完成项** (已完成计划随功能落地, 见各专项文档与 git 历史)。
夜间轮进度明细见 [夜间施工进度.md](夜间施工进度.md) (唯一状态源)。

## 2026.09 计划 (M1 收口, panvox 双仓对齐)

backlog 全景与逐项施工方案见 [backlog/README.md](backlog/README.md)。

- [ ] A-1 字幕链收尾: 信号暴露已落地(方案A, `1608887`: SubtitleEncoding 枚举 +
      ISubtitle::getFileEncoding, SWIG 四语言随构建同步) + 延迟接口(`21491dc`) +
      **候选枚举 T5 与 ass 路径编码自愈已落地**(09-19: listSubtitleCandidates +
      .ass/.ssa 编码探测+中文路径自愈, **契约变更: .ass 路径 getFileEncoding 不再恒
      unknown**, 见 a01); 剩 **ASS 样式覆盖接口**(T3) → PGS e2e(T4, 合成配方已给
      avox-test, 素材侧零外部依赖) → [a01](backlog/a01-ass-pgs.md)
- [ ] A-2 秒起播: 埋点+probe 降档已落地(`d7ebd1d`/`ed75f49`: 本地索引容器快档 3-14ms,
      保底字段缺失回退补查); **T3 seek 精确化已判定「不动」**(09-19 post-T2 基线: 首帧
      持平偏快, seek 31→153ms 为双峰噪声且落地恒 0, 见 a02); 剩 T4 基线回归用例 →
      [a02](backlog/a02-fast-start-seek.md)
- [ ] A-3 VP9/WEBM: **三平台硬解已落地**(Win D3D11VA `1c195f5` 本机实测 hw 命中 /
      Android `19e4729` 编译过待真机 / Apple `19e4729` 待 mac 编译+真机; 软解为回退项,
      VDecoderTask 选型失败自动回退); 剩 Android/Apple 真机验证 + W38 原始样片定性(外部) → [a03](backlog/a03-vp9-webm.md)
- [ ] A-6 FFmpeg 9.0.1 收口: Windows 离线回归 33/33 已过; 剩 **Android/iOS/macOS/Linux
      四平台回归 + UE 链路验证**(本机做不了, 待 CI/真机) + T5 回写 panvox; T6 16KB 页对齐(P2) → [a06](backlog/a06-ffmpeg9.md)
- [ ] A-5 avox_remote: T1 契约设计定稿(`44cf620`) + **T2 IOParseDav 已落地**(`29c2cd6`) +
      **T3 断链自愈已落地**(`c6c928d`: refresh 换链续播+断流退避, avox-test 双例 PASS
      恢复 5.6s/11.9s) + IOParseSmb 同款 EOF 隐患已修(`c784372`); 剩
      T4 目录列表缓存 → T5 剩过期令牌回调用例 → [a05](backlog/a05-remote-vfs.md)

## 2026.09 计划 (离线超分转码)

实时超分上限不足, 走 `createRecorder(true)` 离线解码→增强→编码出片 (只走 Real-ESRGAN,
BSD-3-Clause 可商用), 配合 panvox 媒体库「画质增强」挂机任务 → [ai/离线超分转码方案.md](ai/离线超分转码方案.md)
(P0 管线验证基本收口: 样例、U/V 色差修复产物级复验、离屏空输出不崩、G13 收尾卡死根因已修)

- [x] P0 零头: G1/G13/G14/frames=0/G15/FindRealEsrgan.cmake 均已落地(`843b4b2`, 09-19);
      模型分发口径已定(09-19): **不入库/不 LFS/发布不随包, manifest 链接用户自取**,
      口径权威 [assets/models/README.md](../../assets/models/README.md) →
      [ai/离线超分转码方案.md](ai/离线超分转码方案.md)
- [ ] P1 panvox 集成: pvx_enhance shim (镜像 pvx_aisub job 模型, 含 stop_at_ms 试转预览) + Dart 画质增强任务队列 (方案 §5 T6/T8)
