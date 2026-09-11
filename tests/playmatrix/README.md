# playmatrix — 播放回归矩阵

> 每次改动后跑一次, 确认**播放**这条链路(拉流 / 解码 / 帧输出 / 录制)没被搞坏。
> 用例表与判定口径只此一份 (`PlayMatrix.hpp`), 各平台 runner 只做宿主适配,
> 因此同一 case id 在五个平台含义一致, 可汇总成一张跨平台表。

## 为什么单独一套

- `tests/`(doctest) 管的是**纯逻辑**, 秒级、任何机器可跑, 但它碰不到 IO/解码/渲染。
- `samples/` 里的播放样例是**手动走查**用的, 参数硬编码、无统一判定与退出码。
- 本目录补的是中间那层: 有明确判定行 + 退出码, 不依赖人眼, 但需要真实流源与解码器。

三者叠加: 改完 → `ctest` 秒级兜逻辑 → **本矩阵兜播放** → 需要看画面再开 samples。

## 用例表 (26 条, 默认跑 25 条)

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
| | `shot` | 本地 mp4 | 截图 + **图像客观质量** (关 vulkan 走原生渲染) |
| | `shot-vk` | ZLM | 截图 (离屏 vulkan 路线) — **已知返回 0, 默认不跑** |
| | `rec-copy-h264` | ZLM | 直通录制 (原流拷贝) |
| | `rec-transcode-h264` | 本地 mp4 | 转码录制 + 中途 seek |
| F 无vulkan直取<br>(车道B) | `yuvout-h264` | 本地 mp4 | 硬解直出 **nv12** 类型契约 (DX11 staging / Metal readback) |
| | `yuvout-h264-soft` | 本地 mp4 | 软解 cpuIn 零拷 packed 视图 (交付解码格式) |
| | `rec-transcode-novk` | 本地 mp4 | 无vulkan转码录制 (`pushFrame` 非vk分支, 车道B前该分支不存在) |

`--list` 里带 `(off)` 的即默认不跑的用例, `--all` 打开。

## 判定口径

| 用例 | 判 PASS 条件 |
|------|-------------|
| `*/h264` `*/h265` 拉流 | 15s 内 `playing && fps>0 && pos>1500ms`, 失败自动重开重试 3 次 |
| `webrtc-*` | `connected && firstFrame && fps>0` |
| `frame-contract` | 帧数 ≥10 且 `rowPitch ≥ width`、缓冲容纳整帧, 且抽 2 帧 packed→split→RGBA 的 PNG 非空 |
| `yuvout-*` | 帧数 ≥10 且 packed 契约过, 且**首帧类型 = 期望值** (`yuvout-h264` 判 `nv12`, `-soft` 判 `yuv420P`); 类型不对 = 车道断裂或硬解回退软解, 都 FAIL |
| `shot` | 取到图 + PNG 落盘非空 + **不是废图** (见下节): 灰度均值 12~243、标准差 ≥6、最常见颜色占比 ≤95% |
| `rec-*` | 产物 ≥8KB (空文件/仅文件头判掉); 转码用例额外走一次中途 seek |

统一输出 `[AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]`, 末尾一行
`case=play-matrix result=... pass=/fail=/skip=`; 进程退出码 0=全过。

stdout 全程镜像进日志文件 (默认产物目录下 `pm_log.txt`, `--log=` 覆盖), 判定行 + SDK
日志都在; 事后(人或大模型)只读这个文件即可复判, 无需盯控制台。宿主 main 之前的插件
注册几行不落盘, 不影响判定。

## 画面质量怎么自动判（三刀）

"需要人看的"那一类, 多数既不需要人、也不需要大模型。按代价从低到高分三刀:

### 第一刀 · 客观像素统计（已做, 确定性, 秒级, 可进门禁）

`shot` 用例现在不只判"能不能取到图", 还判"这图是不是废的": 截图后抽样算
灰度均值 / 标准差 / 最常见颜色占比, 命中任一条即 FAIL ——

| 症状 | 判据 |
|------|------|
| 黑屏 | 灰度均值 < 12 |
| 白屏 | 灰度均值 > 243 |
| 纯色 / 无细节 | 灰度标准差 < 6 |
| 一整块同色 / 卡帧 | 最常见颜色占比 > 95% |

实测输出: `grab=1 bytes=152050 luma=37.1 std=51.5 top=0.65 1524x726`。
渲染链路的回归里绝大多数坏图就是这四类, 几行算术就能定性, 比调模型快几个数量级且
完全可复现 —— **这是它进得了门禁的原因**, 且已随离线子集进了 CI。

### 第二刀 · 大模型做语义判定（未做, 非确定性, 不建议进门禁）

