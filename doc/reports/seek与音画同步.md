# 播放引擎排查案例集 ②：seek 与音画同步

> 状态: 有效 · 上次核对: 2026-09-30 · 权威源: 记忆库(memory/)+各仓 git 提交号
> 汇编 2026-09-19~09-30 的 seek/时间轴/时钟案例; 每条含根因与修复提交号

一句话背景: seek 族的病灶高度收敛——**落点、闩(latch)、游标**三类; 音画同步族
则几乎全是「单位/截断/标签信源」问题。本篇按这两条主线组织。

## 1. seek 落点与门闸

- **RM seek 落点校验** (`5dede2a`, 09-23): seek 后直读校验 + 变体重试
  (-1 min=target / 视频流 BACKWARD; **FORWARD 恒落尾勿用**) + stash 回灌。
  panvox「落片头」实为生产续播 seek(0) 覆盖探针, 非引擎乱落。
- **超长 GOP seek 冻画** (09-24): GOP≥20s 时落 GOP 中段→无参考解码→帧队列空冻。
  用户拍板方案 A(C+A: 落点吸 IDR+容忍花屏到下一 IDR 自愈, VLC 同款);
  `AV_CODEC_FLAG_OUTPUT_CORRUPT` 已开; 下一针=FFVDecoder receive 侧
  Frame num gap 螺旋。判据: 501 包全 nal:1 无一 IDR。
- **VR 鱼眼 seek 卡死** (09-24): 门闸洗清——有 stss 的 mp4 BACKWARD 必落
  sync sample; 真嫌犯是未提交的 IDR 闸对「SEI 前置包」误丢首 GOP。

## 2. avio 闩(latch)族—— seek 后卡死/冻死第一大家族

FFmpeg avio 的 pb->error / eof 闩一旦置位, 后续读恒失败, 重试全撞闩。
**本族四案例同根不同症**:

| 案例 | 症状 | 闩的来源 | 修复 |
|---|---|---|---|
| seek 尾部 buffer 死锁 (`a501667`, 09-26) | 尾段 buffering 永不满足 | 门闸无 EOF 逃生 + intr 停后闩仍吐 AVERROR_EXIT | 门闸 bIOComplete 分支 + cmdSeek 复位 + 清闩 |
| 4.4GB mkv http seek 卡死 (`8d61674`, 09-26) | seek 后前向扫 3 分钟 | avio_seek 小距离分支不清闩 → 毒死尾部 Cues 解析, 满尾段被当失败丢弃 | wrapSeekCb/fetchHttpSeg 清闩 + 读满文件尾短段入库 |
| 极空间 seek 双区饿死 (`af17043`+`b8fc33a`, 09-26) | seek 后卡顿/冻死 | checkAvccPacket uint32 长度链回绕死循环(FC FF FF FF) + 打断窗 EIO 闩×小距离 seek 清不掉 | 预读 8 段 + 读前清闩 + seek 后兜底 + close 不报假 onError |
| 极空间 mp4 打开卡死 (09-26, 三刀待拍板) | open 永久 buffering | 服务端提前收尾连接 → filesize 被污染 ~4.4MB → EOF 闩雪崩 | ①seek 败清闩 ②EOF 未到真尾重建通道 ③guard 自然解除 (未落) |

**家族规律**: 凡「seek/open 后永久 buffering、重试无效、日志出现重复同一条读失败」
→ 先查闩。清闩要落在**每次可能置闩的失败路径之后**, 不是只在 seek 入口。

## 3. 无索引/特殊容器 seek

- **改后缀 FLV 无索引** (`6200b4b`, 09-26): 90 分钟 FLV 顺序扫要 25~46s。
  修=flvEstimateSeek(索引直跳+字节估算 corrMs 修正, 四点位 0.4~0.6s,
  门=flv+网络+IOacked) + ack 200ms→1.2s(否则拖动风暴冻死: http 重连退避吃掉
  ack→整扫 48s+撕 demuxer)。坑: flvdec 自建索引自不用 / 尾随 prevTagSize
  才自洽 / post0 残帧是伪影勿追。
