# 播放引擎排查案例集 ③：网络直链与 NAS 存储

> 状态: 有效 · 上次核对: 2026-09-30 · 权威源: 记忆库(memory/)+各仓 git 提交号
> 汇编 http 直链 / NAS(WebDAV/SMB/BT) / DLNA / ZL 路由的已定谳案例与守则

一句话背景: 网络族的病根大都在「**按本地文件的假设用网络**」——顺序扫描、
skip 即断连、闩不复位、服务端非标准行为。每条含修复提交与守则。

## 1. http 直链三大战役

- **逐 GOP mdat 的 mp4 打开 11 分钟** (`39aa4aa`, 09-25/26): 迅雷/Twitch VOD
  把 mp4 落成 ~9117 个逐 GOP mdat box, FFmpeg mov 顶层扫描每个 mdat 一次
  avio_skip = 一次 http 断连重连(150ms) ≈ 11 分钟开不了。
  修法(IOParseFF, 仅 http 生效): avio_open2 自开通道 + 64KB 预扫(大 moov 才
  1 次 seek) + 4MB wrapper + open 期 AVSEEK_SIZE 谎报 mdat1 末尾(命中 mov.c
  早退不种毒, 索引照建)。实测病理直链 open 503ms(冷缓存 5.5s), 常规 http 零变化。
  **引擎实为 FFmpeg 9.0.1(avformat-63), 曾误标 8**。
  非孤例: 所有迅雷/Twitch 逐段下载 mp4 直链都撞。
- **迅雷拼装音轨锚文件头** (`a0107be`, 09-26): 前 60s=6 字节空 AAC 帧的病态
  交错 + http 每包跨 36MB 重连。修=IOParseFF http 全量 wrapper + 256KB 段缓存
  + 锚点钉住; 接 39aa4aa 多 mdat 谎报早退。
- **mpv 式 4.4GB mkv seek 毒闩** → 见案例集②(avio 闩族), `8d61674`。

## 2. 极空间(NAS)族

- **.torrent 顶 .mkv 扩展名** (09-25): BT 目录里 `.torrent` 顶 `.mkv` 名
  (36KB bencode), 服务器 content-type 按扩展名给 video/x-matroska 纯误导。
  **守则: NAS 直链播不了, 第一件事 curl 看文件头**; 引擎日志
  `EBML header parsing failed` = 拿到的非 mkv, 别往引擎查; 用 avox_cli 实播对质。
- **服务端提前收尾连接 → filesize 污染 → EOF 闩雪崩** (09-26, 三刀待拍板):
  partial×8845 + 同毫秒 EOF×52 + clock-leak seek(0) 死循环; 文件/服务端/限流
  全排除 + Windows 同代码正常 + 二次复现不卡 = 时变。http 干净 EOF 只在
  off≥filesize → 卡死时 httpPb filesize 认知被污染 ~4.4MB; seek 失败路径
  不清闩(HEAD 亦然)重试全撞闩。banner 的 commit_hash 会骗人(dylib 实为别的)。
- **WebDAV 掐流** (09-26, 外链长片): 20 分钟 19117 次连接截断 = 按客户端栈
  掐连接(urllib 死 curl 活); 系统代理 LAN 恒 502(CIDR 不生效)。
  **教训: 对频率惩罚的网关, 请求量是第一约束**; SMB 全链已打通待 A/B 判决。
- **seek 后卡顿/冻死双区饿死** (`af17043`+`b8fc33a`): 预读 8 段修双区饿死;
  uint32 回绕死循环与 EIO 闩见案例集②。

## 3. 容器/路由层

- **ZL 路由不支持纯 http 文件直链** (09-24): ZLMediaKit 假报 success 卡死
  opening。修=bZlStreamUrl 已落地验证(rtsp/hls 仍走 ZL); panvox 原生链路
  (硬解+GPU 直通)亦 PASS, 剩 Dart 层排查。
- **DLNA 录制 ts/mkv 必崩** (09-25, `ae1f7e6`): IOMuxerFF 非 mp4 扩展名漏
  alloc → 空上下文崩(非「extradata 已有」假设)。附带: RVA 导出表映射法
  (无 lib 对符号)。
- **torrent fileIndex 宽容读取** (`7e3be9f`, 09-30): shim setString 下发数字
  选项 getInt 不容 string → 宽容读取。

## 4. HEVC 高码率 NAS 直链降级链

- **HEVC 高码率 NAS 直链三刀** (`2c2444b`, 09-25): 供给窗看门狗(饿等清零, 非首包起算——
  第一版被孤包误杀) + FFDecoder 连续 send 失败 → openFailed 瞬时降级
  (vulkan 3.8s→0.1s) + 回退吸 IDR(RingBuffer::dropUntil+flattener.reset)。
  dx11 全程保住, 首帧 14.9s→6.3s 零 POC 错误; 剩 ~4.6s=IO 线程 TrueHD 参数
  探测占线(avformat_find_stream_info, 潜在刀)。回归: 本地 hw/ctest/离线矩阵全绿。

## 5. 网络族守则(蒸馏)

