# 病族案卷 · seek 后

> 状态: 有效 · 上次核对: 2026-09-28 · 权威源: -
> panvox-play skill 的案卷分册: SKILL.md §4 只放一行式索引, 签名对上后再来本文读根因/判据/验收。
> 配套: `../SKILL.md`(流程与日志判读) · `病族-open.md` · `病族-播放中.md`

- **seek 落尾/越尾钳尾先分两态**: 健康 = 短暂 buffering 后 `io complete`/completed(短片钳尾常态, 0927 夜巡 133 片); 病态 = 滞留 buffering 无 completed, 三形态:
  - ①尾部段 `seg fetch seek misland got:-541478725(AVERROR_EOF)` 重试转 `read frame failed EIO` 源被闩死 → **已修 1bcc35a(0927)**: 钳尾 seek 后 demuxer 越界读(want 恒 256KB 段起点 + got=EOF 实锤), 取段 10 次空转重试后 wrapReadAt 把文件尾事实报成 EIO 当真故障; 修 = 越界段快速失败 + wrapReadAt 按文件大小分真伪回 EOF + 寻位前清 eof 闩。
  - ②落尾帧队列空恒 buffering → 已定谳另一宗: 视频轨 `invalid`(`add video track: invalid-…` / `VideoTrack unsupported codec -1`)= 引擎不认的老编码(SVQ3 .mov 实锤), 纯音频代打而音频轨仅数秒, seek 越过音频终点后零供给; 已补 SVQ3 映射修(bd09de9, 0927)。**见同族签名先查 `add video track` 是否 invalid**; 其余非 invalid 片 = ①形同根, 已随 1bcc35a 愈合(0928 二刷)。
  - ③改后缀 FLV fallback 落点后 demux 无下文(683)→ 与①同根(misland 闩), 已随 1bcc35a(0928 二刷愈合)。
  - **0928 二刷 16 片 15 片走 completed; 残留新形态(未修立案): 258 resume-seek 即 onComplete 闩 → `avformat_seek_file failed(-1)` → `cmdSeek failed` → 帧队列空恒 buffering(600s 锚因 playing 窗仅 33ms 未发出), 非 misland 族, 无对应 commit**。
- **fMP4 分片(stub moov + moof×N, 迅雷类)只播首片即假 EOF → 已修 c5f70ed(0928)**: 顶层有 `moof` 时预扫须放弃 AVSEEK_SIZE 谎报早退(39aa4aa 只对非分片多 mdat 安全) —— 谎报把文件大小钉在第一分片 mdat 末尾, mov 头扫描只索引首分片, 播完首片即假 EOF(229s 片 ~10s 止)。判据 = **本地/HTTP 同止且零 read 错误**(probe 播本地同片 35s 无 EOF 对照)。同 commit 另钉 EOF 兜底门闸: 尾包在途清 `bIOComplete` 后一次性闩无人重报 → 空转/恒 buffering; 修 = 局部 bEof 升格 `AVSource::bAtEof` + renderFrame 门闸(atEof && !bSeeking && 四队列全空)补 Complete。
- **缩图/录制 muxer 会话 init 失败后 close 跳0崩 → 已修 f76db0e(0928)**: IOMuxerFF 失败态(零流 `write_header` EINVAL / `avio_open2` 败)未释放 fmtCtx, onClose 仍调 `av_write_trailer` → interleave_packet 槽 NULL → call 0。签名 = dump **RIP=0** + 栈 `MediaMuxer::close→IOMuxerFF::onClose→av_write_trailer`; 触发面 = 缩图腿(视频 desc `invalid-…` 即零流)。修 = 失败态 releaseOutput + trailer 前 bInitStreams 闸。取证全文 `docs/reports/2026-09-28-mux-trailer-null-call.md`(panvox 仓)。
- mkv seek 卡 ~3 分钟(matroska 内部前向扫描兜底) = avio error/eof 闩残留毒死尾部 Cues 解析 → 已修 8d61674(wrapSeekCb 清闩 + 读满文件尾短段入库)。真机签名(0927 Android): `state playing→seek` 后 86s 零状态转换, 伴 `[FF][matroska,webm] Read error at pos ≈ filesize-5KB` + seg-diag `read failed n=AVERROR_EXIT(-1414092869) httpEof=1`; 同片同源新引擎 seekstorm PASS 且 open 6ms vs 旧机 9.5s。**先核 banner(§1.0) —— 病根常是"修复没上到该设备的引擎"; 但 banner 是 configure 烤的会失真, 判新旧行为以库指纹/md5 为准**。
  - **残宗未修(0927 深夜, 换新库后 Android 真机仍现)**: 8d61674 只除"永久闩死"; 一次 cmdSeek 的 interrupt 窗口(AVERROR_EXIT = avio interrupt 自家中止, avio.c:521)连环杀两段关键读 —— ①在飞取段(seek 恰落在 prefetch 读中; WiFi 300ms/段必撞, localhost 微秒级撞不上 = 桌面 A/B 全绿的盲区); ②重连后的尾部 Cues 段(读到 225KB 再被杀, pos 恒 = filesize-5KB) → matroska 退化为从早期 cluster 线性前扫(手机表现为 seek 20s+ 无进展)。修复域 = IOParseFF seek 暂停/interrupt 窗口与 fetchHttpSeg 重连的握手(777/809/1186/1331/1377 注释链), 动手前先想清 seekTo ack 纪律。缓解(已给用户): 关「记住播放位置」免开播自动 seek 撞窗口。
