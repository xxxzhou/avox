# playmatrix — 播放回归矩阵

> 每次改动后跑一次, 确认**播放**这条链路(拉流 / 解码 / 帧输出 / 录制)没被搞坏。
> 用例表与判定口径只此一份 (`PlayMatrix.hpp`), 各平台 runner 只做宿主适配,
> 因此同一 case id 在五个平台含义一致, 可汇总成一张跨平台表。

## 为什么单独一套

- `tests/`(doctest) 管的是**纯逻辑**, 秒级、任何机器可跑, 但它碰不到 IO/解码/渲染。
- `samples/` 里的播放样例是**手动走查**用的, 参数硬编码、无统一判定与退出码。
- 本目录补的是中间那层: 有明确判定行 + 退出码, 不依赖人眼, 但需要真实流源与解码器。

三者叠加: 改完 → `ctest` 秒级兜逻辑 → **本矩阵兜播放** → 需要看画面再开 samples。

## 用例表 (23 条, 默认跑 22 条)

轴: 取流方式 × 编码 × 解码模式 × 帧/录制输出。**不做全笛卡尔**, 用固定交叉控制耗时。

| 组 | case | 源 | 维度 |
|----|------|----|------|
| A 协议 × 编码<br>(ffmpeg IO + 硬解) | `file-h264` · `file-h265` | 本地 mp4 | 不依赖网络的基准 |
| | `rtsp-h264` · `rtsp-h265` | ZLM | RTSP |
| | `rtmp-h264` · `rtmp-h265` | ZLM | RTMP |
| | `hls-h264` · `hls-h265` | ZLM | HLS 分片 |
| | `ts-h264` · `ts-h265` | ZLM | HTTP-TS |
| B IO 方案对照 | `rtsp-h264-zm` · `rtsp-h265-zm` | ZLM | `AVOX_IO_PLAN=zlmediakit` 对照 ffmpeg9 |
| C 解码模式对照 | `file-h264-soft` · `file-h265-soft` | 本地 mp4 | 软解 (硬解由 A 组覆盖) |
| | `rtsp-h264-soft` · `rtsp-h265-soft` | ZLM | 软解 + 网络 |
| D WebRTC | `webrtc-h264` · `webrtc-h265` | ZLM WHEP 信令 | 独立通道, 不经 IO 方案 |
| E 帧 / 截图 / 录制 | `frame-contract` | ZLM | 离屏 yuv420P packed 契约 + 抽帧 RGBA |
| | `shot` | ZLM | 截图 (关 vulkan, 走平台原生渲染) |
| | `shot-vk` | ZLM | 截图 (离屏 vulkan 路线) — **已知返回 0, 默认不跑** |
| | `rec-copy-h264` | ZLM | 直通录制 (原流拷贝) |
| | `rec-transcode-h264` | 本地 mp4 | 转码录制 + 中途 seek |

`--list` 里带 `(off)` 的即默认不跑的用例, `--all` 打开。

## 判定口径

| 用例 | 判 PASS 条件 |
|------|-------------|
| `*/h264` `*/h265` 拉流 | 15s 内 `playing && fps>0 && pos>1500ms`, 失败自动重开重试 3 次 |
| `webrtc-*` | `connected && firstFrame && fps>0` |
| `frame-contract` | 帧数 ≥10 且 `rowPitch ≥ width`、缓冲容纳整帧, 且抽 2 帧 packed→split→RGBA 的 PNG 非空 |
| `shot` | 进入 playing 且 `screenShot` 取出、PNG 落盘非空 |
| `rec-*` | 产物 ≥8KB (空文件/仅文件头判掉); 转码用例额外走一次中途 seek |

统一输出 `[AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]`, 末尾一行
`case=play-matrix result=... pass=/fail=/skip=`; 进程退出码 0=全过。

## 跑法

```bash
# 一键 (推流 → 跑 runner → 汇总), 需本机 ZLM MediaServer
python script/testenv/play_regress.py

# 已有流源 / 拉另一台 ZLM / 跳过用例
python script/testenv/play_regress.py --no-push
python script/testenv/play_regress.py --host=192.168.68.245
python script/testenv/play_regress.py --skip=shot,webrtc-h265

# Android 真机 (push runner + 依赖 so + 本地源, 再走 adb shell; 主机地址自动取本机局域网 IP)
python script/testenv/play_regress.py --android --serial=<序列号>

# 只列用例表
python script/testenv/play_regress.py --list

# 直接跑 runner (Windows 产物在 build/.../install/*/Release/)
playtest --host=127.0.0.1 --skip=webrtc-h265
```

流源由 `script/testenv/push_streams.py` 提供 (`live/avox264`=H264, `live/avox`=H265);
局域网真机跑时加 `--lan-ip=<本机 IP>`。

## 各平台宿主

宿主源码与用例表都在 `tests/playmatrix/`: `HostMain.cpp` 是通用命令行宿主 (参数解析 +
资源定位), 各平台只是换构建方式; Apple 要出 iOS app, 因此自带宿主。

| 平台 | 位置 | 形态 | 状态 |
|------|------|------|------|
| Windows | `platform/windows/playtest/` | console exe, 无头离屏 | 已实测 22/22 |
| Android | `platform/android/playtest/` | console 可执行, `adb push` + `adb shell` | 待真机验证 |
| Apple | `platform/ios/avoxtest/` | 同一份用例表, iOS app + macOS 无头 CLI | 待 Mac 上编译验证 |
| Linux | 待建 | console exe | 暂不做 |