1. **先 curl/ffprobe 看真拿到的东西**——扩展名、content-type、文件头三样都可能骗。
2. **http 上 skip=断连**: 任何「循环 skip」的容器扫描在网络上是 O(n) 重连,
   先做 wrapper/预扫/谎报尺寸早退。
3. **闩必须在失败路径清**: EOF/error 闩 + EIO 闩 + 自建 guard 三类;
   复位点=读失败后、seek 失败后、close 时。
4. **服务端会撒谎**: filesize 污染、提前收尾、假 content-type、按 UA 掐连接——
   引擎对「EOF 未到真尾」要有重建通道假设。
5. **对频率惩罚的网关**: 单位时间请求数是第一约束, 段缓存/锚点/批量读优先。
6. **回归锤**: 8 连远跳 + 小距离风暴 + 断网中途 + 限速, 四锤过才算过。

## 6. 本地存储误案两则(对照)

- **E:\备份空副本** (09-21): 「(2)」类解压副本可能是 0 字节空壳; 「没画面」先
  ls 看大小+搜同名完好副本; 本机无 ffprobe 时用 Python 解 MOV atom 验流
  (字段偏移极易错 4 字节)。
- **dv-shot/素材核验**: 素材库统一在 avox-test/assets/video/, 素材生成脚本
  `assets/gen/`(gen_dv_l1gate.py 的 code10=64+940·pq 有限量程口径可复用)。

## 相关文档

- mkv-http-seek/avio 闩细节: `seek与音画同步.md` §2
- dav-* 用例既有红: `avox-test` 本地 WebDAV fixture, 与引擎无关

## 7. 案例补充细节

- **39aa4aa 六机制复核**: 上游 hevcdec/mov 的早退路径在 FFmpeg 9.0.1(n9.0.1)
  与 8.0 一致; 纯选项路(ignidx/seekable=0)全实测排除(http.c:2124 ENOSYS 等)。
  病理直链冷缓存 5.5s vs 修复前 ~11 分钟; 常规 http 25ms 原生零变化;
  ctest 2/2。
- **极空间 EOF 雪崩时序证据**: partial×8845 + misland EOF×52 同毫秒 +
  clock-leak seek(0) 死循环; 排除链=文件 hash 干净/服务端日志/限流阈值/
  Windows 同代码正常/二次复现不卡(时变性)。修复刀位: ①seek 败清闩 ②EOF 但
  未到真尾重建通道 ③guard 自然解除——**三刀均未落, 待拍板**。
- **外链长片 SMB 现状**: 共享枚举/挂载全链通(真实共享名含手机号打码), 已挂载
  /Volumes 实测可读; panvox 有局域网发现, 施工剩三小增量(点条目带 IP/枚举
  共享下拉/真实报错)。用户拍板: SMB 治抖与否待 A/B。
- **高码率 NAS 降级链回归口径**: 本地 hw + ctest 2/2 + 离线矩阵 49P+4dav 既有挂
  + 软解, 四路全绿才算过; 「供给窗看门狗」必须饿等清零而非首包起算(孤包误杀
  实锤过第一版)。
- **dlna 录制**: IOMuxerFF 按**输出扩展名**决定 muxer 分配, 非 mp4 路径漏
  alloc → 空上下文; ts/mkv/mp4 三容器录制全绿验证。
- **dav-\* 用例族**: dav-open-list/play-seek/auth-fail/broken-resume/
  outage-resume/auth-expired 六条走本地 WebDAV fixture(28773 端口, avoxtest);
  「4dav 既有挂」指 fixture 环境红(listCode=-8), 与引擎无关, 解读矩阵时先分账。

## 8. ZL/IO 计划速查

| 场景 | IO plan | 说明 |
|---|---|---|
| rtsp / rtmp / hls | ZL 路由 | bZlStreamUrl 门控 |
| 纯 http 文件直链 | ffmpeg + IOParseFF wrapper | ZL 不支持且假报 success |
| WebDAV | IOParseDav | fixture 六用例 |
| SMB | libsmb2 整文件守卫 | AVOX_REMOTE_SMB |
| 本地 disc | IRemoteSource 三段式 | BDMV 枚举/直开 |

## 9. 网络问题「五问」排查模板(蒸馏)

1. 拿到的字节对吗? (curl 文件头/扩展名/content-type/大小)
2. 是 IO plan 走错吗? (ZL 假 success/路由门控)
3. 失败路径清闩了吗? (EOF/EIO/guard 三类闩)
4. 单位时间请求数多少? (对频率惩罚的网关: skip 循环=重连风暴)
5. 服务端行为复现得了吗? (时变服务端: 二次复现+跨机对照定性)

## 10. 关联记忆文件指针

per-gop-mdat-mp4-http-open-stall-0925 · mkv-http-seek-cues-latch-poison-0926 ·
nas-bt-torrent-as-mkv · zlm-http-file-io-plan-fail ·
dlna-record-ts-mkv-crash-fixed-0925 · panvox-http-seek-buffering ·
renamed-flv-noindex-seek-scan-0926(与案例集②交叉)
(注: 部分涉片名的记忆文件名从略, 案例细节以本文各节为准)
