# avox_tempo —— 音频变速不变调 (SoundTouch)

> 状态: 已落地(Windows; 其余平台待库产物与装载验证) · 上次核对: 2026-09-23 · 权威源: ->

目标: `IMediaPlayer::speed` 的音频侧从「重采样变调」升级为 **tempo 变速不变调**，五端(Windows/Android/iOS/macOS/Linux)统一生效。
依赖模式照抄 avox_remote↔libsmb2：源码仓拉到同级 `D:\Work\github\soundtouch` → `script/soundtouch/` 预编译入库仓 `avc_library` → `cmake/FindSoundTouch.cmake` → 插件 `find_package` 条件编译，**缺库自动跳过**（未装插件 = 行为与现状一致）。

## 落地记录 (2026-09-23)

- **已落地**: T1 库链(windows 产物 `/MT` 静态 2.4.1) · T2 核心三件(`IAudioTempo.hpp`/`audioTempoHub`/`ARenderTask` tempo 分支+PTS 换算) · T3 插件本体(Windows DYNAMIC) · T5 构建脚本四件套(windows/android/apple/linux, 仅 windows 实跑)。
- **L1 套件 13/13 全绿** (avox-test `l1_avox/tempo` + `tempo_regress.py`): 三条红旗已修, 见下「红旗修复记录」。
- **验证**: avox.dll+avox_tempo.dll 编译过 · ctest 2/2 · `samples/functest/tempotest` 探针(rate=2.00 零错误) · 降级路径(挪走插件 dll: 明确报缺位, 旧变调逻辑照常) · avox-test 离线播放矩阵。
- **遗留**: Android/iOS/macOS/Linux 库产物未编、装载未验(脚本就位); `doc/player/core/播放器时间.md` 现状待翻转。

## 红旗修复记录 (2026-09-23, 套件 3 红旗→全绿)

1. **tempo.dsp.latency** — `SoundTouchTempo::latencyMs()` 把 `SETTING_INITIAL_LATENCY` 的样本数当 ms 返回(4401@48k)。修: `lat*1000/sampleRate` → 91ms。
2. **tempo.pipe.pitch (tap 断流/音频静音)** — 根因不在 tempo: `AVSource::setSpeed` 对任何 speed>1 **预判进入 I 帧模式**(为 zlmediakit 服务端 trick-play 设计), ffmpeg 文件源包流不随倍速变, 预判只落得 ARenderTask 静音车道 + 静音车道硬灌时钟(pos 锯齿), 且退出依赖 P/B 包抵达 singleVideo, 视频消费链背压时长期滞留。修: 新增 `AVSource::bSpeedAble`(IOParseZM::onSpeed 置 true, IOParseFF 置 false), I 帧模式预判仅对 `speed>1 && bSpeedAble` 生效。文件源 2x 从此走 tempo 通道。
3. **tempo.pipe.dynspeed (换档时钟失准)** — 两层: ① `ARenderTask` tempo 分支不更新 `framePts`, 跳变检测每帧误触发→节奏基准失效(已修: tempo 分支同步记录输入域 framePts)。② `collectStatus` 的 AV 对齐看门狗在 tempo 下失真——音频解码轴按 speed×墙钟结构性领先视频轴, ioDiff 恒超阈值误关同步, 滞后的视频时钟接管进度(2x 长播 pos 塌方)。修: 新增 `MediaPlayer::bTempoPlayback`(cmdSpeed 闩锁, close 复位), 生效期间跳过 ioDiff 看门狗, 对齐仍由视频 computeDelay 跟音频主时钟保证。已知边界: zlm 活源 + tempo 时看门狗同样跳过(源跳变依赖轨内跳变检测兜底)。
4. 附带: 套件宿主 PipeCtx 补离屏面(`setOffSurface(yuv420P)`, seektest 同款)——无消费面时视频管线背压爬行放大 ②③ 症状。


## 背景与现状

- speed 通知链现成: `MediaPlayer::setSpeed(src/avox/player/MediaPlayer.cpp:824)` → `AVTrack::setSpeed(AVTrack.cpp:162)` → `AudioTrack::onSpeed(AudioTrack.cpp:61)` → `ARenderTask::speed(ARenderTask.cpp:47)`；时钟按 speed 缩放(`Clock.hpp:37-53`)不动。
- 音频侧现状 = `FFResample` 重采样到 `sampleRate/speed`(`src/avox/audio/ARenderTask.cpp:126-137`)：同内容塞更少样本按原速率播，**音调会变**。四端 `AudioOutput::speed()` 全是空实现。`ARenderTask.cpp:125` 已有预留注释「在这可以应用速度变化如 SoundTouch」。
- 无 bitstream 直通(AC3/DTS 也先解成 PCM)，不存在「直通流没法变速」的例外路径。
- 选型: **SoundTouch 2.4.1**(LGPL-2.1，WSOLA，纯 C++ 带 SSE/NEON，建议域 0.25–4x、0.5–2x 最佳)。
  - 源码已就位 `D:\Work\github\soundtouch`(codeberg，HEAD f738b11 2026-04-19，活跃维护；codeberg 直连偶发 schannel 断连，重试可过，备选 SourceForge git)。
  - 否决备选: ffmpeg atempo(需去掉五端 `--disable-avfilter` 重编重发 ffmpeg)；signalsmith-stretch(谱域 CPU 高，armv7 有压力)；Sonic(音乐劣化明显)。