真正需要"看懂画面"的: 人脸像不像、水印干不干净、AI 超分出图质量、字幕位置对不对。
截图落盘后可以交给 VLM, 但有三个前提:

1. **别问"这图正常吗", 要问"和参考图有哪些差异"** —— VLM 擅长找差异, 不擅长给正确性背书
2. **要结构化输出**（JSON: `pass` / `reason` / `confidence`), 别要一段散文
3. **固定 rubric + temperature 0 + 按图片 hash 缓存**, 否则门禁会抖

所以它的定位是**夜间 / 手动跑的诊断层**, 不是每次改动都要过的门禁。

### 第三刀 · 人工兜底

人不再"全看一遍", 只看前两刀标了不确定的少数。

### 产物落地约定（为第二刀留的接口）

截图统一落 `<outdir>/<prefix><case>.png` (默认前缀 `pm_`), 用例失败时保留、
成功时覆盖。要接 VLM 时直接读这个目录即可, **不用改 runner**。

## 跑法

```bash
# 一键 (推流 → 跑 runner → 汇总), 需本机 ZLM MediaServer
python script/testenv/play_regress.py

# 离线子集 (不需要 ZLM): 本地文件硬解/软解 + 截图质量 + 转码录制, 供 CI / 无流源时跑
python script/testenv/play_regress.py --offline

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
playtest --host=127.0.0.1 --win        # 出窗口看画面; macOS 同样支持 (判定横幅上屏)
```

`--offline` 已接进 CI: `.github/workflows/release.yml` 的 `Run playback regression
(Windows, offline subset)` 步骤, 紧跟 ctest 之后, 失败即整体失败。离线子集含
`yuvout-h264-soft` / `rec-transcode-novk`; **`yuvout-h264` 不进 CI** —— 它严格判 `nv12`,
CI 虚机没有 GPU 视频单元会软解回退而误报, 只在真机 / dev 机跑 (它正是ffmpeg9 裁掉
hwaccel 那类回归的哨兵: 车道任何一环断了都会从这里先炸)。

流源由 `script/testenv/push_streams.py` 提供 (`live/avox264`=H264, `live/avox`=H265);
局域网真机跑时加 `--lan-ip=<本机 IP>`。

## 各平台宿主

宿主源码与用例表都在 `tests/playmatrix/`: `HostMain.cpp` 是通用命令行宿主 (参数解析 +
资源定位), 各平台只是换构建方式; Apple 要出 iOS app, 因此自带宿主。

| 平台 | 位置 | 形态 | 状态 |
|------|------|------|------|
| Windows | `platform/windows/playtest/` | console exe, 无头离屏; `--win` 出窗口看画面 | 已实测 22/22 + `--win` |
| Android | `platform/android/playtest/` | console 可执行, `adb push` + `adb shell` | 已实测 21/23 (09-11, 小米 23113RKC6C; 差 `rec-transcode-*`×2, 见下) |
| Android APK | `platform/android/AvoxJava` testbed 的 `PlayMatrixActivity` | SurfaceView 出画面 + 判定横幅上屏 + `pm_log.txt` | 已实测 20/25 (09-11, 小米 23113RKC6C; 5 条均为构建形态所致, 见下) |
| macOS | `platform/macos/playtest/` | console exe, 无头离屏; `--win` 出窗口 (CAMetalLayer 画面 + 判定横幅, `WinHost.mm`) | 已实测 **25/25 + `--win`** (09-11, M2) |
| Apple app | `platform/ios/avoxtest/` | 同一份用例表, iOS app (画面+判定大字上屏) + macOS CLI (`--win` 同样出画面+横幅) | macOS 已实测 **25/25 全绿** (09-11, M2, 含全协议 LAN/WebRTC/车道B); 注意无头进程必须挂存活会话, 见 [avoxtest README](../../platform/ios/avoxtest/README.md) |
| Linux | 待建 | console exe | 暂不做 |

Android 双形态互补: console 判定行只走 stdout, 与 Windows 完全一致的 runner 契约,
测的是**数据通路** (byte-buffer 硬解 NV12 直出 CPU, 即 yuvout 车道); 要看画面用
testbed APK (导航页「播放矩阵」按钮), JNI 入口 `pmRunMatrix` 在
`src/avox_android/PlayMatrixJni.cpp`, 走真实上屏路径 (MediaCodec→Surface), 日志同样
落盘 `Android/data/avox.samples.mediaplayer/files/pm_log.txt` 可 `adb pull` 复判。
宿主进程 stdout 镜像日志 (`pm_log.txt`, `--log=` 覆盖) 三平台 console 通用。

APK 构建 (gradle Debug 变体):

```bash
cd platform/android/AvoxJava
gradle :testbed:assembleDebug       # local.properties 指向本机 SDK; Debug 变体要求 ZLM Debug 产物
```

