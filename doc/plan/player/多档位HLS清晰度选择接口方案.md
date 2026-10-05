# 多档位 HLS 清晰度选择接口方案

> 2026-10-06 设计定稿草案 v2。证据链与五份死亡现场见 panvox 仓记忆
> `avox-win-engine-crash-1005-iptv`; 复现配方: 任意直播 HLS 频道(多档位
> master), app 内点台, ≤1 分钟。本文只定接口与行为结构, 施工另行排期。

## 1. 背景与证据

直播 HLS master(如 Red Bull TV)声明多档位(6 个视频档, 各带 AAC, 另有
CC 组)。现状行为链与崩溃:

1. ffmpeg hls demuxer 将全部档位注册为流(视频流索引含 4/5);
2. AVSource 仅建部分 video track(实测 4 条) → 轨表与流表不对齐;
3. 未建轨档位的包涌入: MediaPlayer::onPacket 按 index 查轨越界
   (`index out of range:4` 刷屏, MediaPlayer.cpp:432/464);
4. AVSource::alignPacketPts 的 pre_pts 数组写**无界检查**, 同类包直达即
   野指针写(AVSource.cpp, AVOX_20261006-001923.dmp, 0xC0000005);
5. onPacket 丢包路径 free 已损坏内存 → 0xC0000374
   (panvox.exe.25096.dmp 符号化栈: RunTask → AVPacket 回调 → _free_base)。

对照实验: 单档位清单直开(avox_cli 与 app 探针页)零越界、帧正常 ——
「一次只消费一路档位」是被实证的正解; 崩溃触发与 setSpeed 无关
(推迟下发后照崩)。

## 2. 目标 / 非目标

**目标**
- 轨表、流表、收包三者严格一致: 注册的流必须建轨, 未建轨的包必须在
  AVSource 边界被丢弃(修今晚全部死法)。
- 档位即清晰度: 复用既有 ISourceInfo 的 video 轨表作为清晰度列表,
  新增运行时切换接口(镜像 setAudioTrack)。
- 切换不崩: 直播优先 segment 边界加入, 受控重开兜底。

**非目标(P2/P3)**
- 无感切档(新档预取对齐)与 ABR 自动档;
- HLS 独立音轨组(AUDIO rendition)选择。

## 3. 接口设计

### 3.1 列表: 零新增 —— ISourceInfo 即清晰度列表

既有 ISourceInfo::videoSize()/getVideoDesc(i) 就是档位列表(VTrackDesc
含 trackId=流 Id、codecId、VideoDesc 宽高帧率)。清晰度菜单直接消费,
无需新增 VideoQuality 结构。

### 3.2 建轨规则(AVSource)

- master 全档位解析; **按分辨率降序保留最多 4 档**建 video track
  (低档位丢弃, 用户定稿 2026-10-06);
- 每档 muxed 音频建 audio track(既有行为);
- 档位到流 Id 的映射存于 VTrackDesc::trackId(既有字段), 门控据此放包。

### 3.3 切换: 新增 IMediaPlayer::setVideoTrack

```cpp
// 语义镜像 setAudioTrack: 切换激活视频轨(=清晰度档位)。
// index 对应 ISourceInfo::getVideoDesc 的下标, 默认 0=最高档。
// 引擎保证: 切换期间包索引/轨表一致, 状态机不泄漏(opening 至多短暂重入)。
virtual void setVideoTrack(int32_t index) = 0;
```

切换实现(引擎内, 二选一按流型):
- 优先: ffmpeg 流 discard 语义, 于 segment 边界加入新档
  (master 带 INDEPENDENT-SEGMENTS 时干净);
- 兜底: 受控重开至该档 playlist(状态机短暂重入 opening, ≤1s)。

### 3.4 边界守卫(必修, 与选择无关)

AVSource 包出口处: `packet.stream_index ≥ 已注册流数` 或未命中任何
trackId → **直接丢弃, 不得进入任何按索引写/按索引 free 的路径**
(alignPacketPts 的 pre_pts 写、onPacket 的轨查、丢包 free 全覆盖)。
本条独立于档位选择, 防其它流型再踩。

## 4. 状态机与线程纪律

- 重开/切档时旧 io/decode 任务必须 join 完毕后才放新包进门
  (1005 夜「在途包涌入新会话」竞态的教训);
- alignPacketPts 属 open 期扫描, 此窗口内引擎不得接受任何命令式变更
  (app 侧已配套把 setSpeed 下发推迟出该窗口, panvox 98ebe44)。

## 5. shim 与 panvox

- pvx_player 新增 1 符号: `pvx_set_video_track`(quality 列表走既有
  source_info 通道); `panvox_c_api.h` 同步(修饰名导出, 漏加即静默死);
- panvox: 播放页「清晰度」菜单(形态抄音轨菜单, 数据源
  ISourceInfo video 轨表, 显示分辨率); 界面改动按公约走设计稿同步,
  接口落地后另批施工。

## 6. 分期

| 期 | 内容 |
|----|------|
| P1 | 边界守卫 + 建轨 cap4 + setVideoTrack(受控重开兜底) + shim/菜单 |
| P2 | 无感切档(预取对齐) + ABR 自动档 |
| P3 | 独立 AUDIO rendition 选择(复用同套轨机制) |

## 7. 测试与验收

1. Red Bull TV(6 档, INDEPENDENT-SEGMENTS): app 单点即播不崩, 播放 ≥3min
   含至少一次 playlist 刷新; 菜单见 4 档(降序); 换档成功且不崩;
2. avox_cli: `play -i <master> -vtrack <n>` 逐档开播验证;
3. 越界回归: 注入 stream_index 越界包(或用旧现场 URL 时序)零崩溃,
   日志仅见丢弃行;
4. CCTV 类单档流回归零影响; 全量 play_regress 基线不回退。

## 8. 开放问题

- VTrackDesc 是否补 bandwidth(菜单显示码率, 可选, 不阻塞 P1);
- muxed 变体的「档内音轨」与 setAudioTrack 的正交边界(P3 一并定)。