- **VT 硬解 seek 吃坏数据永久 wedge** (09-26, `88b0135` 已落): 极空间直链 mp4
  seek 落点簇拆出全零 PPS → VT session 永久 wedge(-12909 风暴 767~1080 条);
  FFmpeg 车道宽容坏 NAL 故 Windows/软解无恙。韧性=检测 baddata 后重建 session;
  自愈停顿 ≤1GOP(dropped:220≈9s)易被用户当「停止」。根因(PPS 清零)在 a02 域待协调。
- **VT 反向 seek 静默失效** (09-26 未修): VT flush 不丢在飞帧 + restampFramePts
  钳位劫持 → 3 帧旧帧锚死游标, 新流全改戳旧时间轴。修法=flush 世代号 +
  restamp 域归 a02 协调(探针实测短路后立即恢复)。

## 4. 时钟/节拍/速度

- **某剧集 MKV 声画差 1s** (`86fb877`, 09-25): 窗口循环节拍被 fps 标签
  (23.976→43.5ms) 钳得比 PTS 步进(41.7ms) 慢 → 视频钟 0.959x 每秒亏一帧,
  renderDiff 贴 1000ms 看门狗线锯齿。两行修复, 用户真机确认。
  **口径: 节拍真值只能来自各自 PTS 链, 标签不可信。**
- **TrueHD 直链 18 倍慢放** (`96e205b`+`c3f92b0`, 09-26): 两刀——MLP/TRUEHD
  枚举合一恒取 MLP 车道零输出+逐包 5ms 消费上限堵死 demux; 真因=音频采样游标
  ms 整数截断(TrueHD 40 采样=0.83ms→0)冻结 → 1s 重锚锯齿 → 视频快进/冻结循环,
  **声音照常是最大迷惑点**。修=µs 游标; 顺带根治 AAC 每包 0.2ms 截断的轻度同病。
- **4x→8x 切速慢** (`75b2e33`, 09-23): cmdSpeed 升速时 seek 冲队列 + AVTrack
  统一 200; demux 领先量被音频队列(21-24ms/包)钳制。
- **MediaPlayer delayMs 定标** (09-26): 四处消费点(恢复门闸 OR/直播自动变速/
  A-V 对齐判定/日志); 首帧不吃它; 用户拍板简版全局 1000→2000(`2a200e9` 已推),
  按 ioDuration 分点播/直播的精细方案未做(直播可 mp.delay.ms 调回)。

## 5. 音轨与字幕轨

- **多音轨混声** (09-24): cmdReady 启动全部音轨 → 双 AAC 混声; 只改 start 不够
  (pushPacket 满会堵 IO), 必须 onPacket 路由门控; `setAudioTrack` 已落地
  (`ba43db7`), aconfig 缓存补投切轨。
- **TrueHD [mlp] 刷屏** (09-25): 文件无恙, 刷屏真凶=某片 TrueHD 轨打开
  (media_info.json at/via 重建时间线定位); N 包无 major sync 静音停喂刀法待拍板。
  字幕无 5 条上限(AVOX_MAX_TRACK=4 是音/视频槽, 6 条 srt 全注册全可选)。
- **变速不变调** (09-23): avox_tempo 插件 + SoundTouch 2.4.1, Windows 全绿;
  其余平台待库产物。
- **[FF][h264] no frame! 刷屏过滤** (09-24): onFFLog 日志桥精确文本过滤
  (vsnprintf 去尾换行后等值才丢)。

## 6. RM/RMVB 时钟生态(专题)

- **NOPTS 透传** (09-22/23): 源层音频垃圾 pts 归一 NOPTS + AudioTrack nextPts
  采样推进 + collectStatus 改比 decodeOut(不与 ioTime 混比——启动幻影教训)。
- **cook 音频坏声两层**: WASAPI 缓冲抽干爆音(用户修 full() 水位, loopback 实测
  12.65s 连续) + 源层 40ms 合成 pts 灌水 1.72×(cook 实为 23.2ms/包, nextPts 修)。
  教训: loopback 逐位 MSE 验证; 假峰=两个「看起来像」的尖峰叠加。
- **rv40 B 帧跳变**: 帧入口钳位 nominalFrameMs; best_effort 走 dts 链的探针铁证。

## 7. 排查手法索引

