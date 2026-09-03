# RTSP 点播 Seek（GB28181 录像回放）

## 问题：seek 后包时间完全不可信

seek 后服务器返回的 RTP 时间戳**无法参考**，两种行为都可能出现、且不可预测：
- **顺延**：时间戳不跳，从 seek 前位置继续递增（最常见；日志实证：seek 到 15 秒，包 pts 还在 6 秒连着涨）。
- **跳变**：时间戳跳了，但跳变量 ≠ 真实 NPT 跳变，video/audio 可能还各跳各的。

根因：服务器 seek 时**画面内容**定位到了目标 I 帧，但**时间戳生成器**没重置。内容与 pts 标签脱钩——画面在 20 秒，包 pts 还是 12 秒。

## 所有包时间同源污染

`IO包时间(ntp_ms) → 解码帧pts → 渲染帧pts → clock → getPosition → seek目标`，一条绳上 seek 后全脏。唯一可靠：avox 自己发出的 seek 目标 `pos`（`npt=X` 对应真实 NPT）。

## 方案：在 IOParseZM 重构包 pts（隔离 + 根治）

在包入口（`processPacket` 前）把失真 pts 重写成真实 NPT。**只改 IOParseZM**：本地文件（ffmpeg IoPlan）、直播（不 seek）都不走这条，AVTrack/clock 通用逻辑零改动。

```cpp
// IOParseZM 成员 (video/audio 各一套, 防错位)
int64_t seekAbsTarget = -1;            // seek目标绝对PTS, -1=没seek过
int64_t seekPtsOffset[2] = {0, 0};     // new_pts = pts + offset
bool    seekPtsPending[2] = {false, false};

// seekTo(pos): 已有逻辑 + 锚定标志
seekAbsTarget = pos;                   // pos = 真实目标绝对PTS
seekPtsPending[0] = seekPtsPending[1] = true;

// onPacket (video merger 回调 / audio 分支) 调 rewriteSeekPts:
if (seekAbsTarget >= 0) {
    int track = (视频类) ? 0 : 1;
    if (seekPtsPending[track]) {
        seekPtsOffset[track] = seekAbsTarget - pts;   // 锚第一个包
        seekPtsPending[track] = false;
    }
    pts += seekPtsOffset[track];       // 重构 pts/dts
    dts += seekPtsOffset[track];
}
```

seek 到 20 秒 → 第一个包（不管服务器给 12 还是 25）锚成 20 秒绝对 → 后续包 `= 20 + (该包pts − 锚点pts)`。用**同段流包 pts 增量**（编码器时钟，可靠），不是墙钟。

## 为什么不累积、不用管渲染/IO 差

- **不累积**：每次 seek 把 `seekPtsPending` 置 true，下个包重算 `seekPtsOffset`，增量只在两次 seek 间算。
- **不用管渲染/IO 差**：seek 目标走 `getPosition`（clock 渲染时间），不碰 IO 包；重构后 pts 顺流成渲染帧 pts，clock 基于真实帧（不用墙钟估算，变速/burst 自动跟），buffer 差是正常缓冲，clock 在渲染端天然是渲染时间。
- **顺带修同步**：video/audio 都锚同一 `seekAbsTarget`，`alignPacketPts` 的 5 秒检查也过。

## 误差与限制

- **GOP 粒度**（±1-2 秒）：服务器 seek 到最近 I 帧，每次独立随机，**不累积**。
- **首个包残留**：seek 后第一个包若混入残留，锚点偏一点；cmdSeek 已 flush avox 队列 + 下次 seek 重锚，影响小。
- pts 失真 → 检测不了 GOP 偏差，无法更精。

## VOD 模式（NtpStamp）已删除

曾经用 NtpStamp 的 VOD 模式（让 RTP 跳变透传）处理"时间戳大跳变"，但这流不跳/乱跳，救不了；且开启后旧流反而累积（第一个包之后才吃跳变，锚点过早、offset 过大）。已从 ZLMediaKit 子模块（Stamp/RtpReceiver/RtspPlayer/mk_player）和 avox 层（AVSource/MediaPlayer/IMediaPlayer）、CmdPlay `--vod`、nodejs 封装、Shell 快捷命令全部移除。去掉后旧/新流统一走"顺延 + 第一个包锚定"。

## 代码位置

| 位置 | 作用 |
|------|------|
| `IOParseZM.hpp` | `seekAbsTarget`/`seekPtsOffset`/`seekPtsPending` 成员 + `rewriteSeekPts` |
| `IOParseZM.cpp seekTo` | `seekAbsTarget=pos`，置 pending 标志 |
| `IOParseZM.cpp onPacket` | merger 回调 / audio 分支调 `rewriteSeekPts`（核心）|
| `MediaPlayer.cpp cmdSeek/getPosition` | `updateSeekTime(spts)`、getPosition 优先 clock（已有，不改）|
| `RtspPlayer.cpp sendPause` | seek 命令 `npt=X.00-`（已验证正确，非病因）|
