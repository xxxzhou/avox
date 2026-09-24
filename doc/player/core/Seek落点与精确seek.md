# Seek 落点闸与精确 seek

> 状态: 已落地 · 上次核对: 2026-09-24 · 权威源: -


超长 GOP / 无真 IDR 片源的 seek 冻结与黑屏已修复；seek 落点闸改为三档入口；新增 `mp.seek.precise` 精确 seek 选项（默认关）。

## 背景：seek 后画面冻结 / 黑屏数秒

高清修复版重封装片源（极空间 NAS http mkv 等）seek 后：旧版永久冻结，中期版本黑屏约 5 秒后恢复且 `Missing reference` 刷屏。根因链：

1. 修复版转码常用超长 GOP（≥20s）甚至**全流只有 open-GOP 恢复点、没有真 IDR**（实测玉蒲团 mkv 扫 3.7 分钟内容全是 nal:1，无一个 nal:5）。
2. seek 落点在 GOP 中间或落在恢复点上，解码器从缺参考状态起解 → Frame num gap 螺旋持续 0 帧 → 帧队列空 → 冻结。
3. 防呆放行后的缺参考垃圾包逐包软解 → 黑屏数秒 + 错误刷屏。

二分实测 ≥09-03 即存在，非回归。

## 修复清单（五处，联动生效）

| 位置 | 改动 |
|------|------|
| `VDecoderTask::flush` | 新增 `bResetCtx`：flush 后解码线程**整体重建 codecCtx**（hw 道 `avcodec_flush_buffers` 清不掉 POC/frame_num，旧态毒化解码） |
| `FFDx11Decoder::onAttachContext` | 设备复用：重建 ctx 时挂已有 hwBuffer 不重建设备（重建会让渲染侧 setDevice 旧 device 与解码新 device 分家，共享纹理互拷失效） |
| `FFVDecoder::onPreDecoder` | `AV_CODEC_FLAG_OUTPUT_CORRUPT`：容忍坏帧输出到自愈（方案 A，VLC 同款） |
| `IOParseFF::seekTo` | h264/h265 不武装 `bWaitKeyframe`（容器 KEY 闸与 AVSource nal 级闸双重扣帧）；RM 等其余编码保留 |
| `AVSource::processPacket` | seek 保护期音频持有到首个视频 I 帧（防音频先跑画面冻的错轴体感） |

## IDR 闸三档入口（`AVSource::singleVideo`）

seek 落点后按优先级选入口：

1. **组内真 IDR（nal:5）**：截断到 IDR 起步，最干净入口（原有）。
2. **容器关键帧组（open-GOP 恢复点）直接作入口**：按 I 帧放行。依据：解码器 seek 时已整体重建（干净 POC）+ OUTPUT_CORRUPT，恢复期短花屏可容忍（VLC 同款）。全流无真 IDR 的修复版片源只能走此档。
3. **落点在 P/B 中间（索引坏）**：丢弃直到下一入口；丢弃是 IO 速度远快于解垃圾。`kSeekIdrDropMax=5000` 防呆防真无关键帧流永久黑屏，逐包日志节流（首 5 包 + 每 500 包）。

判据：`containerKey`（ffAvoxPacket 带入的容器 KEY 标记，在 h26x 被 nalu 语义覆盖前留存）。mkv 索引关键帧=封装器认定的可 seek 点（含恢复点），可信；mpegts 全包标 KEY 的容器在无 IDR 时会走第 2 档容忍花屏，可接受。

## 精确 seek：`mp.seek.precise`（默认关）

| 关（默认） | 开 |
|------|------|
| 落点 I 帧即播，画面从 I 帧起（快） | 从落点 I 帧静默解码丢弃到目标位才显示（准） |

实现：

- `VideoTrack::seekDiscardPts`：onDecode/onDecodeGpu 入队前丢 `pts < 目标` 的帧（restamp 前判；B 帧重排乱序安全），首个 ≥ 目标帧自解除；flush 复位。
- `AVSource::preciseSeekPts`：processPacket 音频分支丢目标位前的包（必须在 alignPacketPts 之前，其尾部 dispatch 才是下发点）；NOPTS 包放行（`AVOX_NOVALID_PTS=INT64_MIN` 会误吞且闸永不清）。
- `MediaPlayer::cmdSeek`：seek 成功后武装各 videoTrack 与 ioSource；**compateIoTime 的 bSeeking 解除逻辑零改动**（丢弃期 demux 位置回落天然满足"见过落点"判定，首帧渲染时钟到位即解除）。

接入：`setOption("mp.seek.precise", "1")`；CLI `avox_cli play -precise`。

已知边界：

- 长 GOP 片源精确 seek 有解码 burst 代价（20s GOP 软解约 1~3s 黑屏等待，VLC/mpv 同款）。
- 容器索引稀疏致落点已超过目标时（往回退不动），丢弃条件不成立，即从落点播——容器索引能力上限，非缺陷。

## 验证账（2026-09-24）

- **判别铁证**（2.rmvb，同一次 seek `landed:10010 target:12969`）：快进模式首帧 pts=10010（落点 I 帧）；精确模式首帧 pts=12971（目标+2ms）。判据用 `-log-file` 里 logDecode 首帧 pts——**截图文件名的 pos 在 bSeeking 期被钉在目标值，不能作为落点判据**。
- playtest `file-h264-seek` PASS（seek_ms≈125，seek_adv_frames 86/15，dx11 硬解）。
- 离线回归 48 pass（4 个 dav 用例失败为无关既有问题）；玉蒲团验证 `recovery keyframe as entry after 0 drops`、Missing reference 0 条；panvox 深度 seek（62min）实测确认。
