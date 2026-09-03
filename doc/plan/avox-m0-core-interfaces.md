# [archived] avox M0 设计笔记（导入 avox 前的脚手架）

> 本笔记来自 avox 初始 M0 脚手架（全新重写方案），现已改为以 avox(AVPlay) 代码为底座导入；
> 原始文件见 git 历史（3586d2d 及之前）。设计思路仍可作为后续演进的参考。

# avox core

跨平台媒体框架核心。公共接口在根目录（`Avox*.h`），实现在子目录（`player/ source/ gpu/ ...`）。

| 文件 | 说明 |
|------|------|
| **AvoxDef.h** | 基础宏：命名空间、`AVOX_EXPORT` 导出 |
| **AvoxVersion.h** | 版本号（与 CMake project 版本对齐） |
| **AvoxPlayer.h** | 播放器：`IMediaPlayer`、`PlayerState`、`IMediaPlayerOb` |
| **AvoxSource.h** | 数据源：`IVideoSource` / `IAudioSource`（push 模型） |
| **AvoxGpu.h** | GPU 帧：`GpuFrame`、`GpuBackend`（后端中立） |
| **AvoxPlugin.h** | 插件扩展点：`IPlugin`、`PluginInfo` |
| **AvoxCore.h** | 聚合伞头 |

## 目录规划

| 目录 | 功能 |
|------|------|
| player/ | 播放器编排、轨道、时钟 |
| source/ | 文件/流数据源实现 |
| gpu/ | Vulkan 管线对接（跨 API 互映射，来自 aoce 血统） |
