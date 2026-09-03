# avox Godot 播放器 —— 最小示例

在 Godot 项目里跑通 avox 视频播放(Windows / Vulkan 后端,GPU 直通零拷贝)。

## 0. 前置

- 已构建 avox SDK(`avox/build/windows/avplay/install/AMD64/Release/avox.lib`)
- Godot 4.3+,运行在 Vulkan 后端(Forward+ 或 Mobile)
- Visual Studio 2022 + CMake

## 1. 构建插件

```powershell
# avox 库默认路径与实际布局不一致, 用 AVOX_LIBRARY 指定:
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DAVOX_LIBRARY="D:/path/to/avox/build/windows/avplay/install/AMD64/Release/avox.lib"
cmake --build build/windows --config Release
```

产物: `bin/Release/avox_godot.dll` + `bin/avox_godot.gdextension`

## 2. 部署到 Godot 项目

按下面结构放进 Godot 项目的 `addons/avox_godot/`:

```
<godot_project>/
└── addons/avox_godot/
    ├── avox_godot.gdextension   ← 复制自 avox_godot_plugin/bin/avox_godot.gdextension
    └── bin/
        ├── avox_godot.dll       ← 复制自 avox_godot_plugin/bin/Release/avox_godot.dll
        └── (avox 运行时依赖 ↓ 全部复制自 avox/.../Release/)
            avox.dll
            avcodec-61.dll
            avformat-61.dll
            avutil-59.dll
            ...
```

> `avox_godot.dll` 运行时依赖 `avox.dll`,后者又依赖 ffmpeg 等 DLL。
> 缺任一个,Godot 控制台会报“无法加载 avox_godot.dll / 找不到依赖”。
> **最省事**:把 avox Release 目录下所有 `.dll` 一起复制到 `bin/`。

`.gdextension` 里 dll 路径已写死 `res://addons/avox_godot/bin/avox_godot.dll`,按上面结构放即可对上。

## 3. 跑 demo

1. 把 `demo/main.gd` 和 `demo/main.tscn` 复制到 Godot 项目根。
2. 打开 `main.tscn`,把脚本里的 `player.url` 改成真实地址。
3. 首次 Godot 会提示发现新 GDExtension,启用并 **重启编辑器**。
4. F5 运行 `main.tscn`,视频应显示在 TextureRect。

## 类名说明

节点类名是 **`MediaPlayer`**(代码里 `GDCLASS(MediaPlayer, Node)`)。
项目根 README 里写的 `AvoxMediaPlayer` 与代码不一致 —— 后续应统一加 `Avox` 前缀,避免与其它插件的全局类名冲突(本次未改,避免扩大改动面)。

## 排查

| 现象 | 可能原因 |
|---|---|
| 黑屏 / 纹理一直为空 | avox 尚未 onReady;或 GPU 直通未启用(确认 Godot 是 Vulkan 后端、dll 已正确加载) |
| 控制台报 dll 依赖缺失 | `avox.dll` 及其依赖未复制到 `bin/` |
| 画面花屏 / 尺寸错位 | 视频分辨率与导入 extent 不匹配(本版本已用 onReady 的 `getSourceInfo` 对齐) |
| 撕裂 / 闪烁 | 共享内存读写缺同步 —— 已知待解项,持续播放可能复现 |
