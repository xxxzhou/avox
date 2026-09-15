# A-6 FFmpeg 9.0.1 换代适配(收口)

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

## 验收

- 回归全绿 + UE 链路走通,即可关闭本项。

## 风险

- 基本无;唯一外溢:minsize 裁剪包**只带 av1 硬解壳、无 dav1d 软解**(`build_ffmpeg.py:102-104`),
  这不是 9.0.1 的坑,但直接限制 a12 的 AV1 软解兜底方案选型,先记在这里。