- Debug 变体下 `FindZLMediaKit` 查 `release/android/Debug`, 缺了会整链编不过
  (`RtcPlayerApi.cpp` 无条件 include ZLM 头) —— 用 `AVOX_BUILD_TYPE=Debug` 跑一次
  `build_common.build_module('ZLMediaKit', …)` 补齐, Release 目录同理。
- gradle 侧 cmake 参数与 `build_android.py` 对齐: `AVOX_ENABLE_WEBRTC/GODOT/TESTS=OFF`。
- 用法: 手机与开发机同网段, 开发机 `push_streams.py` 在推流; 打开 app 点「播放矩阵」,
  判定横幅上屏; 换 ZLM 主机 `adb shell am start -n avox.samples.mediaplayer/.PlayMatrixActivity --es host <ip>`。

09-11 真机首跑 (小米 23113RKC6C) 20/25, 5 条 FAIL 均为形态所致非回归:

| 用例 | 原因 |
|------|------|
| `webrtc-*`×2 | 本构建 `AVOX_ENABLE_WEBRTC=OFF` (与 build_android.py 对齐), `createWebRtcPlayer-null` |
| `rec-transcode-*`×2 | 与 console 相同的已知问题: AndVEncoder CPU 输入零输出 (见上节) |
| `yuvout-h264` | APK 硬解走 Surface 上屏路径, 无 CPU NV12 输出, `frames=0` 属预期; 该用例只对 console (byte-buffer 车道) 有意义 |

真机排查两坑 (均已修): 深链直进 Activity 时没人调 `JNIHelper.initJNI`, assetManager
为空致 vk shader assert 自杀; `Window::close` release 宿主借出的 ANativeWindow 后,
下条用例 `initVkSurface` 拿已解绑窗口崩溃 —— `Window::initSurface` 已补对称 acquire。

## webrtc on Android (插件化接入, 运行链未通)

矩阵默认本地源与推流已全换 `assets/video/test/` 标准测试源。webrtc 走 **插件模型**
(Windows 同款): `AVOX_ENABLE_WEBRTC=ON` (build_android 默认 ON, env
`AVOX_WEBRTC_ANDROID=0` 关) → `plugins/avox_webrtc` 编出 `libavox_webrtc.so` 打进
APK; 宿主把插件 .so 从 nativeLibraryDir 复制到 filesDir/plugins 后
`JNIHelper.setPluginsDir` 交插件扫描加载; `webrtc::InitAndroid`(JVM) 也随之移入
`WebrtcModule::loadModule`, 核心 libavox 不再引任何 webrtc 符号 (AndHelper 只留
纯 JNI 的 ContextUtils.initialize)。

**剩余卡点**: 插件 loadModule 里 `webrtc::InitAndroid` 触发
`Check failed: !g_jvm (InitGlobalJniVariables!)` abort —— g_jvm 已被先行初始化
(疑似 dlopen 链上其它 JNI OnLoad/Init 路径或重复调用), 待定位去重;
修复前 webrtc-* 两用例在 android 仍 FAIL。console 形态 (无 JVM) 继续由驱动按
libavox_webrtc.so 缺失规则跳过。

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
| Android | `python build_android.py` → `python script/testenv/play_regress.py --android` | 真机 + 本机 ZLM |

### Android 现状 (09-11 实测 21/23, 小米 23113RKC6C / Android 16)

console 宿主已跑通: 全部拉流用例 (协议×编码×硬软解×IO方案)、`frame-contract`、
`shot`、`rec-copy-h264`、`yuvout-h264`/`-soft` 全过。console 进程无 JNI env / 无
`AAssetManager`, 为此 SDK 侧补了 console 分支 (均不影响 APK 路径):

| 位置 | 改动 |
|------|------|
| `HostMain.cpp` | 进程无 `JNI_OnLoad`, 启动时手动 `AvoxManager::init()` 注册模块; 退出前 `clean()` (否则 libmk_api 静态析构序倒挂, 退出必 SIGABRT) |
| `Avox.cpp getAvoxPath()` | Android 分支改 `/proc/self/exe` 所在目录 |
| `VkShader` / `VkHelper::loadShader` | assetManager 为 null 时回退文件系统 + 空指针保护 |
| `AndVDecoder::onPreDecoder` | **csd 补 AVCC→AnnexB 转换**: mp4/rtmp 的 config 是长度前缀格式, MediaCodec 解析不了 csd 会静默零输出 (rtsp/hls/ts 带内参数集天然 AnnexB 所以之前能过) |
| `AndVDecoder::onPreDecoder` | console 无 JNI 时 `JniSurfaceTexture` 建不出来, `bOpenglRender` 以 nativeWindow 实际取到为准; byte-buffer 模式请求 NV12(0x15) 并按 KEY_STRIDE/slice-height 补全三平面 (只填 data[0] 会令下游 memcpy(null) 段错误) |
| `AndVEncoder::encode` | 输入按行拷适配 codec 紧凑 buffer (原按对齐 stride 整块 memcpy 必拒收) |

