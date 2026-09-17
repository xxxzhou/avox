# A-6 FFmpeg 9.0.1 换代适配(收口)

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -


优先级 P0(独立排期) · 里程碑:并行 · 计划状态:**适配已完成,仅剩收口验证**
来源:backlog A-6。探查结论与 backlog「进行中」口径不符,以本计划为准并回写那边。

## 探查结论(2026-09-15)

- 五平台预编译库**全部已是 9.0.1**(avcodec major 63):
  `3rdparty/library/{windows|android|darwin|ios|linux}/ffmpeg/include/libavutil/ffversion.h:4`。
- 源码已全走新 API:send_packet/receive_frame(`src/avox_ffmpeg/decoder/FFDecoder.cpp:23,36`)、
  ch_layout(`IOParseFF.cpp:369`、`FFADecoder.cpp:43-46`);grep 全 src 无 av_init_packet/
  avcodec_close/decode_video2 残留。
- 构建脚本按 9.0.1 写(`script/ffmpeg/build_ffmpeg.py:70,76,88,97-101`,含 FFmpeg9 拆分
  d3d11va/d3d11va2 的链接实测记录)。
- 已有版本分支先例:HDR side-data 兼容 ffmpeg>=7.1(`FFVDecoder.cpp:157-158,348-364`)。

## 剩余任务(收口)

- [ ] T1 五平台回归:avox-test 离线子集 + playmatrix 全量,各平台至少一轮
      (重点:硬解路径、HDR P010、录制 muxer)。
- [ ] T2 UE 链路验证:avox-ue 插件仓吃本仓 install 产物跑一遍(backlog 注明 UE 链路依赖)。
- [ ] T3 弱化用法复查:`FFVDecoder.cpp:58` codecCtx->pix_fmt 预设在 9.x 属弱化用法
      (get_format 协商为准),确认软解路径无回归。
- [ ] T4(可选)版本守卫:FFCommon.hpp 加版本门控宏,防未来换代裸奔。
- [ ] T5 回写:panvox backlog A-6 打勾,注记「五平台 9.0.1 已落地,收口回归完成」。
- [ ] T6 Android 16KB 页对齐重出包:build_ffmpeg_android.sh 的 configure 补
      `--extra-ldflags="-Wl,-z,max-page-size=16384"` 重编,覆盖
      `3rdparty/library/android/ffmpeg/lib/`;顺带评估 NDK 26.1 → r27+
      (r27 起 libc++_shared 默认 16K 对齐,并同步 build_common.py 的 AVOX_NDK_ROOT)。
      实测与验收口径见下「Android 16KB 页对齐发现」。

## 验收

- 回归全绿 + UE 链路走通,即可关闭本项。

## Android 16KB 页对齐发现(2026-09-17)

背景:Android 15/16 要求原生库兼容 16KB 页内核(Play 对 targetSdk 35+ 已强制)。
本仓构建注入统一处理(`AVOXPlatform.cmake:36-40`、`build_common.py:309-310` 的
`-Wl,-z,max-page-size=16384`),实测 install/aarch64 全部 .so:

| 库 | LOAD 对齐 | 结论 |
|---|---|---|
| libavox / mk_api / fdk-aac / 全部插件 / godot / unity | 0x4000(16K) | ✅ |
| libavcodec / avformat / avutil / swresample | **0x1000(4K)** | ❌ FFmpeg 四件套 |
| libc++_shared.so | **0x1000(4K)** | ❌ NDK 26.1 自带运行时 |

- **FFmpeg 四件套根因**:`script/ffmpeg/build_ffmpeg_android.sh` configure 未加
  `--extra-ldflags`(API=24),属脚本遗漏 → T6 重出包即解。当前设备内核仍为 4K 页
  (`adb shell getconf PAGE_SIZE`=4096),运行无感;16K 内核设备上这四个库会直接加载失败。
- **libc++_shared 根因**:NDK 26.1 产物本身 4K,随 NDK r27+ 默认 16K → 并入 T6 的 NDK
  升级评估(AGENTS.md 记录的 NDK 26.1.10909125 为升级对象,属全局工具链策略变更)。
- 验收命令:`llvm-readelf -l *.so` 查 LOAD 段 Align ≥ 0x4000(16384)。
- 优先级:P2(当前全量设备 4K 内核无感;出 Play 正式包前必须清零)。

## 风险

- 基本无;唯一外溢:minsize 裁剪包**只带 av1 硬解壳、无 dav1d 软解**(`build_ffmpeg.py:102-104`),
  这不是 9.0.1 的坑,但直接限制 a12 的 AV1 软解兜底方案选型,先记在这里。
