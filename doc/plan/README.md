# 计划与设计文档

> 状态: 有效 · 上次核对: 2026-09-18 · 权威源: -


avox 各模块的计划、设计与调研文档索引。已完成的方案随功能落地, 对应实现文档见 [INDEX.md](../INDEX.md)。

## backlog/ — panvox backlog 施工计划集 (A-1 ~ A-19)

双仓清单([panvox/docs/backlog.md](../../../panvox/docs/backlog.md))中 avox 项的逐条施工方案:
代码落点/任务拆解/验收判据/依赖风险。入口 [backlog/README.md](backlog/README.md)。

## current.md — 当前在办

当前迭代的任务清单与进度。

## ai/ — AI 与画质增强

| 文档 | 说明 |
|------|------|
| [Anime4K超分播放设计](ai/Anime4K超分播放设计.md) | Anime4K 超分接入播放管线 |
| [yolo26图像检测](ai/yolo26图像检测.md) | YOLO26 检测设计 |
| [实时增强方案调研](ai/实时增强方案调研.md) | 实时画质增强调研 |
| [实时重打光方案计划](ai/实时重打光方案计划.md) | 实时重打光 |
| [监控画质增强方案调研](ai/监控画质增强方案调研.md) | 监控场景画质增强 |
| [离线超分转码方案](ai/离线超分转码方案.md) | Real-ESRGAN 离线解码→增强→编码出片 (TranscodeRecorder × panvox 画质增强挂机) |

## avatar/ — 数字人

| 文档 | 说明 |
|------|------|
| [视频驱动Avatar设计](avatar/视频驱动Avatar设计.md) | 视频驱动数字人 |
| [Avatar消费端改进计划](avatar/Avatar消费端改进计划.md) | 消费端渲染改进 |

## web/ — Web 渲染

| 文档 | 说明 |
|------|------|
| [OffscreenCanvas自适应分辨率设计](web/OffscreenCanvas自适应分辨率设计.md) | OffscreenCanvas 自适应分辨率 |
| [网页Canvas多路渲染优化方案](web/网页Canvas多路渲染优化方案.md) | Canvas 多路渲染优化 |

## gpu/ — GPU 基础

| 文档 | 说明 |
|------|------|
| [VkDevice-VkDevice交互](gpu/VkDevice-VkDevice交互.md) | Vulkan 设备间纹理交互 |
| [颜色空间矩阵统一设计](gpu/颜色空间矩阵统一设计.md) | 颜色空间/矩阵统一 |
| [HDR管线改造计划](gpu/HDR管线改造计划.md) | HDR/10bit 端到端现状调查与分期改造 |
| [420P奇数色度行高绿条-会话交接](gpu/420P奇数色度行高绿条-会话交接.md) | 720x406 底部绿条根因与改法交接 |

## player/ — 播放器与架构

| 文档 | 说明 |
|------|------|
| [动态加载组件设计](player/动态加载组件设计.md) | 动态插件加载 |
| [多播放器日志归属设计](player/多播放器日志归属设计.md) | 多实例日志归属 |
| [几何层设计](player/几何层设计.md) | 几何层 |
| [输入控制设计](player/输入控制设计.md) | 输入控制 |
| [音频外部读取设计](player/音频外部读取设计.md) | 音频外部读取 |
| [TranscodeRecorder设计](player/TranscodeRecorder设计.md) | 转码录制 |
| [ASS字幕渲染计划](player/ASS字幕渲染计划.md) | ASS/SSA(含 PGS 位图)字幕 avox_ass 磁力模块方案 |
| [字幕样式设计](player/字幕样式设计.md) | 字幕文本样式(字体/颜色/对齐/位置)与三层通用变换(scale/offset/opacity)公开 API |
| [open出图时延与渲染输出事件化](player/open出图时延与渲染输出事件化.md) | onRender 携带 SurfaceRenderEvent(世代/尺寸/互操作句柄), open→出图全链路去轮询 |

## virtualproduction/ — 虚拟制片

| 文档 | 说明 |
|------|------|
| 规划占位 (aoce 标定移植方案尚未落稿, doc/plan/virtualproduction/ 待建) | aoce 标定模块向 avox_calib 的移植整合 |

## 其他

| 文档 | 说明 |
|------|------|
| [鸿蒙](鸿蒙.md) | 鸿蒙平台支持调研 |
