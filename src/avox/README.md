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
