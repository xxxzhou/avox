# avox_torrent —— 磁力/BT 边下边播插件

`open("magnet:?xt=urn:btih:…")` 或本地 `.torrent` 路径即可边下边播, 数据源走标准 `IAVSource` 包回调管线(解封装/解码/渲染零改动)。

## 工作原理

```
magnet:/xxx ──> IOParseTorrent(AVSource)
   ├─ TorrentEngine(libtorrent): DHT/tracker 取元数据(BEP-9) → 选文件
   │   ├─ 顺序调度: 播放位置前向lookahead窗口top优先级 + set_piece_deadline
   │   ├─ 首轮 head+tail 预取(MP4 moov / MKV cues 在尾部场景seek秒级可用)
   │   └─ 未选文件 dont_download; 已播过区间降回低优先级保数据省带宽
   ├─ FFmpeg 自定义 avio(read/seek 回调直读 piece, 无本地HTTP中转)
   └─ AVFormatContext(CUSTOM_IO) → 与本地文件一致的解封装行为(downLive模式)
```

复用的核心导出: `AVSource`/`RunTask`/`OptionLink`(基类), `FFHelper`(ffAvoxPacket/ffIoError/编解码映射/智能指针封装), `H26XHelper`(extradata 拆分) —— 插件内无重复实现。

## 使用

```cpp
auto* player = createMediaPlayer();
player->setIoPlan(IoPlan::torrent);          // 选择 torrent 数据源
player->open("magnet:?xt=urn:btih:...");     // 或 "D:/xx.torrent"
```

可选配置(`IMediaPlayer::getOption()->setInt/setString/setBool`, open 前设置):

| 键 | 默认 | 说明 |
|----|------|------|
| `torrent.fileIndex` | -1 | 手动指定种子内文件索引; -1 自动选最大媒体文件 |
| `torrent.cacheDir` | 系统 temp/avplay_torrent/<infohash> | 缓存目录(按 infohash 落盘, 同种子复用续传) |
| `torrent.lookaheadMB` | 32 | 播放位置前向预取缓冲(MB) |
| `torrent.metaTimeoutMs` | 45000 | 元数据获取超时(两阶段: 超时后强刷announce再等一轮) |
| `torrent.pieceTimeoutMs` | 20000 | 单片等待超时(超时走 netTimeout 错误路径) |
| `torrent.extraTrackers` | 空 | 追加 tracker, 分号/逗号分隔 |
| `torrent.maxDownloadSpeedKB` | 0(不限) | 全局下载限速 KB/s |
| `torrent.cacheMaxGB` | 0(不限) | 缓存目录总量上限(GB), 超限按 LRU 淘汰最久未用种子缓存 |
| `torrent.deleteOnClose` | false | 关闭时删除已下载数据 |

## 磁力多文件: 文件列表探测 + 选文件播放

磁力常是多文件种子。先探测元数据列出文件, 用户选择后再播放, 接口统一在
`AvoxBase.h` 的新类 `ISourceProbe`(不动 IMediaPlayer; swig/godot 自动获得):

```cpp
avox::ISourceProbe* p = avox::createSourceProbe("torrent");  // 插件未装返回 nullptr
avox::ISourceProbeOb* ob = ...;                             // onProbeResult(code) 回调
p->setOb(ob);
p->start("magnet:?xt=urn:btih:...", /*cacheDir*/"", 45000); // 异步: 只等元数据零下载
// 回调 code=0 后:
int n = p->getFileCount();
p->getFileIndex(i); p->getFilePath(i); p->getFileSize(i); p->isMediaFile(i);
p->selectFile(3);                                   // 选择(原始文件索引)
p->applyToOption(player->getOption());              // 写入 torrent.fileIndex
player->open(同一磁力url);                           // 起播, 元数据缓存秒开
```

实现要点:
- 探测端 `TorrentEngine::probe()` 用 libtorrent `stop_when_ready`, 元数据一到自动暂停, 全程零下载;
- 探测与播放共用进程级共享会话(热 DHT/peer 缓存, 免每次冷启动 2~5s); 同一种子多引擎并发由进程级引用计数保护, 引用归零才摘种子, 探测退出不会误杀在播;
- 供数按目标文件在种子内的绝对偏移换算 piece 序号(多文件种子文件起点非 0), 调度窗口/尾部预取钳制在目标文件片范围内, 不越界拉其他文件;
- seek 后旧前向窗口残留片降回低优先级, 带宽不再被旧位置分走;
- 元数据落盘 `<cacheDir>/<infohash>/metadata.torrent`, 之后 `start()`(播放)命中缓存直接载入, 免 BEP-9 二次等待(冷门种子 45s 级 → 秒开); 另有 itorrents.org HTTP 缓存通道与 BEP-9 竞速;
- `cacheDir` 与播放选项 `torrent.cacheDir` 传同值才能命中缓存, 传空则两边一致走系统默认;
- godot 封装: `SourceProbe` 类(`platform/godot/plugin/src/source_probe.h`), `start()` 异步 + `probe_result` 信号 + `get_files()`(视频扩展名排前), tools 播放器 UI "文件→打开磁力/BT…" 已接。

## 性能基准(每步优化前后各跑一轮, 分步对比)

