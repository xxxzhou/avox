# AvoxPlayer Unity 插件 (platform/unity)

> **本模块的插件工程已独立为私有仓 `avox-unity`**（UPM 包源码真身 + demo 工程 + 部署/回归/合规工具 + 文档）。
> 本目录保留原生插件 C++ 源码（`plugin/src`，由 avox CMake `AVOX_ENABLE_UNITY` 构建）与本文档存档。
> 使用/开发入口见私有仓 `D:\Work\github\avox-unity\README.md`。

avox SDK 的 Unity 原生插件, 让 Unity 项目直接使用 avox 的音视频播放能力 (rtmp/rtsp/http/本地文件/torrent 等地址, 硬解, 音频直出)。

参考旧版 `xxxzhou/oeip` 的 oeip-unity3d 结构; 帧更新机制按现行做法重写 —— 旧版手写 D3D11 渲染线程方案已过时, 本插件采用 Unity 官方 [NativeRenderingPlugin](https://github.com/unity-technologies/nativerenderingplugin) TextureUpdate 示例 ([keijiro/TextureUpdateExample](https://github.com/keijiro/TextureUpdateExample) 同款) 的 `CommandBuffer.IssuePluginCustomTextureUpdateV2`, 与 Godot 插件 ([platform/godot/plugin](../godot/plugin)) 的双路架构一致:

| | Godot (avox_godot) | UE (AvoxPlayer) | Unity (avox_unity + com.avox.player) |
|---|---|---|---|
| 封装对象 | MediaPlayer (Node) | UAvoxMediaPlayerComponent | AvoxPlayer (MonoBehaviour) |
| GPU 直通 | enableVkOutput → Godot VkDevice 导入 | (未做, 预留) | enableVkOutput → Unity VkDevice 导入 → CreateExternalTexture |
| CPU 回退 | onFrame → NV12 槽 → R8 纹理 + shader (SubViewport) | 同左 → UpdateTextureRegions | onFrame → NV12 槽 → R8 纹理 (IssuePluginCustomTextureUpdateV2) + shader Blit |
| 帧来源 | setOffSurface + enableYuvOut(nv12) | 同左 | 同左 |
| 音频 | avox 内置渲染直出 | 同左 | 同左 (WASAPI, 不经 AudioMixer) |

## 结构

```
platform/unity/
├── README.md                    # 本文档
├── deploy_unity.ps1             # 部署: UPM 包 + 原生 dll → <工程>/Packages/com.avox.player
├── test_unity.ps1               # 一键回归: 冒烟 + 真实播放测试 (batchmode, 默认找 D:\Work\unity 编辑器)
└── plugin/
    ├── CMakeLists.txt           # avox_unity 目标 (顶层 AVOX_ENABLE_UNITY, Windows 默认 ON)
    ├── unity/com.avox.player/   # UPM 包 (部署到 <工程>/Packages/)
    │   ├── package.json
    │   └── Runtime/
    │       ├── AvoxPlayer.Runtime.asmdef
    │       ├── AvoxNative.cs    # P/Invoke 绑定
    │       └── AvoxPlayer.cs    # MonoBehaviour: 状态事件/纹理创建/Renderer 绑定
    └── src/
        ├── AvoxUnityApi.h/.cpp  # C API 导出 (P/Invoke 入口)
        ├── PlayerBridge.h/.cpp  # IMediaPlayer 包装 + BGRA 帧槽 + GPU 状态机
        ├── GpuPassthrough.h/.cpp# UnityPluginLoad + volk + NT句柄导入 + 纹理更新回调
        ├── volk_impl.c          # VOLK_IMPLEMENTATION 编译单元
        └── unity/*.h            # Unity 官方插件头 (Companion License, 取自 NativeRenderingPlugin)
```

## 依赖

- **avox SDK** — 本仓库, `python build_windows.py` (顶层 `AVOX_ENABLE_UNITY` 默认 ON) 产出 `avox_unity.dll` 到 `build/windows/avox/install/AMD64/Release/`
- **Unity 2021.3+** — 用户自行安装
- 当前仅 **Windows x64**; CPU 回退全图形后端可用, GPU 直通需 **Unity Vulkan 后端**

## 部署到 Unity 工程

```powershell
./platform/unity/deploy_unity.ps1 -UnityProject D:\Work\MyUnityProject
```

脚本把 UPM 包复制到 `<工程>/Packages/com.avox.player` (Unity 自动识别本地包), 原生 dll 复制到包内 `Runtime/Plugins/Windows/x86_64/`。打开工程即用, 无需 .meta 入库 (Unity 自动生成)。

## 回归测试

```powershell
./platform/unity/test_unity.ps1 -UnityProject D:\Work\unity\AvoxTest
```

脚本向工程 `Assets/Editor/` 写入两个 batchmode 测试 (反射调 internal AvoxNative, 不污染工程配置) 后依次执行:

- **冒烟**: 原生 dll 链加载 → `avoxPlayerCreate/Destroy` → `AvoxPlayer` 组件挂载
- **播放** (软解): 打开 `assets/video/avox_electron.mp4` → 等 Ready/Playing → 校验帧尺寸与 2 秒进度推进

编辑器默认优先找 `D:\Work\unity\*\Editor\Unity.exe`, 兜底 Unity Hub 目录; 可用 `-UnityEditor` 指定, `-SkipPlay` 只跑冒烟。全绿退出码 0。

## 使用

1. 场景里给任意 GameObject 添加 **Avox / Avox Media Player** 组件
2. 设 `Url`, 勾 `AutoPlay` (或任意时机调 `Open()`)
3. 视频显示三选一:
   - **自动绑 Renderer**: 组件自动把 `VideoTexture` 经 MaterialPropertyBlock 绑到 `targetRenderer` (默认取自身) 的 `_MainTex`/`_BaseMap`, 挂个 Quad 即出画面
   - **代码/UI**: 读 `VideoTexture` 属性或监听 `onTextureCreated` 事件, 自行赋给 RawImage/Material
4. 事件: `onStateChanged` / `onReady` / `onComplete` / `onError`
5. 控制: `Pause()` / `Resume()` / `Seek(ms)` / `SetSpeed()` / `SetVolume()` / `ioPlan` (torrent 等, 下次 Open 生效)
6. 扩展: `SetOptionInt/String/...` / `GetOption*` (键值参数透传 `IMediaPlayer::getOption`), `StartRecord(path)` / `StopRecord()` / `RecordState` (ffmpeg 封装录制), `LoadSrt(path)` / `CloseSubtitle()` (SRT 字幕; ASR/翻译依赖可选模块)
7. 色彩: 源 colorSpace (BT.601/709/2020 × full/limited) 自码流元数据解析, 渲染管线 shader 与 CPU 回退转换同参切换

```csharp
var player = gameObject.AddComponent<Avox.Player.AvoxPlayer>();
player.onTextureCreated.AddListener(tex => GetComponent<Renderer>().material.mainTexture = tex);
player.onStateChanged.AddListener(s => Debug.Log($"state: {s}"));
player.Open("rtmp://example.com/live");
```

## GPU 直通 (与 godot 同构)

```
avox (自己的 VkDevice)                    Unity 主线程                    Unity 渲染线程
──────────────────────────────           ───────────────────────         ─────────────────────
onReady 尺寸就绪 → pendingGpuInit
                                         avoxPlayerUpdateGpu():
                                           volk 加载 Unity instance/device
                                           enableVkOutput(w,h)  ← 每帧管线自动拷入共享图 (RGBA8, GENERAL layout)
                                           getVkOutputHandle → NT 句柄
                                           vkCreateImage(OPAQUE_WIN32)+vkAllocateMemory 导入
                                           CloseHandle
                                         CreateExternalTexture 收养 VkImage
采样 ←──────────────── 外部内存 VkImage 直接采样 (零 CPU 回读/零拷贝) ────────
```

- **后端要求**: 仅 Unity **Vulkan** 图形 API (Player Settings 图形 API 列表把 Vulkan 排最前, 或 `-force-vulkan` 启动)。与 godot 插件同样的约束 —— avox 导出的是 Vulkan 外部内存 (`OPAQUE_WIN32`), Unity 默认的 **D3D11 无法打开** 该内存级句柄 (非 D3D11 资源句柄)
- **D3D11 (默认) / OpenGL**: 自动走 CPU 回退, 行为一致仅多一次 NV12 回读 + shader 转换; 运行期可随时 `GpuPassthrough` 属性查当前模式
- 与 godot 一致的已知风险: 跨 VkDevice 外部内存直采在部分驱动 (Intel iGPU) 可能异常; 中途分辨率重导时旧纹理有一帧竞态
- 后续方向: avox 侧补 D3D11 导出 (`avox_windows/dx11` 已有 Dx11SharedTex 基础设施) 后, D3D11 后端可走 `OpenSharedResource1` 直通

## CPU 回退帧通路 (YUV 上传, shader 转 RGB)

```
avox 渲染线程                          Unity 主线程 / 渲染线程
──────────────────────────────        ─────────────────────────────────────────────────
onFrame(YUVFrame) ↓                    IssuePluginCustomTextureUpdateV2 → R8 纹理
  只重排成紧凑 NV12                      (w × h*3/2, UpdateTextureBegin 从帧槽 memcpy,
  (yuv420P 顺手交织成 NV12)               无帧补中性黑 Y=16/UV=128, End free)
  存单帧槽位 (新帧覆盖旧帧)            Graphics.Blit(R8, RenderTexture, YUV shader)
                                        → VideoTexture 对外是转换后的 RenderTexture
```

一张 R8 纹理装整帧 NV12 (Y 在上 2/3, 交错 UV 在下 1/3), 与
`swig/nodejs/yuvglrender.js` 的单图方案同构。相比原先的逐像素 CPU 转换:
上传量 1.5 字节/像素而非 4, 色转全在 GPU, 色度还免费获得双线性上采样。
矩阵/量程由源 `colorSpace` 经 `avoxPlayerGetColorSpace` 传给 shader uniform。

shader 在 `Runtime/Resources/AvoxYuvToRgb.shader` (放 Resources 下才保证进包)。
画面上下颠倒时勾组件的 `cpuFlipY`。

## 已知限制 / 后续

- 仅 Win64; Android 需 AHardwareBuffer 导入 + Gradle 接入 (godot 侧已有 AHB 流程可参照), iOS 需 Metal 路径
- GPU 直通要求 Vulkan 后端 (见上); D3D11 直通待 avox DX11 导出
- YUV 转换矩阵/量程由源 colorSpace 元数据驱动 (容器/VUI 标记优先, 未标记按 ≥720p=BT.709、H264/H265/MPEG=limited 惯例推断, 见 `FFHelper::ffColorSpace`); godot 插件已接入, UE 插件尚未
- `VideoTexture` / `onTextureCreated` 的类型是 `Texture` (不再是 `Texture2D`) —— CPU 回退输出的是 shader 转换后的 `RenderTexture`; 旧代码若显式声明 `Texture2D` 参数需改签名
- GPU 直通勿在 batchmode/headless 验证: 无 Game View 渲染循环时 `enableVkOutput` 因 outputLayer 未建失败, 且该场景下 Unity 进程可能段错误 (2026-09-05 实测); 交互式编辑器不受影响
- IL2CPP 正常工作 (纯 blittable P/Invoke); Unity 6 可选升级 `[LibraryImport]` 源生成
- `getPingback`/埋点未封装; ASR/翻译字幕依赖 avox_sherpa/translation 可选模块, 未集成时 `LoadSrt` 仅文件字幕可用