Android 不出 APK: 判定行只走 stdout, console 可执行 + `adb shell` 就能拿到, 省掉
JNI/Activity/Gradle 一层, 换来与 Windows 完全一致的 runner 契约; 代价是不覆盖 Java
绑定层 (那层由 `platform/android/AvoxJava` testbed 与 Godot 工具箱覆盖)。

## 上机清单 (换一台机器跑)

公共前置三件:

1. **ZLM 服务端**要跑在拉流目标机上 (`MediaServer.exe`, http/80 rtsp/554 rtmp/1935)。
   `push_streams.py` 只推流, 不起服务端。
2. **测试流源**: `python script/testenv/push_streams.py` (推 `live/avox264`=H264 / `live/avox`=H265)。
   真机需 `--lan-ip=<拉流机的局域网IP>` —— 驱动在 `--android` 时自动取本机私有网段地址。
   注意: 装了 VPN/TUN 的机器 UDP 探测会拿到 `198.18.x` 假地址, 驱动已优先枚举私有网段,
   仍不对就显式 `--host=` / `--lan-ip=`。
3. **本地源** (`file-*` 用例): 仓库自带 `assets/video/webrtc_pull.mp4`(H264) 与
   `avox_electron.mp4`(H265); Apple 侧另需 `wall_long.mp4` / `wall_265.mp4`。

| 平台 | 命令 | 前置 |
|------|------|------|
| Windows | `python build_windows.py` → `python script/testenv/play_regress.py` | 无 |
| macOS | `python build_mac.py` → 按 [avoxtest README](../../platform/ios/avoxtest/README.md) 编 → `python script/testenv/play_regress.py` (darwin 自动识别) | SDK install + `wall_*.mp4` + `AVOX_HOST` |
| iOS | Xcode 打开 avoxtest 工程点 Run (app 自跑, 判定上屏 + `Documents/avoxlog.txt`) | 签名 + 本地网络权限 |
| Android | `python build_android.py` → `python script/testenv/play_regress.py --android` | 见下「Android 未通」 |

### Android 目前跑不通 (需先定方案)

console 宿主**能编出来**, 但**跑起来会在渲染阶段挂**: avox 在 Android 把 shader/资源
全走 JNI 的 `AAssetManager`, 而控制台进程拿不到它, 三处都没有回退:

| 位置 | 现状 | 后果 |
|------|------|------|
| `VkShader::loadShaderModule` (`__ANDROID__`) | `assert(assetManager != nullptr)` 后直接 `loadShader(assetManager,…)` | assetManager 恒 null → 断言/空指针, shader 取不到 |
| `VkHelper::loadShader(AAssetManager*,…)` | 只判了 `!asset`, 没判 `!assetManager` | `AAssetManager_open(nullptr,…)` 属未定义行为 |
| `getAvoxPath()` (`__ANDROID__`) | 直接 `return ""` | 即便加了文件回退, 路径会变成 `/assets/glsl/x.spv`, 不存在 |

两条路可选:

- **A 改 SDK (3 处小改)**: `getAvoxPath()` 返回 `/proc/self/exe` 所在目录;
  `VkShader` 在 assetManager 为空时回退文件系统; `VkHelper::loadShader` 补空指针保护。
  改动只在 assetManager 为 null 时生效 (APK 路径行为不变), 但**需真机验证**。
- **B 走 APK**: 复用现有 Godot 工具箱 + `avox://` 深链 (见功能测试矩阵 W2), 不碰 SDK,
  但 runner 契约与其它平台不一致 (判定行来自 logcat)。

另: Android 侧 `libavox_webrtc.so` 未编, `webrtc-*` 用例驱动里已自动跳过并告警。

## 按改动选跑

| 改了哪里 | 跑什么 |
|----------|--------|
| IO / 网络 / 解封装 | 全跑 (A+B 组是重点) |
| 解码器 | 全跑 (C 组 + `frame-contract` 是重点) |
| 帧布局 / 帧契约 | `frame-contract` + `ctest` |
| 渲染后端 | 各平台窗口样例 (headless 覆盖不到 GPU 直通) |
| 录制 / 封装 | `rec-copy-h264` + `rec-transcode-h264` |

## 已知取舍与悬案

- **截图分两条路线**: `shot` 关掉 vulkan 走平台原生渲染 (稳定路线, Windows 实测 PASS);
  `shot-vk` 走离屏 vulkan, `fetchFrame` 返回 0 —— Windows 实测 `VideoRender.cpp:117 check shot:0`,
  Apple 侧见 `doc/test/功能测试矩阵.md` W5 同源问题。修好前 `shot-vk` 默认不跑 (`--all` 可开)。
- **zlmediakit IO 的 PTS 异常**: `rtsp-*-zm` 判 PASS 但 `pos` 报出 1.78e12 ms 量级的绝对时间戳
  (ffmpeg IO 同流为正常相对值)。用例只判 `pos>1500` 所以不受影响, 但这个口径值得单独查
  (此前 `samples/vulkantest/README.md` 记过"用 zlmediakit 拉流 PTS 约 9 倍"的现象, 同一家族)。
- **软解只在 file/rtsp 对照**, 未铺满全协议 —— 控制耗时; 需要时按 `buildCases` 加行即可。
- **转码录制固定软编** (`setHardEncode(false)`): Windows 硬编 h264_mf 对 profile 敏感, 本矩阵只兜
  "播放 + 录制能出正确产物", 不做编码器矩阵。
- **`assets/video/webrtc_pull.mp4` 尾部不完整** (ffmpeg 报 partial file), 但能正常起播, 不影响用例。
- **保留 `samples/` 的角色不变**: 需要肉眼确认画面/需按键/依赖插件与外网的, 仍留在 samples, 不进门禁。
