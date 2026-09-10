# AGENTS.md

## 项目概述

跨平台音视频播放器 SDK (C++17)，支持直播/本地播放、硬解/GPU 渲染、Vulkan 图像处理、WebRTC、AI 功能。

**平台:** Windows, Android, iOS, Linux, WebAssembly

详细文档: [doc/INDEX.md](doc/INDEX.md)

## 编码规范

- **C++17** + RAII (智能指针，无手动 new/delete)
- **命名:** 类型 `PascalCase`，函数 `camelCase`，成员 `snakeCase`，常量 `kPascalCase`；类成员禁用 `_` 前后缀 (写 `int inputSize;` 不写 `_inputSize`/`inputSize_`)，方法局部参数可带 `_` 后缀
- **类定义顺序:** 构造/析构 → private → protected → public
- **格式:** 函数体内不空行，逻辑分段用注释；注释精简：类≤2行，方法/变量1行
- **导出:** `AVOX_EXPORT` 定义于 `src/avox/AvoxDef.h`，`AVOX_EXPORT_DEFINE` 用于构建 SDK
- **文件:** `.h`(C接口), `.hpp`(C++), `.cpp`(实现), `*Export.h`(公共API)

详细规范见 [DeveloperGuide.md](DeveloperGuide.md)

## 目录结构

`3rdparty/` 第三方库源码 · `assets/` 资源 · `cmake/` 构建脚本 · `glsl/` 着色器源码 · `platform/` 平台代码 (UE/Unity/Godot 插件) · `samples/` 探针与人工走查样例 (归位说明见 samples/README.md) · `src/` SDK 源码 (avox 模块) · `swig/` C#/Java/Node/python 绑定 · `build/` 构建输出 · `tests/` 单测 (doctest, 随主构建编译) 与播放回归矩阵共享用例表 (tests/playmatrix) · `doc/` 文档 · `plugins/` 插件 · `script/` 辅助脚本

## 构建命令

```bash
python build_windows.py   # Windows x64
python build_android.py   # Android arm64-v8a/armeabi-v7a (需 NDK 26.1.10909125)
python build_ios.py       # iOS arm64/x86_64
python build_mac.py       # macOS arm64/x64/universal (需 macOS + Xcode)
python build_linux.py     # Linux x64

# 单元测试 (随构建自动编译, 手动运行:)
ctest --test-dir build/windows/avox --output-on-failure -C Release

# 播放回归矩阵 (每次改动后跑一次, 确认播放链路未坏; 需本机 ZLM MediaServer)
python script/testenv/play_regress.py
python script/testenv/play_regress.py --offline   # 无 ZLM 的离线子集 (CI 用这个)

# 提交门禁: pre-push 跑 ctest + 上面那个离线子集 (已装 core.hooksPath=.githooks)
# 跳过: AVOX_SKIP_GATE=1 git push; 全量: AVOX_GATE_FULL=1 git push
```

## 平台支持

| 平台 | 架构 | 硬解 | 渲染后端 |
|------|------|------|----------|
| Windows | x64 | DX11 | DX11/DX12/Vulkan |
| Android | arm64-v8a, armeabi-v7a | MediaCodec | OpenGL ES/Vulkan |
| iOS | arm64, x86_64 | VideoToolbox | Metal/Vulkan |
| macOS | arm64, x64 | VideoToolbox | Metal/Vulkan |
| Linux | x64 | VAAPI(计划) | Vulkan |
| WebAssembly | wasm32 | 软件 | WebGL(计划) |

## 核心模块 (src/avox/)

**播放器:** `IMediaPlayer`(URL播放), `ISourcePlayer`(设备采集) → 实现类 `MediaPlayer`, `SourcePlayer`

**数据流:** IO层(`IAVSource`, `IRawSource`, `ISourceInfo`) → 解码层 → 渲染层(`ISurfaceRender`, `IAudioRender`)

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

- **提交消息:** 首行 ≤50 字、硬上限 100 字，说清「哪个模块 + 做了什么」；细节写 commit body 或文档，不塞首行。历史提交多有超长，不作参照
- **日志:** 输出只用英文
- **API 命名:** `create*` 返回需释放的内存，`get*` 返回托管对象
- **UE 插件:** 独立分发仓 [avox-ue](platform/ue/README.md) (AvoxMediaPlayerComponent/Actor + AvoxGpuInterop)，GPU 直通 RHI D3D11/D3D12/Vulkan
- **Unity 插件:** [platform/unity](platform/unity/README.md)；`AVOX_ENABLE_UNITY` 构建原生 dll，`deploy_unity.ps1 -UnityProject <工程路径>` 部署；帧通路/GPU 直通细节见其 README
- **Godot Android 打包:** 全流程见 [platform/godot/docs/新机器Android打包.md](platform/godot/docs/新机器Android打包.md)，一键脚本 `platform/godot/build_android_godot.sh`
- **VSCode 卡顿:** 工作区缓存按路径累积所致，解法见 [doc/tools/VSCode卡顿排查.md](doc/tools/VSCode卡顿排查.md)，别重拉 clone 或改名
- **子模块重置:** `git -C 3rdparty/sherpa-onnx reset --hard HEAD`
- **GLSL:** `glsl/` 编译为 `.spv`，自动复制到 `build/.../glsl/`

## 外部依赖库路径

大型依赖库 (WebRTC、ONNX Runtime、OpenCV 等) 在同级 `../avc_library` (git clone xxxzhou/avc_library)：预编译库在 `3rdparty/library/<android|ios|windows>/`，WebRTC 源码在 `src/`；同级另有 avox-godot/avox-ue/avox-unity 插件仓。

自定义路径: 环境变量 `AVOX_EXTERNAL_LIBRARY_DIR` 或 CMake 参数 `-DAVOX_EXTERNAL_LIBRARY_DIR=<路径>`，默认自动查找 `../avc_library`。
