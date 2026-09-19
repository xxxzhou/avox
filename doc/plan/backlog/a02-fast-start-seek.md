# A-2 秒起播 / seek 秒响应

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T1 埋点 + T2 probe 降档已落地; 剩 T3 seek 精确化/T4 基线) · 来源:backlog A-2
做成可量化指标:打开→首帧、seek→首帧 ms。

## 出口判据

1. 打开→首帧、seek→首帧两项指标有埋点、有日志、可从宿主读取(供回归对比)。
2. 本地文件起播显著快于基线(目标值在基线采集后定,先建口径再谈数字)。
3. 优化不破坏探测完整性:10bit/编码类型等选型字段在降档后仍可得(见风险,A-12/A-3 依赖)。

## 现状(代码落点)

- **打开流程单一路径,零降档**:`src/avox_ffmpeg/IOParseFF.cpp:319` avformat_open_input、
  `:331` avformat_find_stream_info(默认参数);probesize/analyzeduration 从未设置,
  无本地/网络分流逻辑。
- **seek 链路已成熟**:cmdSeek(`MediaPlayer.cpp:1534`)→ preSeek 打断读线程
  (`IOParseFF.cpp:614`)→ seekTo(pause/ack 200ms、AVSEEK_FLAG_BACKWARD、
  字节流分支 avio_seek_time,`:621-674`)→ bSeeking 保护期
  (`src/avox/source/AVSource.cpp:559-566`)。优化空间在「精确到目标前最近关键帧」而非打断机制。
- **无任何指标**:`[dbg] ioDbgCount`(`IOParseFF.hpp:37`)是唯一计数,open/seek 路径无
  chrono 打点;首帧判定点在首个成功视频包触发 onVideoDesc(`VDecoderTask.cpp:193-205`)。
- **慢源已有豁免先例**:torrent 开放超时放宽(`MediaPlayer.cpp:1323-1327`)、
  缓冲看门狗 30s(:1357-1360)——指标口径照此单列 smb/torrent。

## 任务拆解

- [x] T1 指标埋点(2026-09-18, `d7ebd1d`):MediaPlayer/IOParseFF 打点,open 阶段拆解
      (open_input/find_stream_info/首包/首帧);seektest 端到端实测 open→首帧 189ms /
      seek→首帧 13ms / IO 分段 0+2ms,与 runner 外部基线吻合;ctest 2/2 + 离线回归 33/33 绿。
- [x] T2 probe 降档分档(2026-09-19, `ed75f49`): 本地索引容器(mp4/mov/matroska/avi/flv,
      无 URL scheme 或 file:)快档 probesize 1MB + max_analyze_duration 1s; **保底字段
      硬要求已实现**: 视频(宽高/pix_fmt/fps)与音频(采样率/声道)任一缺失即恢复默认参数
      再跑一次 find_stream_info; 网络/ts/HLS 保持默认。实测: 本地 mp4 find_stream_info
      3-14ms, 10bit HDR mp4 快档直接得 yuv420P10(A-12 预判字段无损)。
      「跳过 find_stream_info」未做(风险大于收益, 快档+保底回退已达目标)。
- [x] T3 seek 精确化:**2026-09-19 判定「不动」**(avox-test post-T2 基线
      `out/latency/baseline_post-t2-0919.*`): T2 后 first_frame 全面持平偏快
      (file-h264 -21ms / file-h265 -24ms / shot -30ms / probe-field -31ms,
      probe 五例红线全绿); file-h264-seek seek_ms 31→153 判为噪声(双峰序列,
      150ms 轮早于 T2 即存在, seek_land_ms 恒 0)。无可见症状, 关键帧索引 seek
      不做; 用例继续留在矩阵里观察, 若后续版本出现 landing 异常再重启本项。
- [ ] T4 基线与回归:avox-test playmatrix 加延迟用例(本地 mp4/mkv/webm + WebDAV),
      采集基线 → 优化 → 对比报告落 avox-test/doc。

## 验收

- playmatrix 延迟用例输出 ms 指标;本地 mp4 与 mkv 起播对比基线有可解释改善;
  10bit 样片在降档后选型仍正确(联动 a12 验收)。

## 风险与开放问题

- **probe 降档掏空 codecpar**(pix_fmt/fps)会连锁打穿 A-12 的 10bit 预判与 A-3 的解码选型,
  这是 M1 优化打穿 M4 计划的头号交叉点,T2 的保底字段是硬要求。
- ts/HLS 等无索引流降档收益有限,分档别一刀切。
- 指标口径:首帧定义为「渲染回调首帧」而非「解码首包」,与产品口径对齐后再定目标值。
