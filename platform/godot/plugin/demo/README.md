# avox Godot 播放器 —— 最小示例

在 Godot 项目里跑通 avox 视频播放(Windows / Vulkan 后端,GPU 直通零拷贝)。

## 0. 前置

- 已构建 avox SDK(`avox/build/windows/avox/install/AMD64/Release/avox.lib`)
- Godot 4.3+,运行在 Vulkan 后端(Forward+ 或 Mobile)
- Visual Studio 2022 + CMake

## 1. 构建插件

```powershell
# avox 库默认路径与实际布局不一致, 用 AVOX_LIBRARY 指定:
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DAVOX_LIBRARY="D:/path/to/avox/build/windows/avox/install/AMD64/Release/avox.lib"
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
            avcodec-63.dll
            avformat-63.dll
            avutil-61.dll
            swresample-7.dll
            zlib1.dll
            libwinpthread-1.dll
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

## 4. WebRTC 拉流 demo (rtc_main)

在 MediaPlayer demo 基础上多一个前提:`bin/plugins/avox_webrtc.dll` 要在位
(avox 运行期从 `<avox.dll目录>/plugins/` 扫描 WebRTC 动态插件;用 deploy_godot.ps1
junction 方式部署的,构建输出里自带 plugins/ 目录,无需额外操作)。

1. 把 `demo/rtc_main.gd` 和 `demo/rtc_main.tscn` 复制到 Godot 项目根。
2. F5 运行 `rtc_main.tscn`,默认拉本机 ZLM 的 `rtsp://…/live/avox264` 对应信令
   `http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play`;
   也可命令行传入任意信令地址:`godot --path . res://rtc_main.tscn -- <信令url>`。
3. 底部状态行依次显示 连接状态 → 出图分辨率 / 错误码。

最小代码就三步(完整见 `rtc_main.gd`):

```gdscript
var rtc := RtcPlayer.new()
add_child(rtc)
rtc.connect_signaling("http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play")
# 远端画面: rtc.get_texture(), 首帧信号 first_video_frame, 连接状态 connection_state_changed
```

要点:
- 纯拉流显式 `rtc.set_video_direction(1)` + `rtc.set_audio_direction(1)` (recvonly,默认 sendrecv)。
- `connection_state_changed` 收到 `connected`(2) 才是真正连上;首帧等 `first_video_frame`。
- 自定义信令(自有服务器)不调 `connect_signaling`,改 `open_rtc()` + 监听
  `local_sdp`/`ice_candidate` 信号自己送出,远端消息用 `set_remote_sdp`/`add_ice_candidate` 回填。
- 推流方向(摄像头/麦克风上麦)用 `set_roll_type(1)` + 方向设 sendonly,配合长连接信令。
- 无头回归:`godot --headless --path <项目> -s res://test_rtc.gd -- <信令url>`
  (脚本在 avox 仓 `platform/godot/tools/test_rtc.gd`,输出统一判定行)。

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