- 改后缀 FLV(无 keyframes 索引) seek 转 25~46s(顺序整扫, 耗时 = 目标偏移÷实测吞吐可精算对账) → 已修 flvEstimateSeek 字节估算直跳(四点位 0.4~0.6s; IOParseFF bFlvFastSeek 门, 0926 落地, `git log -S flvEstimateSeek` 自查); 家族 = 所有迅雷改后缀网络 FLV。**关键认知**: flvdec 播放期自建流索引但 flv_read_seek 恒不用(委托 avio_seek_time 需 pb->read_seek→ENOSYS); 单点 seektest 绿但拖动仍冻 → 杀伤在 ack 等待 200ms 撞 http 重连退避(1s 不可打断)→ 快速 seek 门(要求 IO acked)全关退回整扫 + 并发撕 demuxer → 已修 ack 上限 1.2s(IOParseFF seekTo); 回归用 seekstorm 探针(samples/functest)。
- seek 进片尾 buffering 死锁(剩余 < 垫子 2s 恢复门闸永不满足 + avio pb->error 闩致 EOF 永不到, 读线程 60 次/s 空转) → 已修 a501667(门闸 bIOComplete 逃生 + cmdSeek 复位 + 清闩)。
- seek 后播一会卡一会、pos 半速爬 + 周期 buffering(rip 音频 mega-chunk 致视频/音频双区读相距数百 MB, 每段一请求 ~150ms 建连) → 已修 af17043(顺流预读 8 段 + LRU 16MB); 多段拼装 rip 的常态形态, 会再现。keep-alive(persistent)在极空间实测有害, 默认保持 0。
- Mac seek 冻画只有声(pos 照走)、日志 `[vt] BadDataErr(-12909)` 风暴数百条 = VT 吃到坏 NAL(seek 落点簇拆出的全零 PPS)后 session 永久 wedge; FFmpeg 车道宽容坏 NAL 故 Win/软解无恙 → 韧性已修 88b0135(连击 → 扣帧等 IDR 重建会话, 修复后表现为 ≤1GOP 自愈卡顿); 根因(PPS 视图清零)在 a02 施工域待协调。
- Mac **反向** seek 没反应/进度条弹回旧轨继续走 = VT flush 不丢在飞帧(3 帧旧帧)+ VideoTrack restamp 把新流逐帧改戳回旧时间轴 → **未修**; 短路探针已实锤机制(/tmp/probe0926), 修法 = flush 世代号 + restamp 对落后游标首帧重锚(归 a02)。
- seek 后长冻(GOP≥20s IDR 稀疏, 落点非 IDR 放行缺参考) → **未修**(a02 门闸域); 另有渲染侧时钟钉死变体(pos 冻在目标、IO 背压读不到 EOF)。
