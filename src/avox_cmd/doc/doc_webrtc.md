# webrtc — WebRTC 推拉流

Phase 3 — WebRTC 实时通信推拉流。

## 用法

```bash
# 拉流
avox_cli webrtc pull -i https://signaling.server/room/123

# 推流 (摄像头+麦克风)
avox_cli webrtc push -i https://signaling.server/room/123 -device

# 推流 (媒体文件)
avox_cli webrtc push -i https://signaling.server/room/123 -file video.mp4

# 指定 ICE 服务器
avox_cli webrtc pull -i ... -ice stun:stun.l.google.com:19302
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `pull/push` | | | ✅ | 拉流/推流模式 | - |
| `-i` | `--input` | String | ✅ | 信令服务器 URL | - |
| `-device` | | Bool | | 使用设备采集 (推流) | 关闭 |
| `-file` | | String | | 推流文件源 | - |
| `-ice` | | String | | ICE/STUN/TURN 服务器 | - |

## 对应 SDK API

- `createWebRtcPlayer()` → `IRtcPlayer*`
- `createZlTestSdpAgent()`: ZLMediaKit SDP 协商
- `IRtcPlayer`: `setRollType()`, `addIceServer()`, `setSdpAgentOb()`, `open()`, `close()`
- 推流: `ISourcePlayer` + 设备采集/文件播放

## 实现要点

1. 根据 pull/push 选择模式
2. pull: 创建 `IRtcPlayer`，设置 SDP 代理，连接信令服务器
3. push: 创建设备采集源或文件播放源，推送到 WebRTC
4. SDP 协商通过 ZLMediaKit 代理完成
5. 参考: `samples/vulkantest/webrtcpull.cpp`, `webrtcplaytest.cpp`
