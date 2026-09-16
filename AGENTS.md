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

`3rdparty/` 第三方库源码 · `assets/` 资源 · `cmake/` 构建脚本 · `glsl/` 着色器源码 · `platform/` 平台代码 (UE/Unity/Godot 插件) · `samples/` 探针与人工走查样例 (归位说明见 samples/README.md) · `src/` SDK 源码 (avox 模块) · `swig/` C#/Java/Node/python 绑定 · `build/` 构建输出 · `tests/` 白盒单测 (doctest, 随主构建编译; 播放回归矩阵等测试正在移交同级 avox-test 仓, 见下「测试」节) · `doc/` 文档 · `plugins/` 插件 · `script/` 辅助脚本

## 构建命令

```bash
python build_windows.py   # Windows x64
python build_android.py   # Android arm64-v8a/armeabi-v7a (需 NDK 26.1.10909125)
python build_ios.py       # iOS arm64/x86_64
python build_mac.py       # macOS arm64/x64/universal (需 macOS + Xcode)
python build_linux.py     # Linux x64

# 单元测试 (随构建自动编译, 手动运行:)
ctest --test-dir build/windows/avox --output-on-failure -C Release
```

## 测试：移交同级 avox-test 仓（进行中）

**功能开发/修复完成后的测试验证，一律去同级 `../avox-test` 仓跑**（统一测试仓，收拢 avox/panvox/三引擎的全部测试，入口见 [avox-test/README.md](../avox-test/README.md)）。

- **已迁入 avox-test**（规范版本在那边维护）：`script/testenv/` 推流与回归脚本、播放回归矩阵用例表与宿主 `l1_avox/playmatrix/`、`doc/test/` 测试文档、`assets/video/` 标准测试源、`platform/godot/tools/tests/` headless 用例。本仓同名目录只是**过渡期副本，不再更新**
- **矩阵副本已删（2026-09-16）**：`tests/playmatrix/` 不再存在；本仓 `platform/*/playtest` 与 `platform/ios/avoxtest` 只是**宿主壳**，用例表与 `HostMain.cpp` 都取自 `../avox-test/l1_avox/playmatrix`（CMake 变量 `AVOX_TEST_ROOT`，可 `-DAVOX_TEST_ROOT=<路径>` 指定）。**同级没有 avox-test 时 playtest 目标自动跳过**（Android `PlayMatrixJni.cpp` 走 `__has_include` 降级成空实现），构建不受影响 —— 改矩阵只改 avox-test 一处
- **留在本仓**：`tests/test_*.cpp`（doctest 白盒单测，直接编本仓源码，随主构建编译）；`samples/` 属第二批迁移
- **新增/修改测试用例一律写到 avox-test**，不要再往本仓加

```bash
# 在 avox-test 仓跑回归 (AVOX_ROOT 默认指向同级 avox, 自动读本仓 build/install 产物)
cd ../avox-test
python script/testenv/play_regress.py --offline   # 离线子集, 无 ZLM 也能跑 (每次改动后跑)
python script/testenv/play_regress.py             # 全量 (需本机 ZLM MediaServer)
python script/build_runner.py --target playtest   # 只重建 runner (avox 主树编不过时用它绕过)
```

**提交/推送不与测试绑定**（2026-09-16 口径）：pre-push 只做文档治理 doc_check（`AVOX_SKIP_GATE=1 git push` 跳过），ctest 与播放矩阵都不卡提交也不卡推送 —— 改完按需自己跑上面的命令，发布流由 CI 跑（`.github/workflows/release.yml`：ctest + 离线子集，失败即整体失败）。想在本机推送前顺手过一遍：`AVOX_GATE_TEST=1 git push`（再加 `AVOX_GATE_FULL=1` 走含网络用例的全量，需本机 ZLM）。

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

## 文档治理 <a id="documents"></a>

避免「文档越来越多、新旧并存、AI 要分辨」的现状漂移机制: **一份事实只在一个权威源维护, 其余用链接引用, 禁止复制粘贴**; 每篇 `.md` 顶部前几行带状态头, 让读者(含 AI) 一眼判定时效。

- **状态头格式**: `> 状态: <取值> · 上次核对: YYYY-MM-DD · 权威源: <相对路径或 ->`
  取值: `有效`(当前权威) / `已落地`(代码已实现) / `施工记录`(实施日志) / `进行中`(未定稿) / `已废弃` / `归档`。
- **修正就地改**, 升级**勿另存 `xxx_v2.md`**——多份并存是漂移根源, 历史可追 git。
- **过期即归档**到 `doc/archive/`(标 `归档`), 不删、不留在正文混淆现状。
- 索引文件(INDEX.md / plan/README 等)只放链接不放事实。
- **门禁**: pre-push 跑 `script/doc_check.py --strict`, 拦截**本次改动** `doc/**/*.md` 的缺状态头/失效链接/多版本命名; 存量文档仅体检不阻塞。手动全量体检: `python script/doc_check.py`。

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
