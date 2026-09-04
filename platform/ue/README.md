# AvoxPlayer UE 插件 (platform/ue)

avox SDK 的 Unreal Engine 插件, 让 UE 项目直接使用 avox 的音视频播放能力 (rtmp/rtsp/http/本地文件/torrent 等地址, 硬解, 音频直出)。

参考旧版 `Q:\Work\github\aocec\UE4Test\Plugins\AocePlugins` 的结构设计; 与 Godot 插件 ([platform/godot/plugin](../godot/plugin)) 同构:

| | Godot (avox_godot) | UE (AvoxPlayer) |
|---|---|---|
| 封装对象 | `MediaPlayer` (Node) | `UAvoxMediaPlayerComponent` (ActorComponent) |
| 帧输出 | SurfaceTextureBridge → ImageTexture/Texture2DRD | FAvoxVideoBridge → `UTexture2D` (BGRA8) |
| 帧来源 | `setOffSurface` + `enableYuvOut(nv12)` 回调 | 同左 (CPU 路径, NV12/yuv420P → BGRA) |
| 音频 | avox 内置渲染 (AudioTrack/WASAPI) | 同左 (WASAPI 直出, 不走 UE 音频) |

## 结构

```
platform/ue/
├── README.md                  # 本文档
├── deploy_ue.ps1              # 部署脚本: 插件源码 + avox 头/dll 布置到 UE 工程
└── plugin/
    ├── AvoxPlayer/            # UE 插件本体 (部署后整个复制到 <工程>/Plugins/AvoxPlayer)
    │   ├── AvoxPlayer.uplugin
    │   └── Source/AvoxPlayer/
    │       ├── AvoxPlayer.Build.cs          # 链接 avox.lib, 延迟加载 avox.dll, 打包 RuntimeDependencies
    │       ├── Public/
    │       │   ├── AvoxPlayerModule.h       # 模块入口 (启动时显式加载 avox.dll, 缺库即报错)
    │       │   ├── AvoxEnums.h              # EAvoxPlayerState / EAvoxIoPlan
    │       │   ├── AvoxMediaPlayerComponent.h
    │       │   └── AvoxFunctionLibrary.h
    │       └── Private/
    │           ├── AvoxSdk.h                # avox 头统一入口 (屏蔽 UE 严格告警)
    │           ├── AvoxVideoBridge.h/.cpp   # ISurfaceRenderOb 帧 → BGRA 单帧槽位
    │           ├── AvoxMediaPlayerComponent.cpp
    │           └── AvoxFunctionLibrary.cpp
    └── demo/AvoxDemoSetup.py  # 编辑器内一键生成测试 Actor (Output Log 执行)
```

## 依赖

- **avox SDK** — 本仓库, 先 `python build_windows.py` 产出 `build/windows/avplay/install/AMD64/Release/` (avox.dll/lib + FFmpeg 等运行时 dll)
- **Unreal Engine 5.x** — 用户自行安装 (UE 4.27 理论可用, 未验证)
- 当前仅 **Windows x64** (uplugin PlatformAllowList + Build.cs Win64 分支)

## 部署到 UE 工程

```powershell
# 插件源码复制到 <工程>/Plugins/AvoxPlayer, 并布置 ThirdParty (avox 头/dll)
./platform/ue/deploy_ue.ps1 -UeProject D:\Work\MyUnrealProject

# 覆盖重建 (丢弃工程内旧插件目录)
./platform/ue/deploy_ue.ps1 -UeProject D:\Work\MyUnrealProject -Force
```

然后打开 UE 工程, 启用 AvoxPlayer 插件 (或在 `.uproject` 里加):

```json
"Plugins": [ { "Name": "AvoxPlayer", "Enabled": true } ]
```

原理与 Godot 插件的 junction 部署不同: UE 插件要经 UBT 编译, 必须真实拷贝源码; `ThirdParty/avox` 放 avox 头与链接库, 运行 dll 由部署脚本同时拷入插件 `Binaries/Win64` (编辑器期延迟加载解析路径) 和 `ThirdParty/avox/lib/Win64` (打包期 `RuntimeDependencies` 源)。

## 使用

### C++

```cpp
#include "AvoxMediaPlayerComponent.h"

UAvoxMediaPlayerComponent* comp = NewObject<UAvoxMediaPlayerComponent>(this);
// 或编辑器里给 Actor 添加 "Avox Media Player" 组件
comp->Url = TEXT("rtmp://example.com/live");
comp->bHardDecode = true;
comp->Open(comp->Url);                   // 或 comp->Open(TEXT("d:/demo.mp4"));
// 每帧视频输出到 comp->VideoTexture (BGRA8 UTexture2D)
```

### 蓝图

1. 任意 Actor 添加 **AvoxMediaPlayer** 组件
2. 设 `Url`, 调 `Open`; `OnStateChanged`/`OnReady`/`OnComplete`/`OnError` 监听状态
3. 视频显示两种方式:
   - **材质**: 建材质放 `TextureSampleParameter2D` (参数名如 `AvoxTex`) → 创建动态材质实例 → `SetTextureParameterValue("AvoxTex", VideoTexture)` → 贴到 Plane/Widget
   - **UMG**: `Image` 控件每帧 `SetBrushFromTexture(VideoTexture)`
4. 控制: `Pause` / `Resume` / `Seek` / `SetSpeed` / `SetVolume` / `SetIoPlan` (torrent 等下次 Open 生效)

### 编辑器内快速验证

编辑器 Output Log 的 Cmd 输入框执行 (改脚本里 URL):

```
py <avox仓库>/platform/ue/plugin/demo/AvoxDemoSetup.py
```

会生成 `AvoxPlayerDemo` Actor 并直接播放, 看日志状态与纹理尺寸。

## 帧通路 (与 godot CPU 回退路径一致)

```
avox 渲染线程                    UE 游戏线程 (组件 Tick)
─────────────────────────────   ─────────────────────────────
setOffSurface(nv12) 离屏渲染     flushEvents(): 广播状态/错误委托
enableYuvOut(nv12)               popFrame(): 取最新 BGRA 帧
onFrame(YUVFrame) ↓              首帧/变分辨率 → CreateTransient
  NV12/yuv420P → BGRA (BT.601)   UpdateTextureRegions 异步上传
  存单帧槽位 (新帧覆盖旧帧)        (渲染线程完成后释放上传内存)
```

## 已知限制 / 后续

- 仅 Win64; Android/iOS 需补各平台 Build.cs 分支与 APL/gradle 接入
- 视频纹理走 CPU 转换 (NV12/yuv420P → BGRA), 大分辨率有 CPU 开销; godot 的 Vulkan 零拷贝直通 (`enableVkOutput` 外部内存导入 UE RHI) 是后续方向
- YUV 转换用 BT.601 full-range (同 godot 插件), BT.709 源颜色会轻微偏色
- 音频由 avox 内置 WASAPI 直出, 不经过 UE AudioMixer (与 Godot 方案一致)
- `IMediaPlayer::getOption`/`getMuxer`/`getSubtitle` 未封装, 需要时参照组件现有模式扩展
