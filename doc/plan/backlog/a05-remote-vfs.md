# A-5 可 seek 虚拟文件系统 (avox_remote)

> 状态: 进行中 · 上次核对: 2026-09-16 · 权威源: -


优先级 P0 · 里程碑 M2(**建议 M1 末期提前启动**:panvox P-4 刮削与源浏览硬依赖)
计划状态:就绪 · 来源:backlog A-5
WebDAV/SMB 统一 range 读 + 缓冲窗口 + seek;直链失效重试、令牌过期回调、目录列表缓存。

## 出口判据

1. WebDAV/SMB 源可播、可 seek,缓冲窗口可配,起播/seek 指标进入 a02 口径。
2. 播放中直链失效(401/403/410/超时)自动重取续播,失败路径有明确错误码。
3. 令牌过期回调(`authExpired`)从引擎抛到产品,产品可弹重授权 UI 后无缝恢复。
4. 目录列表有缓存,二次进入/翻回秒开。

## 现状(代码落点)

- **插件在但薄**:`plugins/avox_remote` 约 2200 行。DavSource(WebDAV,cpp-httplib):
  仅 PROPFIND 一种方法(`DavSource.cpp:392-428`),resolve 拼 `http(s)://user:pass@host` 直链
  (:623-637)交 FFmpeg http 协议播——**DAV 播放没有自有 IO 源**;SmbSource(libsmb2)
  + IOParseSmb:pread 直读、seek 完整(`IOParseSmb.cpp:111-143,317-322,537-575`),
  但单缓冲 256KB(:12-14)无预读窗口。
- **IoPlan 枚举无 http 项**(`src/avox/AvoxMuxer.h:9-13`,只有 none/zlmediakit/ffmpeg/torrent/smb),
  路由在 `MediaPlayer.cpp:1341-1355`——统一 range 读的最大结构缺口。
- **无缓存无重试**:list 每次清空重建、DAV 每请求新建 Client(`DavSource.cpp:399`)、
  SMB 每次 list 独立连接;401/403→authFailed(-3) 就完了,无重试层。
- **半成品契约**:`RemoteCode::authExpired=-4` 已预留(`src/avox/AvoxBase.h:138`,
  注释「UI 重新授权后重建会话」)但无产生者无消费者;`IRemoteSource::refresh(entry,option)`
  预留直链重取(`AvoxBase.h:237-238`)但无人覆写无人调用。
- **观察者只有三个回调**:onOpenResult/onListResult/onListProgress(`AvoxBase.h:160-172`),
  无 onAuthExpired。
- alist/OpenList token 鉴权零代码(DavSource 显式丢弃 token,:322;多处注释提及 alist 但无实现)。
- httplib 请求不可中断(`DavSource.cpp:111-112,356-357`),仓内版本缺三参构造(:396-399 注释)。

## 任务拆解

- [ ] T1 契约设计(先行,半成品收口):authExpired 生命周期定案——DAV/alist 401 产生 →
      `IRemoteSourceOb` 新增 onAuthExpired(动 AvoxBase.h,跨 DLL 只增不改)→ 产品重授权 →
      重建会话;播放中 IO 错误路由回 source 会话 refresh() re-resolve 的通道设计。
- [ ] T2 IOParseDav(新组件,本计划核心):DAV 直链自有 IO 源进 ioSources
      (IoPlan 枚举加项),统一 range 读 + 预读窗口(参照 IOParseTorrent lookahead 模式)+
      seek;SMB 侧对齐同一缓冲口径。
- [ ] T3 直链失效重试:播放中 http 错误分类映射(401/403/410/断流)→ refresh(entry) 重取
      → 带 offset 重开续播;重试策略(次数/退避)可配;alists token 形态鉴权加 Header 注入点。
- [ ] T4 目录列表缓存:会话级连接复用 + 目录树缓存(TTL 可配),秒开指标入 a02 口径。
- [ ] T5 avox-test 用例:WebDAV(本机 ZLM/Alist 容器)播放/seek/断链重试/过期令牌/目录缓存,
      离线子集用本地 http range 服务模拟。

## 验收

- 断链→恢复时长可测(目标:一次重取内恢复);目录二次进入 <100ms(缓存命中);
  令牌过期回调可达产品层(panvox P-2 三态流程吃这个契约)。

## 风险与开放问题

- IOParseDav 是全新组件,量级参照 IOParseSmb(~600 行)+ 缓冲窗口,别低估。
- httplib 不可中断 → 重试/超时中止语义受限,可能要换请求实现或加中止轮询。
- resolve 的特殊容器语义(BDMV/剧集聚合,AvoxBase.h:233-234 注释)是 a10 的挂载点,
  设计 T1 时留口子。