## 架构

```
MediaPlayer::setSpeed ──现成通知链──► ARenderTask
                                        │ speed!=1 且插件在位
frameQueue ────────────────────────────►▼
  (40ms桶)              [IAudioTempo tempo 处理] ─► audioRender->render ─► 五端设备
            插件缺位 ──────────► 走现有变调重采样 = 降级(行为与现状一致)
```

- SoundTouch 代码**只存在于插件**；核心只加三件: 接口 + hub + 消费点。
- 分层原则: 40ms 桶重组(`AudioTrack::onDecode`)、`nextPts` 采样推进、`bCheckfail` 数据量匹配全在 tempo **上游**且基于源 PCM，一律不动。

## T1 库依赖链(libsmb2 模式)

- 源码: `D:\Work\github\soundtouch`，钉 tag 2.4.1(与 `cmake/FindLibsmb2.cmake` 钉 6.2 同口径)。
- `script/soundtouch/build_windows.py` 镜像 `script/smb2/build_windows.py`: CMake 编静态库(/MT 对齐主仓)，产物
  `avc_library/3rdparty/library/windows/soundtouch/{include/soundtouch/*.h, lib/SoundTouch.lib}`。
- `cmake/FindSoundTouch.cmake` 镜像 `cmake/FindLibsmb2.cmake`: 搜 `$ENV{SOUNDTOUCH_DIR}` + `../avc_library/3rdparty/library` + 仓内 `3rdparty/library`；平台目录 `windows|ios|darwin|linux|android/<abi>`；判 `include/soundtouch/SoundTouch.h`；出 `SOUNDTOUCH_INCLUDE_DIRS` / `SOUNDTOUCH_LIBRARIES`。
- 后续: `build_android.py` / `build_apple.py` / `build_linux.py` 镜像 `script/smb2/` 同名脚本。

## T2 核心接入(无插件行为不变)

1. **接口**: 新建 `src/avox/audio/IAudioTempo.hpp` 纯虚接口 —— `init(AudioDesc)` / `setTempo(double)` / `process(输入帧)` / `receive(出块)` / `reset()` / `latencyMs()`。头放核心仓，插件与核心同源编译，虚表一致；对象由插件 new、核心持 `shared_ptr` 释放(跨 dll new/delete 安全，见 plugins/README 跨 dll 铁律)。
2. **hub**: `AvoxManager.hpp` **类末尾追加** `RegeditFactory<IAudioTempo> audioTempoHub;` —— ⚠ 只能追加末尾，中间插入会挪旧成员偏移，跨 dll 增量构建崩(见 AvoxManager.hpp:101 二进制兼容注释)；先例 `audioProcessHub`(:98)。
3. **消费点** `ARenderTask`(src/avox/audio/ARenderTask.cpp):
   - 持有 `std::shared_ptr<IAudioTempo>`；首次 speed!=1 时 `audioTempoHub.create("soundtouch")` 懒加载插件(同 `createRemoteSource` 首调触发插件扫描；未装返回 nullptr → **降级现有变调路径**)。
   - `:126-137`: tempo 在位 → process/receive 替换「重采样到 sampleRate/speed」；speed==1 旁路零开销(保留 syncAudio 微调原样)；顺序 **tempo 先、syncResample 后**。
   - **PTS 两处换算(最大坑，白盒必锁)**:
     ① 输出块源pts = 基准pts + 输出累计样本/sampleRate×speed，每喂一帧对齐一次防漂移(tempo 后块边界与 40ms 输入帧不再对齐)；
     ② `:164-167` `playPts = framePts - queuedMS` → tempo 生效时改 `framePts - queuedMS×speed`(设备积压 1ms 输出音频 = speed 毫秒源时间；2x + 200ms 积压 = 200ms 同步偏差)。
   - `flush()`(:53) 加 `tempo->reset()`(seek 冲刷，防吐旧窗口内容)；`speed()`(:47) 转发 `setTempo`(SoundTouch 支持在线改速平滑过渡，无需 flush)。
   - **不动**: `:98` `cframeMs=frameMs/speed` 节奏口径(tempo 下输出ms=源ms/speed 天然一致)；`:88` >4x/<0.2 静音护栏; `muxerFrame` 录制拿原速帧。
