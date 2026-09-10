# avoxtest — Apple 平台功能矩阵测试

同一份 `avoxtest.mm` 编出两种形态:

- **iOS 真机 app**: 全屏 Metal 渲染, 启动即自动跑矩阵, 判定逐条上屏
- **macOS 无头 CLI**: 离屏解码, 跑完退出, **退出码 0=全过** (可接 CI)

## 测试矩阵 (默认: LAN 模式)

依次拉 Windows ZLM (192.168.68.245, `script/testenv/push_streams.py` 提供流源):

| 用例 | URL | 验证点 |
|------|-----|--------|
| ios-rtsp-h264 | rtsp://:554/live/avox264 | ffmpeg9 IO + RTSP over TCP + 硬解 |
| ios-rtsp-h265 | rtsp://:554/live/avox | 同上, HEVC |
| ios-rtmp-h264 | rtmp://:1935/live/avox264 | RTMP 拉流 |
| ios-hls-h265 | http://:80/live/avox/hls.m3u8 | HLS 分片 + HEVC |
| ios-ts-h264 | http://:80/live/avox264.live.ts | MPEG-TS |
| ios-webrtc-h264 | http://:80/index/api/webrtc?...type=play | WHEP 信令 + WebRTC 拉流 |

判定: 15s 内 `playing && fps>0 && pos>1.5s`, 失败自动重开重试 3 次;
webrtc 为 `connected && firstFrame && fps>0`。

## 运行模式

| 触发方式 | 模式 |
|----------|------|
| 无参数 | LAN 矩阵 (上表) |
| `AVOX_MATRIX=loop` | 进程内回环: 本进程起 ZLM 服务端(rtsp/rtmp/hls) + ffmpeg 解封装节流推流(mk_media) + 回拉, 不依赖局域网。需 `-DAVOX_FFMPEG_INCLUDE=<ffmpeg头>` 参与编译, 且同目录放 `wall_long.mp4`(300s h264) / `wall_265.mp4`(h265), 生成命令见下 |
| 启动参数含 `://` | 单 URL 拉流模式 (如 `./avoxtest rtsp://...`) |

日志: iOS 写 `Documents/avoxlog.txt` (devicectl 取) + UDP 直发 192.168.68.219:9999;
macOS 全走 stdout。

```bash
# 测试源生成 (仅 loop 模式需要)
ffmpeg -f lavfi -i testsrc2=size=480x270:rate=15:duration=300 -c:v libx264 -crf 30 -pix_fmt yuv420p wall_long.mp4
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=15:duration=60  -c:v libx265 -crf 30 -pix_fmt yuv420p -tag:v hvc1 wall_265.mp4
```

## macOS 跑法

```bash
cmake -B build \
  -DAVOX_LIB_DIR=<SDK安装>/aarch64/Release \
  -DAVOX_WEBRTC_LIB=<avc_library>/build/darwin/release/libwebrtc_nosym.a
cmake --build build
./build/avoxtest            # LAN 矩阵, echo $? 看结果
AVOX_MATRIX=loop ./build/avoxtest   # 进程内回环
```

需要 `avox.bundle`(glsl shader) 在可执行文件同目录 (Vulkan/MoltenVK 渲染路径用):
从 SDK 安装目录拷贝即可。

## iOS 跑法

```bash
cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DAVOX_LIB_DIR=<SDK安装>/Release \
  -DAVOX_WEBRTC_LIB=<avc_library>/build/ios/release/libwebrtc_nosym.a \
  -B build-ios
open build-ios/avoxtest.xcodeproj   # 选真机, Run (自动签名)
```

启动后自动跑矩阵, 判定留在屏幕上。注意:

- **macOS 26 / iOS 对重编译的二进制可能要求重新授予「本地网络」权限**
  (设置 → 隐私与安全性 → 本地网络), 未授权时局域网连接报 *No route to host*
- 首选由 Xcode GUI 点 Run (SSH 下 codesign 访问不了钥匙串)
- CLI `xcodebuild` 只能做编译验证 (签名阶段会 errSecInternalComponent, 属预期)

## 已知问题

- SDK 的 mk_api 编译时未开 `ENABLE_WEBRTC`, 进程内回环的 webrtc 用例会 FAIL
  (LAN 模式的 webrtc 走独立部署的 ZLM 服务端, 不受影响)
- `AVOX_IO_PLAN` 环境变量可切换拉流 IO 方案 (ffmpeg/zlmediakit), 默认 ffmpeg
