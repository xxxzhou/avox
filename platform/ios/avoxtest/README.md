# avoxtest — Apple 平台功能矩阵测试

同一份 `avoxtest.mm` 编出两种形态:

- **iOS 真机 app**: 全屏 Metal 渲染, 启动即自动跑矩阵, 判定逐条上屏
- **macOS CLI**: `--win` 出窗口 (CAMetalLayer 画面 + 判定横幅, 同 iOS 口径), 缺省
  离屏, 跑完退出, **退出码 0=全过** (可接 CI)

## 测试矩阵 (默认: LAN 模式)

用例表来自 [`tests/playmatrix/PlayMatrix.hpp`](../../../tests/playmatrix/PlayMatrix.hpp)
—— 与 Windows/Android 宿主**同一份**, 因此同一 case id 各平台含义一致。22 条覆盖:

协议 × 编码 (file/rtsp/rtmp/hls/ts × h264/h265)、IO 方案对照 (ffmpeg vs zlmediakit)、
硬解·软解、WebRTC (h264/h265)、帧契约、截图、直通·转码录制。
用例清单与判定口径见 [tests/playmatrix/README.md](../../../tests/playmatrix/README.md)。

端点默认指向 Windows 上的 ZLM (`192.168.68.245`, 由
`script/testenv/push_streams.py --lan-ip` 供流), 可用环境变量
`AVOX_HOST` / `AVOX_RTSP_PORT` / `AVOX_RTMP_PORT` / `AVOX_HTTP_PORT` 覆盖。

本地文件用例 (`file-*`) 需要 bundle(macOS 为 CWD) 下有 `wall_long.mp4`(h264) 与
`wall_265.mp4`(h265), 生成命令见下方「测试源生成」; 缺了这几条会判 FAIL。

判定: 拉流 15s 内 `playing && fps>0 && pos>1500ms`, 失败自动重开重试 3 次;
webrtc 为 `connected && firstFrame && fps>0`。

## 运行模式

| 触发方式 | 模式 |
|----------|------|
| 无参数 | LAN 播放回归矩阵 (共享用例表, 默认 192.168.68.245) |
| `AVOX_MATRIX=loop` | 进程内回环: 本进程起 ZLM 服务端(rtsp/rtmp/hls) + ffmpeg 解封装节流推流(mk_media) + 回拉, 不依赖局域网。需 `-DAVOX_FFMPEG_INCLUDE=<ffmpeg头>` 参与编译, 且同目录放 `wall_long.mp4`(300s h264) / `wall_265.mp4`(h265), 生成命令见下 |
| 启动参数含 `://` | 单 URL 拉流模式 (如 `./avoxtest rtsp://...`) |

注: 回环模式仍走自己那套精简用例 (h264 协议子集 + webrtc), **未接共享用例表** —— 它有
独立的端口/推流节奏; 做每次改动的门禁请用默认 LAN 模式。

日志: 判定行与 **SDK 日志** (经 `setLogAction` 桥入) 全部落
`Documents/avoxlog.txt` (devicectl 取; macOS 在 `~/Documents`), 口径对齐桌面宿主的
`pm_log.txt` —— 人或大模型只读这一个文件即可复判, 无需盯屏幕。另 UDP 直发
192.168.68.219:9999 (无监听属正常)。`--win` 时判定行同时滚动在窗口横幅上。

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
./build/avoxtest --win      # 出窗口: 画面 + 判定横幅
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

- **macOS 无头 CLI 必须挂在存活会话里跑** (前台 ssh / Terminal 窗口 / GUI):
  `nohup` 脱离会话后, macOS 26 会**静默拒绝**该进程的本地网络访问 —— 报
  *No route to host*, 但**不弹窗、不进 TCC、设置「本地网络」列表里也不出现**,
  极易误判成权限没授。判别实验 (09-11, M2): 同一二进制 50s 内会话内 probe=0 /
  脱离后=-1; nc 恒通属系统二进制豁免, 不能用来证伪。跑法: ssh 前台挂住整个矩阵
  时长 (~10min), 或在 Terminal 窗口里跑
- 重编译后 (新 cdhash) 需要重新进一次存活会话; iOS 真机侧才是「本地网络」权限
  弹窗那套 (设置 → 隐私与安全性 → 本地网络)
- 首选由 Xcode GUI 点 Run (SSH 下 codesign 访问不了钥匙串)
- CLI `xcodebuild` 只能做编译验证 (签名阶段会 errSecInternalComponent, 属预期)

## 说明

- webrtc 架构: 客户端走独立的 libwebrtc (avc_library 产物), 服务端在独立部署的
  ZLM 服务端 —— mk_api 本身**不需要** ENABLE_WEBRTC。因此进程内回环模式
  (AVOX_MATRIX=loop) 不含 webrtc 用例能力属设计使然; LAN 模式的 webrtc 用例
  走独立 ZLM 服务端, 不受影响
- `AVOX_IO_PLAN` 环境变量可切换拉流 IO 方案 (ffmpeg/zlmediakit), 默认 ffmpeg
