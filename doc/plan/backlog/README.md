# avox backlog 功能计划集 (A-1 ~ A-19)

> 状态: 进行中 · 上次核对: 2026-09-23 · 权威源: -


日期:2026-09-15 · 来源:[panvox/docs/backlog.md](../../../../panvox/docs/backlog.md) 第一节「avox 引擎仓」。
本目录是**施工方案层**:每条 backlog 项一份计划,落点到具体文件/行号、任务拆解、验收判据。
**编号与 panvox backlog 对齐,不重排;**本表是 A 项完成状态的权威源**
(2026-09-19 起,代码落地即改),panvox backlog 的 A 项停用打勾、只留任务
描述与对账注记;P 项权威在 panvox `docs/backlog.md`,本仓不复制其状态。**

## 状态总表

| 项 | 优先级 | 里程碑 | 计划状态 | 一句话现状 | 计划 |
|---|---|---|---|---|---|
| A-1 ASS/PGS 链收尾 | P0 | M1 | 施工中(**T3 样式覆盖已落地 09-20**,剩跨平台 STATIC+人工走查) | 信号暴露/延迟接口/候选枚举/ass 编码自愈/PGS e2e/T3 ASS 轨样式覆盖(setAssScale/setAssFont, f2c328e/0936284)均落地;剩 Apple 编译+真渲染取证 | [a01](a01-ass-pgs.md) |
| A-2 秒起播/seek 秒响应 | P0 | M1 | 施工中(T1/T2 完成 09-19) | 埋点+probe 降档落地(本地快档 3-14ms, 保底字段回退);T3 seek 精确化已判定不动(09-19 基线);剩 T4 基线用例 | [a02](a02-fast-start-seek.md) |
| A-3 VP9/WEBM 硬解排查 | P0 | M1 | **三平台硬解已落地(09-19)** | Win 实测 hw 命中/Android 编译过/Apple 待 mac 编译;选型失败自动回退软解;剩真机验证+W38 样片 | [a03](a03-vp9-webm.md) |
| A-4 GPU 直通三平台 | P0 | M4 | 计划就绪(**Windows 分辨率变化已修, 待实测验证, 见 T5**) | Windows 全链通;分辨率变化不跟随已修(换片后旧画布 1:1 落新纹理左上角问题消除);Android AHB 基建在但解码不直出;Apple 缺 Metal 导出 | [a04](a04-gpu-passthrough.md) |
| A-5 可 seek 虚拟文件系统 | P0 | M2 | **T1~T4+§1 全落地(09-20)** | 断链自愈双例 PASS(恢复 5.6s/11.9s);T4 目录缓存(dav+smb 会话级连接复用+dav TTL 缓存)与 §1 authExpired 回调链已落(2cacdef/3cf565d/fd4fc87/2b17164);剩 dav-auth-expired 用例+秒开指标(均 avox-test 侧) | [a05](a05-remote-vfs.md) |
| A-6 FFmpeg 9.0.1 换代 | P0 | 独立 | **适配已完成,待收口(Windows 回归已过, 版本守卫已加)** | 五平台库均 9.0.1、源码已新 API;Windows 离线回归 33/33(09-18),剩四平台回归+UE 链路(待 CI/真机);T6 16KB 页对齐 P2 | [a06](a06-ffmpeg9.md) |
| A-7 avox_subtitle CLI | P1 | M3 | 计划就绪 | sherpa/翻译/CLI 骨架在,批量管线与 SRT 写出为零 | [a07](a07-subtitle-cli.md) |
| A-8 音频软解兜底 | P1 | M3 | **T1/T2 已落地(09-18 提前),待库重编+T3** | 枚举/映射/未知轨株连已修;truehd 待编译机重编部署库,下混(T3)为零 | [a08](a08-audio-fallback.md) |
| A-9 HDR/DV | P1 | M4 | 计划就绪 | tone map 三车道已完成;直通待真机;DV/SDR→HDR 零 | [a09](a09-hdr-dv.md) |
| A-10 蓝光原盘 | P1 | 下版本 | **推迟下版本(09-23 拍板:本版不做 DVD/蓝光)** | 无 libbluray、无 chapter 结构,仅 resolve 注释预留;DVD 原不在 A-10 范围,一并顺延 | [a10](a10-blu-ray.md) |
| A-11 超分/插帧产品化 | P1 | M4 | **只做离线超分(口径 09-19),实时搁置** | 离线管线 P0 已收口(843b4b2),剩 P-17 产品入口集成;实时侧(Anime4K 分档/能力探测/RIFE)维持搁置不补 | [a11](a11-superres-frc.md) |
| A-12 Hi10P/AV1 软解兜底 | P1 | M4 | 计划就绪 | 视频无运行时回退链;AV1 连枚举都没有 | [a12](a12-hi10p-av1.md) |
| A-13 刷新率自适应 | P1 | M4 | 零起点 | vsync 硬编码、无显示模式枚举、无 setter 透出 | [a13](a13-refresh-rate.md) |
| A-14~A-19 增长期 | P2 | M5+ | 摸底完毕 | 见合篇(ohos 零代码/VAAPI 待真机/vision 可接线/写操作零起点/torrent 已完备/webrtc 维持) | [a14-a19](a14-a19-platforms-reserve.md) |