**剩余 2 条**: `rec-transcode-h264`/`rec-transcode-novk` —— AndVEncoder CPU 输入路径
configure/queueInput 全成功但编码器零输出 (c2.qti byte-buffer 输入), 需专门调试;
Android commercial 构建 (LGPL 无 libx264) 没有注册任何 FF 软编, 转码只能走 AndVEncoder。
另 `libavox_webrtc.so` 未编, `webrtc-*` 用例驱动里已自动跳过并告警。

## 按改动选跑

| 改了哪里 | 跑什么 |
|----------|--------|
| IO / 网络 / 解封装 | 全跑 (A+B 组是重点) |
| 解码器 | 全跑 (C 组 + `frame-contract` 是重点); `yuvout-h264` 兼哨兵: 硬解组件被裁/回退软解时它先炸 |
| 帧布局 / 帧契约 | `frame-contract` + `yuvout-*` + `ctest` |
| 无vulkan直取 (车道B) | `yuvout-h264` + `yuvout-h264-soft` + `rec-transcode-novk`; Apple 侧同 id 覆盖 Metal readback |
| 渲染后端 | 各平台窗口样例 (headless 覆盖不到 GPU 直通) |
| 录制 / 封装 | `rec-copy-h264` + `rec-transcode-h264` + `rec-transcode-novk` |

## 已知取舍与悬案

- **`yuvout-h264` 严格判 `nv12`, 无硬解的机器必 FAIL**: 这是故意的哨兵判法 ——
  类型跌回 `yuv420P` 就是硬解回退/车道断裂 (note 会给 `type=... expect=nv12`)。
  无 GPU 的 CI/虚机请走 `--offline` (该用例已在 OFFLINE_SKIP 排除)。
- **本地文件 + copy 直通录制不落盘** (09-11 发现, 待查): 矩阵补用例时对照实验确认
  vk 开关无关 (`rec-copy` file+vk / file+novk 都 bytes=-1), 是 file+copy 组合自身的
  预存问题, 与车道 B 无关; `rec-copy-h264` 走 RTSP 不受影响。copy 的包走 IO 层
  `onPacket`, 不经渲染层, 故车道 B 用转码录制 (`rec-transcode-novk`) 兜非vk分支。
- **`shot` 的质量阈值是经验值**（均值 12~243 / 标准差 6 / 同色比 95%）：能抓住黑屏、
  纯色、卡帧这类典型坏图，但**没做过对抗性验证** —— 比如"画面对但整体偏暗"的合法场景
  可能被误杀。阈值在 `PlayMatrix.hpp::imageLooksAlive`，按实际误报调。
- **截图分两条路线**: `shot` 关掉 vulkan 走平台原生渲染 (稳定路线, 实测 PASS);
  `shot-vk` 走离屏 vulkan, `fetchFrame` 返回 0 —— Windows 实测 `VideoRender.cpp:117 check shot:0`,
  Apple 侧见 `doc/test/功能测试矩阵.md` W5 同源问题。修好前 `shot-vk` 默认不跑 (`--all` 可开)。
- **zlmediakit IO 的 PTS 异常**: `rtsp-*-zm` 判 PASS 但 `pos` 报出 1.78e12 ms 量级的绝对时间戳
  (ffmpeg IO 同流为正常相对值)。用例只判 `pos>1500` 所以不受影响, 但这个口径值得单独查
  (此前 `samples/vulkantest/README.md` 记过"用 zlmediakit 拉流 PTS 约 9 倍"的现象, 同一家族)。
- **软解只在 file/rtsp 对照**, 未铺满全协议 —— 控制耗时; 需要时按 `buildCases` 加行即可。
- **转码录制固定软编** (`setHardEncode(false)`): Windows 硬编 h264_mf 对 profile 敏感, 本矩阵只兜
  "播放 + 录制能出正确产物", 不做编码器矩阵。
- **macOS 无头跑 LAN 全 FAIL 的新手坑**: `nohup`/脱离会话的进程, macOS 26 静默拒绝其
  本地网络访问 (errno 65, 无弹窗不进 TCC), 全部 LAN 用例报 *No route to host* ——
  不是权限没授 (列表里永远找不到它), 把 runner 挂在存活会话里跑即解。
- **`assets/video/webrtc_pull.mp4` 尾部不完整** (ffmpeg 报 partial file), 但能正常起播, 不影响用例。
- **保留 `samples/` 的角色不变**: 需要肉眼确认画面/需按键/依赖插件与外网的, 仍留在 samples, 不进门禁。