4. 公开 API 不动: `speed()` 语义在有插件时升级为「不变调」，缺位保持现状；后续如需「保留变调」选项再议。

## T3 插件本体

- 目录 `plugins/avox_tempo/`: `CMakeLists.txt` / `TempoModule.hpp+cpp` / `SoundTouchTempo.hpp+cpp` / README.md。
- CMakeLists: `find_package(SoundTouch QUIET)` 未找到 → `message(STATUS ...)` + `return()`(同 avox_remote 的 openssl 门)；`register_plugin(avox_tempo DYNAMIC LIBS ${SOUNDTOUCH_LIBRARIES})`。
- TempoModule: `AVOX_REGISTER_MODULE(TempoModule, avox_tempo)`；`loadModule` 时 `audioTempoHub.reg("soundtouch", createFn)`。`IModule::loadModule` 语义 = 能力探测，init 失败返 false 运行期降级。
- SoundTouchTempo: `SoundTouch::SoundTouch` putSamples/receiveSamples 推拉模型；采样格式经 swr 转 f32 interleaved(`SAMPLETYPE float`，s16 备选)；SEQUENCE/SEEKWINDOW/OVERLAP 默认起步，现场可调。
- 登记: `plugins/options.cmake` 加 `option(AVOX_ENABLE_TEMPO "build avox_tempo speed-without-pitch plugin (SoundTouch)" ON)`；`plugins/CMakeLists.txt` PLUGINS_DYNAMIC 加 `avox_tempo`。

## T4 场景串联

seek 中/后换速；播放中改速(平滑无爆音)；暂停恢复；非音频主时钟时 syncAudio 微调顺序；3A(`AudioRender::render` 内 initProcess)与 tempo 串联——tempo 在 ARenderTask 层、3A 在 render 内，天然 tempo 先 3A 后；AudioTap 取 render 后音频(=变速后)，ASR/监听类消费者语义备注；直播/低延迟采集(SourcePlayer)默认不启用，仅 MediaPlayer speed 路径。

## T5 五端

| 平台 | 模式 | 说明 |
|------|------|------|
| Windows | DYNAMIC | 先行，T3 打通 |
| Linux | DYNAMIC | PulseAudio, 同 Windows |
| Android | DYNAMIC(so) | avox_remote 同模式: build_android.py(arm64-v8a + armeabi-v7a, NEON)、so 进 jniLibs、宿主 setPluginsDir |
| macOS | DYNAMIC(dylib) | 与 Win/Linux 同形态源码 0 改动; 特例仅在链接: 核心是静态库, 插件不链 avox, core 符号 undefined + `-Wl,-undefined,dynamic_lookup` 由 dlopen 从宿主导出表回绑(宿主 exe 需 `-Wl,-export_dynamic`, register_plugin mac 分支已处理, generatetest/playtest 均带) |
| iOS | **强制 STATIC** | App Store 禁三方动态库: 名字挪 `PLUGINS_STATIC` 编进 libavox(注册宏走 AVOX_ENABLE_STATIC 构造期注册), 源码 0 改动; FindSoundTouch 需 ios 产物 |

## T6 回归与文档

- ctest 白盒(`tests/test_*.cpp` 直编仓内源码): PTS 映射换算 / 输出时长≈输入×1/tempo / 在线 setTempo 无爆音 / reset 后干净 / **缺插件降级路径回归**；SoundTouch 库未就位自动跳过(同 playtest 模式)。
- avox-test 播放矩阵: speed 0.5/1.5/2 用例(推进正常+无报错+听感 A/B)、seek 中换速、长跑 A/V 偏差；`python script/testenv/play_regress.py --offline` 全过。
- 文档收口: 本 README 转使用文档；`doc/player/core/播放器时间.md`「变速不变调候选 SoundTouch」改已落地。

## 风险与对策

| 风险 | 对策 |
|------|------|
| PTS 双处换算错(最大坑) | 白盒单测锁 + 长跑 A/V 偏差用例 |
| 算法延迟 ~50–100ms(SEQUENCE/OVERLAP 窗) | 时钟本就「framePts−积压」模型天然吸收；首帧出声略迟，可接受 |
| LGPL-2.1 静态链入独立 avox_tempo.dll | 与 libsmb2 静态链入 avox_remote.dll 同态势，不新增合规动作 |
| codeberg 拉取不稳 | 源码已落本地；CI/新机器走 SourceForge git 或产物直接入库仓 |
| 录制/直通语义 | muxerFrame 原速帧录制待产品确认；无 bitstream 直通，无此项 |