## 与 backlog 原文的出入(以本目录为准)

1. **A-6 降级**:backlog 标「进行中」,实测五平台预编译库全部是 9.0.1(avcodec major 63),源码已全走
   send/receive 与 ch_layout 新接口,构建脚本按 9.0.1 适配完毕 → 剩余工作只有**多平台回归 + UE 链路验证收口**。
2. **A-3 无从查起**:W38 编号在本仓 src/doc/tests 全部零命中,计划第一步是从真实样片复现定性,
   唯一线索是 `FFVDecoder.cpp:130` 关于 vp9/webm 首 GOP 丢关键帧的注释。
3. **A-18 无代码任务**:磁力边下边播已完整实现(TorrentEngine:顺序窗口/readAt/probe 模式/LRU 缓存),
   纯合规问题,backlog「默认不宣传、法务评估」口径维持。
4. **A-15 已不是「计划中」**:FFVADecoder 已实现(VAAPI→CPU NV12,无设备降级),编译过、真机未验,
   code-wiki 等文档写「VAAPI(计划)」属文档漂移。

## 施工约定

- **测试真源 = `../avox-test` 仓**(迁移进行中,见 avox-test/README.md):所有验收用例写到
  avox-test 的 `l1_avox/playmatrix`(C++ 宿主)与 `script/testenv/`;本仓 `tests/` 是过渡副本,不再演化。
- **验收跑法**:`cd ../avox-test && python script/testenv/play_regress.py --offline`;
  C++ 宿主构建桥接(B 类)完成前,playmatrix 用例先在本仓构建环境跑、用例表只在 avox-test 改。
- **接口变更纪律**:动 `AvoxPlayer.h`/`AvoxBase.h` 公开接口的项(A-1 样式/信号、A-5 authExpired、
  A-10 chapter)必须同步 SWIG 绑定口径,并过「跨 DLL 纯虚接口只增不改」约束。
- **慢源豁免**:torrent/smb 的指标口径(起播/缓冲看门狗)单列,不与本地/DAV 混算
  (MediaPlayer 已对 torrent 专门放宽)。

## 依赖与排期

```
M1 收口: A-1 → A-2 → A-3 (顺序做,A-1 已有字幕矩阵底子)     A-6 收口并行
M2 媒体源: A-5 (panvox P-4 刮削硬依赖;已提前启动, 09-19 T1~T3 落地)
M3 AI 字幕: A-7 (panvox P-6 依赖) + A-8
M3.5: A-10
M4 五端+变现: A-4 → A-12 (依赖 A-2 的 probe 保底字段) → A-9/A-11/A-13
M5: A-14;  增长期: A-15~A-19
```

交叉依赖提醒:**A-2 的 probe 降档会掏空 `find_stream_info` 产出的 codecpar 字段**(pix_fmt/fps),
而 A-12 的 10bit 预判、A-3 的解码选型都吃这些字段——A-2 做降档时必须留「保底字段」口径,
否则 M4 的两个计划被 M1 的优化打穿。

## 跨仓对齐规则(2026-09-19 起,替代旧「会话分工现状」)

- 本表是 **A 项完成状态权威源**(代码落地即改);panvox `docs/backlog.md`
  的 A 项停用打勾、只留任务描述与对账注记。P 项权威在 panvox,本仓不复制其状态。
- 跨会话引用对方进度:读对方权威文件本体并核对状态头「上次核对」日期,
  禁止只引旧核账/旧报告(2026-09-19 曾因此把已落地的 A-5 当成未排期)。
