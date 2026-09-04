# AvoxPlayer Unity 插件 (platform/unity)

avox SDK 的 Unity 原生插件, 让 Unity 项目直接使用 avox 的音视频播放能力 (rtmp/rtsp/http/本地文件/torrent 等地址, 硬解, 音频直出)。

参考旧版 `xxxzhou/oeip` 的 oeip-unity3d 结构; 帧更新机制按现行做法重写 —— 旧版手写 D3D11 渲染线程方案已过时, 本插件采用 Unity 官方 [NativeRenderingPlugin](https://github.com/unity-technologies/nativerenderingplugin) TextureUpdate 示例 ([keijiro/TextureUpdateExample](https://github.com/keijiro/TextureUpdateExample) 同款) 的 `CommandBuffer.IssuePluginCustomTextureUpdateV2`, 与 Godot 插件 ([platform/godot/plugin](../godot/plugin)) 的双路架构一致:

| | Godot (avox_godot) | UE (AvoxPlayer) | Unity (avox_unity + com.avox.player) |
|---|---|---|---|
| 封装对象 | MediaPlayer (Node) | UAvoxMediaPlayerComponent | AvoxPlayer (MonoBehaviour) |
| GPU 直通 | enableVkOutput → Godot VkDevice 导入 | (未做, 预留) | enableVkOutput → Unity VkDevice 导入 → CreateExternalTexture |
| CPU 回退 | ISurfaceRenderOb::onFrame → ImageTexture | 同左 → UpdateTextureRegions | onFrame → BGRA 槽 → IssuePluginCustomTextureUpdateV2 (Unity 自上传) |
| 帧来源 | setOffSurface + enableYuvOut(nv12) | 同左 | 同左 |
| 音频 | avox 内置渲染直出 | 同左 | 同左 (WASAPI, 不经 AudioMixer) |

## 结构

```
platform/unity/
├── README.md                    # 本文档
├── deploy_unity.ps1             # 部署: UPM 包 + 原生 dll → <工程>/Packages/com.avox.player
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

- **avox SDK** — 本仓库, `python build_windows.py` (顶层 `AVOX_ENABLE_UNITY` 默认 ON) 产出 `avox_unity.dll` 到 `build/windows/avplay/install/AMD64/Release/`
- **Unity 2021.3+** — 用户自行安装
- 当前仅 **Windows x64**; CPU 回退全图形后端可用, GPU 直通需 **Unity Vulkan 后端**

## 部署到 Unity 工程

```powershell
./platform/unity/deploy_unity.ps1 -UnityProject D:\Work\MyUnityProject
```

脚本把 UPM 包复制到 `<工程>/Packages/com.avox.player` (Unity 自动识别本地包), 原生 dll 复制到包内 `Runtime/Plugins/Windows/x86_64/`。打开工程即用, 无需 .meta 入库 (Unity 自动生成)。

## 使用

1. 场景里给任意 GameObject 添加 **Avox / Avox Media Player** 组件
2. 设 `Url`, 勾 `AutoPlay` (或任意时机调 `Open()`)
3. 视频显示三选一:
   - **自动绑 Renderer**: 组件自动把 `VideoTexture` 经 MaterialPropertyBlock 绑到 `targetRenderer` (默认取自身) 的 `_MainTex`/`_BaseMap`, 挂个 Quad 即出画面
   - **代码/UI**: 读 `VideoTexture` 属性或监听 `onTextureCreated` 事件, 自行赋给 RawImage/Material
4. 事件: `onStateChanged` / `onReady` / `onComplete` / `onError`
5. 控制: `Pause()` / `Resume()` / `Seek(ms)` / `SetSpeed()` / `SetVolume()` / `ioPlan` (torrent 等, 下次 Open 生效)

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
- **D3D11 (默认) / OpenGL**: 自动走 CPU 回退, 行为一致仅多一次 BGRA 转换; 运行期可随时 `GpuPassthrough` 属性查当前模式
- 与 godot 一致的已知风险: 跨 VkDevice 外部内存直采在部分驱动 (Intel iGPU) 可能异常; 中途分辨率重导时旧纹理有一帧竞态
- 后续方向: avox 侧补 D3D11 导出 (`avox_windows/dx11` 已有 Dx11SharedTex 基础设施) 后, D3D11 后端可走 `OpenSharedResource1` 直通

## CPU 回退帧通路

```
avox 渲染线程                          Unity 渲染线程 (IssuePluginCustomTextureUpdateV2)
──────────────────────────────        ─────────────────────────────────────────────────
onFrame(YUVFrame) ↓                    UpdateTextureBegin: 按 Unity 纹理尺寸 malloc,
  NV12/yuv420P → BGRA (BT.601)           从帧槽 memcpy (无帧补黑), UpdateTextureEnd free
  存单帧槽位 (新帧覆盖旧帧)              上传/采样由 Unity 完成 (跨 URP/HDRP/内置管线)
```

## 已知限制 / 后续

- 仅 Win64; Android 需 AHardwareBuffer 导入 + Gradle 接入 (godot 侧已有 AHB 流程可参照), iOS 需 Metal 路径
- GPU 直通要求 Vulkan 后端 (见上); D3D11 直通待 avox DX11 导出
- YUV 转换 BT.601 full-range (同 godot/UE 插件), BT.709 源轻微偏色
- IL2CPP 正常工作 (纯 blittable P/Invoke); Unity 6 可选升级 `[LibraryImport]` 源生成
- `getOption`/`getMuxer`/`getSubtitle` 未封装, 需要时参照 PlayerBridge 现有模式扩展
