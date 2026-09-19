# A-5 可 seek 虚拟文件系统 (avox_remote)

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M2(**建议 M1 末期提前启动**:panvox P-4 刮削与源浏览硬依赖)
计划状态:**T1 契约设计 + T2 IOParseDav 已落地, 剩 T3 重试/T4 缓存/T5 用例** · 来源:backlog A-5
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

## T1 契约设计(2026-09-19 定稿, 未动代码)

> 状态: 设计定稿待实施。任务卡: 夜间施工进度.md T11。实施时按本节落码, 变更需回写本节。

### 1. authExpired 生命周期(三态授权流)

```
产品 open/list(带 user/pass 或 token)
  → DavSource/SmbSource 会话建立
  → 任意请求收到 401/403(且非首次鉴权):
      1) 引擎置会话 invalid, 停止重试(避免无谓风暴)
      2) 触发 IRemoteSourceOb::onAuthExpired(sourceId) —— 新增回调
      3) 播放中: IO 层按「断流」处理(见 §2), 不静默循环重试
  → 产品弹重授权 UI, 拿到新凭据后调用 reauthorize(entry, user, pass, token)
      —— 新增公开方法(挂在 IRemoteSource, 见 §4)
  → 引擎用新凭据重建会话: 播放中场景经 refresh(entry) 重 resolve 直链续播(§2);
     浏览场景下次 list 自然生效
超时语义: onAuthExpired 触发后 30s 内未 reauthorize, 会话进入 closed,
后续操作返回 authFailed(-3), 产品需重新 open。
```

要点: authExpired 是「需要人工介入」信号(区别于可自动重试的瞬时 401——
单次请求超时/重试路径内消化, 不抛产品); 同一会话 authExpired 至多抛一次,
直到 reauthorize 成功后重新武装, 防回调风暴。

### 2. 播放中直链失效 → refresh 续播通道

```
IOParseDav 读包遇 http 401/403/410/5xx-断流:
  → IO 层上报 onIoError(remoteBroken, url) 后暂停读循环(不 close)
  → 引擎调 IRemoteSource::refresh(entry) —— 复用 AvoxBase.h:237 预留签名,
    实现方(DavSource)重 PROPFIND/GET 直链(带新凭据/新 token)
  → 成功: 取新直链, IOParseDav 以「原 offset 重新 range 打开」续播;
    失败: 分类映射(401→走 §1 authExpired; 404/410→ioError 文件已删;
    超时→重试策略退避)
重试策略: 次数 3 次, 退避 1s/2s/4s, 常量先内定, 后经 option 透出。
```

IOParseDav 结构: 参照 IOParseTorrent 的 lookahead 窗口(预读 4MB 起步,
可配), seek = 重开 range 请求; 单缓冲 256KB 的 IOParseSmb 对齐同一窗口口径。
IoPlan 枚举(AvoxMuxer.h)加 `dav` 项, MediaPlayer.cpp 路由注册。

### 3. alist/OpenList token 鉴权

DavSource 增加 Header 注入点: `setAuthHeader(key, value)`(内部会话级,
Authorization: Bearer <token> / 自定义 key 均可); resolve 产生的直链请求
由 IOParseDav 携带同一 Header。token 刷新由产品层负责(引擎只透传,
过期走 §1 authExpired)。

### 4. 接口面清单(全部「只增不改」)

| 位置 | 变更 | 说明 |
|---|---|---|
| `AvoxBase.h` IRemoteSourceOb | + `onAuthExpired(const char* sourceId)` | 带默认空实现, 旧观察者零影响 |
| `AvoxBase.h` IRemoteSource | + `reauthorize(entry, user, pass, token)`(虚, 默认 false) | 旧插件未覆写返回 false, 产品可判不支持 |
| `AvoxBase.h` RemoteCode | 沿用预留 authExpired=-4 | 首个产生者=本契约 |
| `AvoxMuxer.h` IoPlan | + `dav` | 值追加尾部 |
| SWIG 四语言 | 头文件即真源, 构建期再生成(gitignore) | UseSWIG 不跟踪头依赖, 改头后 touch common.i |

### 5. 对 A-10 的预留口

resolve() 的特殊容器语义(BDMV/剧集聚合): refresh(entry) 返回的 entry 保留
container 字段语义, IOParseDav 只消费直链不解释容器; BDMV 挂载点后续以
独立 source 插件接入, 不在本契约内扩展枚举。

### 6. 实施顺序与工作量

T2 IOParseDav(参照 IOParseSmb ~600 行 + 窗口, 2-3 天) → T3 重试/续播
(1-2 天, 依赖 §2 通道) → T4 目录缓存(1 天) → T5 avox-test 用例
(配合 dav-* 用例表已预留)。httplib 不可中断限制: 重试/超时中止先以
「短超时 + 整体重试」近似, 若语义不足再换请求实现。

## 任务拆解(T2~T5, 设计见上节)

- [x] T2 IOParseDav(2026-09-19, `29c2cd6`): 新组件 plugins/avox_remote/IOParseDav
      (AVSource+RunTask+自定义avio, 与 IOParseSmb 同构), httplib Range GET +
      **4MB 单线程预读窗口**(顺序读摊薄请求, seek 落窗口内零请求), Basic 认证
      (userinfo 百分号解码), bytes=0-0 探测总大小(Content-Range)。IoPlan 加 `dav`
      项; MediaPlayer 对 dav://davs:// 自动路由(同 smb:// 先例); http(s) 直链
      显式 setIoPlan(dav) 亦走此源。**EOF 停放机制**(IOParseFF 同款 bEof/bEofReset):
      小文件起播即读完, seek 后复位续读; IOParseSmb 同款「seek 在 EOF 后管道
      静止」隐患已随后同构修复(bEof/bEofReset/bEofNotified 三原子移植, Release
      编译通过 —— libsmb2 已在 avc_library 就位参与常规编译; 运行期待真机 SMB
      服务端走查)。本机 range 服务器实测: dav:// 播放+seek PASS
      (seek 109ms), 离线回归 44/44 绿。v1 限制: 服务端不支持 range(200 全量)
      明确报错不降级; 直链失效自动重试(refresh 续播)属 T3。
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