分步计时工具: `samples/functest/torrentbench.cpp`(探测文件列表/起播/稳态追帧/seek恢复)
+ `script/torrent/torrent_bench.py`(多路磁力语料/结果JSON/对比表):

```bash
python script/torrent/torrent_bench.py list                  # 语料: bbb/sintel/cosmos/steel/bbb_multi
python script/torrent/torrent_bench.py run --tag bbb --fresh # 冷启动(清缓存), 全阶段
python script/torrent/torrent_bench.py run --tag bbb         # 热缓存(元数据缓存/已下数据复用)
python script/torrent/torrent_bench.py run --all --probe-only
python script/torrent/torrent_bench.py compare old.json new.json   # 优化前后分步差值
```

结果存 `results/torrent_bench/*.json`(logs/ 内含每轮引擎日志); 阶段说明:
probe_ms=文件列表就绪, open_ready_ms=open到轨道就绪(含引擎首/尾片等待+avformat),
steady_ms=消费12s内容的实际耗时(speed_ratio<1=追不上), seekNN_ms=seek到未下载区域
的二段判据恢复时长(位置到位且继续前进800ms, 防假快)。

2026-08 基线(Windows, 冷启动3轮中位; swarm健康度波动大, 对比看中位数):

| 语料 | probe | open→ready | 稳态追帧 | seek 25/50/75% |
|------|-------|-----------|---------|----------------|
| bbb(单文件磁力) | 1.9s | 12~20s | ≥0.99 (6.5MB/s) | 5.9/3.7/3.1s |
| bbb_multi(12文件) | 0.03s(本地.torrent) | 13~28s | 0.99~1.00 | 7.8/7.5/5.2s |
| sintel | 2.5s | 49.5s | — | 3.4/3.4/3.4s |
| steel(AV1/Opus) | 2.1s | 19.4s报"编码不支持"退出 | — | — |

open→ready 耗时构成(阶段细分日志 `[torrent] stage *`): 元数据 ~10ms(缓存命中) →
**head_ready 占绝大部分(等swarm出块, 客户端不可压)** → 尾片等待与头片并行。
健康swarm场景引擎侧已无更多可挤空间; DHT状态持久化(`%TEMP%/avplay_torrent/session.state`,
运行45s后节流落盘, 进程启动~85ms恢复)的收益在冷swarm/冷门种子场景。

已知边界: AV1/Opus 等 SDK 未覆盖编码会快速报 `no playable codec track`(IOParseTorrent
无轨道即错误返回, 不再挂死); 播放器缓冲看门狗 `mp.buffering.timeout.ms`(默认10s)
对 torrent 源自动放宽到 30s, seek 落点数据等待可长达十几秒。

多文件种子的调度要点(踩坑实录):
- 目标文件首/尾piece覆盖的邻接小文件不能 dont_download(整片永不落地→open超时),
  也不能 low_priority(本版libtorrent file优先级盖过piece优先级, 头片28s+才到),
  必须 default 档; 引擎已自动处理(共享边缘piece的邻接文件提default)。
- open 首轮调度只顶头部8片+尾段: 一上来灌满32MB窗口会和头片抢webseed请求
  流水线(64深), 头片就绪后随读推进滚动放大窗口。

## 构建(依赖引用方式与 opencv/onnxruntime 一致)

libtorrent 独立项目维护, **不在本仓库编译**:

1. 源项目: `D:\Work\github\libtorrent`(`git clone --branch v2.0.14 https://github.com/arvidn/libtorrent.git` + `git submodule update --init deps/try_signal`)
2. Boost 头文件(仅头文件用法): `D:\Work\github\boost`(官方发布包内的 `boost/` 目录)
3. 在 libtorrent 目录执行 `python build_windows.py` → 产物安装到 `../avc_library/3rdparty/library/windows/libtorrent/{include,lib}`(静态 /MT; encryption 默认 ON, OpenSSL MT 静态版封进 lib, 无 DLL 依赖)
4. 本仓库 `cmake/FindLibtorrent.cmake` 自动查找; 找不到时 `AVOX_ENABLE_TORRENT` 自动降级关闭(构建日志可见)
5. 插件产物: `<输出>/plugins/avox_torrent.dll`, ModuleMgr 懒加载扫描注册(首次 `open` 自动加载, MediaPlayer::cmdOpen 已挂 `ensureStarted` 触发点; `createSourceProbe` 同样触发懒加载)

重编细节与坑表见 [REBUILD.md](REBUILD.md)。

## 当前范围与限制

- 平台: Windows 已验证; Linux 同套代码理论可用; Android 预编译产物已入库(arm64 开加密 / v7a 暂无预编译 OpenSSL 为加密关), 插件接入待后续静态链方案
- encryption 已开启(MSE 协议加密, 可连"强制加密" peer); OpenSSL 为 MT 静态封进插件 DLL, 与主程的 OpenSSL DLL 版互不冲突
- 单文件播放策略: 一个种子选中一个媒体文件播放; 多文件选择经 `torrent.fileIndex` 或 `ISourceProbe`
- 起播速度取决于 swarm 健康度(peer 少的冷门种子会先经历元数据等待期, 有界超时报错; 探测过一次后有元数据缓存)
- 测试建议使用 Blender 官方发布的 Big Buck Bunny / Sintel 种子(web-seed 友好)
