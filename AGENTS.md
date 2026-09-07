# AGENTS.md

## 项目概述

跨平台音视频播放器 SDK (C++17)，支持直播/本地播放、硬解/GPU 渲染、Vulkan 图像处理、WebRTC、AI 功能。

**平台:** Windows, Android, iOS, Linux, WebAssembly

详细文档: [doc/INDEX.md](doc/INDEX.md)

## 编码规范

- **C++17** + RAII (智能指针，无手动 new/delete)
- **命名:** 类型 `PascalCase`，函数 `camelCase`，成员 `snakeCase`，常量 `kPascalCase`
  - 类成员字段：禁止以 `_` 开头或结尾 (如 `int inputSize;` 而非 `int _inputSize;` `int inputSize_;`)
  - 方法内局部参数：可以以 `_` 结尾 (如 `int inputSize_` 在方法参数中允许)
- **类定义顺序:** 构造函数/析构函数 → private 成员 → protected 成员 → public 方法
- **格式:** 函数体内不要空行，逻辑分段用注释而非空行
- **注释:** 不要太多，写精简点；类最多三行，方法及变量一般一行就够了
- **导出:** `AVOX_EXPORT` 定义于 `src/avox/AvoxDef.h`，`AVOX_EXPORT_DEFINE` 用于构建 SDK
- **文件:** `.h`(C接口), `.hpp`(C++), `.cpp`(实现), `*Export.h`(公共API)

详细规范见 [DeveloperGuide.md](DeveloperGuide.md)

## 目录结构

| 文件夹 | 功能 |
|--------|------|
| **3rdparty/** | 第三方依赖库源码 |
| **assets/** | 资源文件 (字体、图片、模型等) |
| **cmake/** | CMake 构建脚本和工具链配置 |
| **glsl/** | Vulkan GLSL 着色器源码 (.glsl → .spv) |
| **platform/** | 平台相关代码 |
| **samples/** | 示例程序和演示项目 |
| **src/** | 核心 SDK 源码 (avox 模块) |
| **swig/** | SWIG 绑定代码 (C#/Java/Node.js) |
| **build/** | 构建输出目录 |
| **tests/** | 单元测试 (doctest, 随主构建自动编译) |
| **doc/** | 项目文档 |
| **plugins/** | 插件模块 |
| **script/** | 辅助脚本 |

## 构建命令

```bash
python build_windows.py   # Windows x64
python build_android.py   # Android arm64-v8a/armeabi-v7a (需 NDK 26.1.10909125)
python build_ios.py       # iOS arm64/x86_64
python build_mac.py       # macOS arm64/x64/universal (需 macOS + Xcode)
python build_linux.py     # Linux x64

# 单元测试 (随构建自动编译, 手动运行:)
ctest --test-dir build/windows/avplay --output-on-failure -C Release
```

## 平台支持

| 平台 | 架构 | 硬解 | 渲染后端 |
|------|------|------|----------|
| Windows | x64 | DX11 | DX11/DX12/Vulkan |
| Android | arm64-v8a, armeabi-v7a | MediaCodec | OpenGL ES/Vulkan |
| iOS | arm64, x86_64 | VideoToolbox | Metal/Vulkan |
| macOS | arm64, x64 | VideoToolbox(待验证) | Metal/Vulkan(待验证, 结构就绪 `python build_mac.py`) |
| Linux | x64 | VAAPI(计划) | Vulkan |
| WebAssembly | wasm32 | 软件 | WebGL(计划) |

## 核心模块 (src/avox/)

**播放器:** `IMediaPlayer`(URL播放), `ISourcePlayer`(设备采集) → 实现类 `MediaPlayer`, `SourcePlayer`

**数据流:** IO层(`IAVSource`) → 解码层 → 渲染层(`ISurfaceRender`, `IAudioRender`)

**关键接口:** `IAVSource`, `IRawSource`, `ISourceInfo`, `ISurfaceRender`, `IAudioRender`

**关键文件:** `src/avox/AvoxPlayer.h`, `src/avox/AvoxSource.h`, `src/avox/AvoxDef.h`

## 扩展模块

- `avox_ffmpeg/` - FFmpeg 音视频解封装/解码
- `avox_zlmediakit/` - ZLMediaKit 直播协议
- `avox_webrtc/` - WebRTC 集成
- `avox_onnx/` - ONNX Runtime AI 推理
- `avox_cv/` - AI 图像修复 (水印去除) + 通用 YOLO 检测/分类
- `avox_vulkan/` - Vulkan GPU 图像处理 (100+ 效果)
- `avox_sherpa/` - 语音识别
- `avox_translation/` - 神经机器翻译

SWIG 绑定: `swig/csharp/`, `swig/nodejs/`, `swig/python/` (Java 绑定构建时生成于 `swig/java/`, 不入库)

## 重要说明

- **API 命名:** `create*` 返回需释放的内存，`get*` 返回托管对象
- **UE 插件:** avox 的 Unreal Engine 插件已迁移至独立分发仓 [avox-ue](platform/ue/README.md)(AvoxPlayer: AvoxMediaPlayerComponent/Actor + AvoxGpuInterop), GPU 直通 UE RHI D3D11/D3D12/Vulkan
- **Unity 插件:** avox 的 Unity 原生插件在 [platform/unity](platform/unity/README.md)(avox_unity.dll + com.avox.player UPM 包, 帧更新用 IssuePluginCustomTextureUpdateV2; GPU 直通 Unity Vulkan/D3D11/D3D12 后端, 见 platform/unity/plugin/src/GpuPassthrough.h), `platform/unity/deploy_unity.ps1 -UnityProject <工程路径>` 部署; 顶层 AVOX_ENABLE_UNITY 构建原生 dll
- **Godot 播放器 Android 打包:** 新机器从零编出含 torrent 的 Android APK 全流程见 [platform/godot/docs/新机器Android打包.md](platform/godot/docs/新机器Android打包.md)(一键脚本 platform/godot/build_android_godot.sh; 坑表含 INTERNET 权限/dex/shader/深链注入)
- **VSCode 卡顿:** 项目用久了"所有操作卡"是 VSCode 工作区缓存按路径累积所致,解法见 [doc/tools/VSCode卡顿排查.md](doc/tools/VSCode卡顿排查.md)(删 Cache/CachedData/workspaceStorage),别重拉 clone 或改名
- **子模块重置:** `git -C 3rdparty/sherpa-onnx reset --hard HEAD`
- **GLSL Shader:** `glsl/` 编译为 `.spv`，自动复制到 `build/.../glsl/`

## 外部依赖库路径

大型依赖库（WebRTC、ONNX Runtime、OpenCV 等）存放在 `../avc_library` 目录，与 avplay 同级：

```
work/
├── avplay/           # 本项目
└── avc_library/      # 大型依赖库 (git clone xxxzhou/avc_library)
    ├── 3rdparty/
    │   └── library/  # 预编译库 (android/ios/windows)
    │       ├── android/
    │       ├── ios/
    │       └── windows/
    └── src/          # WebRTC 源码目录
```

**自定义路径:**
- 环境变量: `export AVOX_EXTERNAL_LIBRARY_DIR=/path/to/avc_library`
- CMake 参数: `-DAVOX_EXTERNAL_LIBRARY_DIR=/path/to/avc_library`
- 默认: 自动查找 `../avc_library`