| 手法 | 用途 |
|---|---|
| renderDiff 对看门狗线作图 | 声画漂移可视化(锯齿=重锚, 恒偏=截断) |
| 探针比对「标签值 vs PTS 步进」 | 节拍类漂移定源 |
| 单包字节级解释(采样数×单位) | 慢放/快放倍率怪症 |
| seektest 多点位落点统计 + post0 残帧标记 | 落点类(残帧是伪影勿追) |
| 8 连远跳 + 小距离 seek 风暴 | 闩类回归锤 |
| PANVOX_AUTOPLAY/RESUME 环境变量 | 一键复现到指定点位 |

## 8. 案例补充细节

- **seek 尾部死锁双病灶完整口径** (a501667, 未推送): 门闸侧「尾<delayMs 永不
  满足」与 avio 侧「intr 停了闩仍吐」互相掩护; 探针 5/6 PASS 验证; 遗留 30s
  selfcheck 稀疏 GOP 冻画属**渲染侧时钟钉死**(另一病, 另案)。
- **极空间 seek 风暴验证口径**: 8 连远跳(501 包量级) + 小距离风暴锤零死亡后
  才算修复; 单点 seek 通过不算数——闩类的复现依赖**历史路径**。
- **外链长片 µs 格截断两刀**: 丢 ctts B 帧流 POC 重建按 33ms 格每帧欠 0.367ms
  → VT 车道透传包 pts 吃满漂移, 每 ~4.5s 静默丢帧(Windows FFmpeg 车道输出走
  dts 链无视包 pts 故无感=平台差异迷惑点)。修=POC 显示格 µs 化(79abb44) +
  NAS 网关掐连接包漏截断补偿(e409b32), 均已推送部署; strings 对 dll 失效用
  `grep -a`。用户接受「能播偶尔抖」基线(9d3b5d3 回退后)。
- **RMVB 音频改造三护栏**: 源层透传 NOPTS + nextPts 采样推进 + collectStatus
  比 decodeOut; 遗留 RTSP ts_offset 与离线回归。
- **delayMs 消费点速查**: ①恢复门闸 OR 项 ②直播自动变速 ③A-V 对齐判定
  ④日志; 2000 在队列约束内安全(视频 8s/音频 4.3s 容量)。
- **[mlp] 刷屏停喂刀法**: N 包无 major sync 即静音停喂(该轨按坏轨处理),
  已给方案待拍板; 素材复现件=avox-test `test_h264_truehd`(8s 刷 579 条)。
- **变速不变调枚举坑**: 枚举成员 AVOX_AUDIO_* 命名对齐坑见施工卡
  plugins/avox_tempo/README.md。

## 9. 未修/待办清单(截至 09-30)

| 项 | 状态 | 下一步 |
|---|---|---|
| VT 反向 seek 静默失效 | 未修 | flush 世代号 + restamp 域归 a02 协调 |
| mac VT seek 全零 PPS 根因 | 根因在 a02 域 | PPS 清零源定位 |
| seek 冻画 FFVDecoder Frame num gap 螺旋 | a02 施工区 | 移交话术已备 |
| 极空间 EOF 闩雪崩三刀 | 方案已定待拍板 | ①seek 败清闩 ②重建通道 ③guard 解除 |
| TrueHD 停喂刀 | 待拍板 | N 包无 major sync 静音停喂 |
| 30s selfcheck 稀疏 GOP 冻画 | 另案 | 渲染侧时钟钉死 |
| delayMs 精细方案(点播/直播分档) | 简版已落地 | 按 ioDuration 分档, 直播可调回 |

## 10. 关联记忆文件指针

rm-seek-landing-verify-fix · seek-freeze-window-repro-0924 ·
seek-tail-deadzone-avio-latch-fix-0926 · renamed-flv-noindex-seek-scan-0926 ·
mac-http-bwd-seek-silent-fail-0926 · mac-vt-seek-baddata-freeze-0926 ·
(外链长片跳帧两刀记忆, 涉片名从略) ·
avtrack-queue-1000-speedup-iframe-lag · mediaplayer-delayms-analysis-0926 ·
avsync-1s-fps-label-0925 · truehd-mlp-lane-slow-playback-0926 ·
multi-audio-tracks-play-simultaneously-0924 ·
avox-audio-track-selection-0924 · avox-tempo-plan-0923 ·
fflog-noframe-filter-0924 · rmvb-audio-nopts-passthrough ·
rmvb-cook-audio-underrun-nextpts · 
