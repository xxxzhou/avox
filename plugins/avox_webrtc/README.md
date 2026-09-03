# WebRTC 插件

avox_webrtc 从 `src/avox_webrtc/` 迁为 `plugins/avox_webrtc/` 动态插件，avox.dll 零 webrtc 编译依赖。

## 动机

webrtc.lib ~1GB+ 静态链入 avox.dll → 不需要 WebRTC 时体积浪费。插件化后按需加载。

## 插件边界

| 代码 | 归属 | 理由 |
|------|------|------|
| IRtcPlayer / RtcRollType | 核心 `avox/AvoxPlayer.h` | 纯接口，SWIG 友好 |
| ISdpAgentOb + onRemoteSdp | 核心 `avox/AvoxPlayer.h` | 双向 SDP 交换接口 |
| TestSdpOb + mk_http | `src/avox_zlmediakit/` | HTTP 信令，零 webrtc 依赖 |
| createWebRtcPlayer | 核心 AVOX_EXPORT | AvoxManager 工厂，SWIG 可绑定 |
| createZlTestSdpAgent | 核心 AVOX_EXPORT | 走核心 TestSdpOb |
| addRtcPlayerOb / removeRtcPlayerOb | 插件 AVOX_PLUGIN_API | dynamic_cast<RtcPlayer*> |
| RtcPlayer / RtcParse / RtcHelper | 插件 | PeerConnection 实现 |
| RtcAudioProcess (3A) | 插件 | 依赖 webrtc::AudioProcessing |
| webrtc.lib | 插件链接 | 体积巨大，按需加载 |

## 数据流

```
核心层 avox.dll                    插件 avox_webrtc.dll
┌─────────────────┐               ┌──────────────────┐
│ ISdpAgentOb     │◄──onLocalSdp──│ RtcParse         │
│ IRtcPlayer      │               │  (PeerConnection)│
│ TestSdpOb       │──onRemoteSdp─►│                  │
│ createWebRtc    │  createRtc    │                  │
│ Player()        │  Player()────►│ new RtcPlayer()  │
└─────────────────┘               └──────────────────┘
```

## SSL 依赖

- **静态链接**(iOS/WASM)：BoringSSL（webrtc 自带），与 avox.dll 同二进制无冲突
- **动态链接**(Win/Android)：BoringSSL 在 avox_webrtc.dll 内，avox.dll 内 Agent 用 OpenSSL，天然隔离
- CMake: `AVOXOptions.cmake` 按 `AVOX_DLL_TYPE` 自动切换

## 跨 DLL 子类化 → AVOX_EXPORT

插件子类化 avox 内部类（RtcPlayer 继 BasePlayer 等），Windows DLL 边界要求对应类有 AVOX_EXPORT，
否则符号不在 avox.lib → LNK2001。已加 AVOX_EXPORT 的类见 `src/avox/` 下对应头文件。
其他插件只实现接口（IModule/IRawSource 等）不需要。

## WebrtcModule 注册

`WebrtcModule::loadModule` 向 AvoxManager 注册：
- `rtcPlayerHub.reg("webrtc")` → `new RtcPlayer()`
- `audioProcessHub.reg("webrtc")` → `new RtcAudioProcess()`
- `rawSources.regInitFunc(WebRTC, "webrtc")` → `new RtcParse()`

## 编译 WebRTC

1. 新建目录如 `D:/Work/webrtc`，clone depot_tools
2. 用 `script/webrtc/gclient_webrtc.bat` 同步源码
3. 切到目标分支（如 m138 → branch-heads/7204），gclient sync -D
4. 用 `script/webrtc/webrtc_win_gen.bat` 生成工程，`webrtc_win_build.bat` 编译 webrtc.lib

## 平台注意

- **Android**: 需要 libwebrtc.jar，初始化调 `ContextUtils.initialize()` + `InitAndroid()`，AudioDeviceModule 用 `CreateJavaAudioDeviceModule`
- **iOS**: 链接 libwebrtc.a 时 Xcode Other Linker Flags 加 `-ObjC`（Category 方法不被丢弃）
- **MSVC**: 运行库 MT/MTd；Debug 用 RelWithDebInfo；不开启 FORCE:MULTIPLE（运行时解码数据异常）

## ZLMediaKit 做 WebRTC 服务器

需单独编译 ZLMediaKit（开 ENABLE_OPENSSL），依赖 openssl + libsrtp2（需 ENABLE_OPENSSL）。

## 参考

- [WebRTC 入门](https://zhuanlan.zhihu.com/p/624357784)
- [WebRTC 全平台编译指南](https://pixpark.net/c706dac8.html)
