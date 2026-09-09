# WebRTC 插件

avox_webrtc 从 `src/avox_webrtc/` 迁为 `plugins/avox_webrtc/` 动态插件，avox.dll 零 webrtc 编译依赖。

## 动机

webrtc.lib ~1GB+ 静态链入 avox.dll → 不需要 WebRTC 时体积浪费。插件化后按需加载。
发布分发用无符号版 `webrtc_nosym.lib`/`libwebrtc_nosym.a`（llvm-strip 剥调试信息, Windows 带符号 313MB → 无符号 84MB），带符号版不进版本管理，仅本机留存用于崩溃符号化。

## 插件边界

| 代码 | 归属 | 理由 |
|------|------|------|
| IRtcPlayer / RtcRollType / RtcConnState / RtpDirection / IRtcPlayerOb / ISignalChannel | 核心 `avox/AvoxPlayer.h` | 纯接口，SWIG 友好 |
| ISdpAgentOb + onRemoteSdp | 核心 `avox/AvoxPlayer.h` | 双向 SDP 交换接口 |
| TestSdpOb + mk_http | `src/avox_zlmediakit/` | HTTP 信令，零 webrtc 依赖 |
| createWebRtcPlayer | 核心 AVOX_EXPORT | AvoxManager 工厂，SWIG 可绑定 |
| createZlTestSdpAgent | 核心 AVOX_EXPORT | 走核心 TestSdpOb |
| addRtcPlayerOb / removeRtcPlayerOb | 核心 AVOX_EXPORT | dynamic_cast<BasePlayer*> cross-cast |
| RtcPlayer / RtcParse / RtcHelper | 插件 | PeerConnection 实现 |
| RtcAudioProcess (3A) | 插件 | 依赖 webrtc::AudioProcessing |
| webrtc_nosym.lib(.a) | 插件链接 | 无符号版, FindWebRTC release 默认优先链接 |

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

## 接口 v2 (扩 UE/Unity/Godot 前固化)

`IRtcPlayer` v2 在纯接口层补齐引擎集成所需能力（实现都在插件）：

| 能力 | 接口 | 说明 |
|------|------|------|
| 连接状态 | `getConnectionState` / `IRtcPlayerOb::onConnectionState` | 映射 PeerConnectionState, 播放线程回调 |
| 首帧 | `IRtcPlayerOb::onFirstVideoFrame` | 隐藏 loading, 解码线程回调 |
| 轨道方向 | `setVideoDirection` / `setAudioDirection` | recvOnly/sendOnly/sendRecv/inactive, 默认 sendRecv; 发送需设置对应源 |
| 推流参数 | `setSendVideoBitrate` / `setSendVideoFps` / `setPreferredVideoCodec` | RtpSender 参数 + SetCodecPreferences |
| 重连 | `reconnect` / `setAutoReconnect(b, maxRetries)` | failed 触发自动重连(3s 间隔), 重连后重新走信令 |
| 统计 | `getFps` / `getLossRate` / `getRttMs` | 帧率本地统计; 丢包/RTT 走 RTCP GetStats(2s 节流) |
| DataChannel | `setEnableDataChannel` / `sendDataChannel` / `onDataChannelMsg` | 二进制; offer 方主动建, answer 方用远端的 |
| 信令通道 | `ISignalChannel` + `setSignalChannel` | 本地 SDP/ICE 自动送出、远端自动回填; 低层钩子 `ISdpAgentOb` 仍可用 |
| 真实 open 结果 | `open()` 返回 PC 创建结果 | 连接本身异步, 看 `onConnectionState` |

关键修复（v1 遗留）：
- `addIceServer` 此前为空实现（永远走硬编码 STUN/TURN 兜底）
- 远端音频此前从不发声（`remoteARender` 未创建且用的是裸 `AudioRender`, 现接 `getDefaultAudioOutput`）
- 对端只发单媒体(纯视频/纯音频)此前永远不 ready（`setRemoteSdp` 时按远端 SDP 修正期望媒体）
- 未设推流源时此前仍 AddTrack（SDP 恒 sendrecv）, 现按 `hasSource()` 决定

### 两种信令用法

```
// 1. 低层钩子(现状): onLocalSdp 里自己换远端 SDP 再 setRemoteSdp/addIceCandidate 回填
player->setSdpAgentOb(ob);
player->open();

// 2. 信令通道: 实现 ISignalChannel(HTTP/WS/WHIP), 交换全自动
player->setSignalChannel(channel);
player->open();   // 内部 connect, onLocalSdp 时 sendLocalSdp, 远端消息走 onRemoteSdp/onRemoteIceCandidate
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
4. 各平台用 `script/webrtc/` 下对应脚本：Windows `.\webrtc_build_windows.ps1 release`，mac/ios/android/linux 用同名 `.sh`
5. Windows 脚本剥符号产 webrtc_nosym.lib 时，llvm-strip 会把 `boringssl_asm` 成员（GNU as 产出 COFF）的**整个符号表**剥掉（非仅调试信息），导致链接报 `ChaCha20_ctr32_*`/`vpaes_*` 等 73 个 LNK2019——脚本已内置"剥后删除 asm 坏成员、回插原始成员重建索引"的修复；编完拷 `obj/webrtc*.lib` 到 avc_library `build/windows/release/`

## 平台注意

- **Android**: 需要 libwebrtc.jar，初始化调 `ContextUtils.initialize()` + `InitAndroid()`，AudioDeviceModule 用 `CreateJavaAudioDeviceModule`
- **iOS**: 链接 libwebrtc.a 时 Xcode Other Linker Flags 加 `-ObjC`（Category 方法不被丢弃）
- **MSVC**: 运行库 MT/MTd；Debug 用 RelWithDebInfo；不开启 FORCE:MULTIPLE（运行时解码数据异常）

## ZLMediaKit 做 WebRTC 服务器

需单独编译 ZLMediaKit（开 ENABLE_OPENSSL），依赖 openssl + libsrtp2（需 ENABLE_OPENSSL）。

## 参考

- [WebRTC 入门](https://zhuanlan.zhihu.com/p/624357784)
- [WebRTC 全平台编译指南](https://pixpark.net/c706dac8.html)
