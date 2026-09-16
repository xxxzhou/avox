# A-10 蓝光原盘:BDMV/ISO 源 + 章节信息

> 状态: 进行中 · 上次核对: 2026-09-16 · 权威源: -


优先级 P1 · 里程碑 M3.5 · 计划状态:零起点(仓内无任何相关代码)
跳片头/Intro Skipper API 配套;片头片尾静音检测可选。

## 出口判据

1. BDMV 目录与 ISO 源可播放主故事线(自动选 longest playlist)。
2. 章节信息暴露给宿主(index/标题/入点),mkv/mp4 通用章节同样暴露。
3. 产品能据此做章节列表 + 跳片头(P-8 吃接口)。

## 现状(代码落点)

- **全仓零实现**:无 libbluray(3rdparty/library 五平台无、cmake 无引用)、无 chapter/marker
  数据结构(IMediaPlayer/AvoxDef/player/ffmpeg 全文零命中)。
- **唯一挂载点语义预留**:`src/avox/AvoxBase.h:234` resolve() 注释——「特殊容器在此翻译:
  磁力单文件/蓝光原盘(BDMV)文件夹/剧集聚合,语义同 Kodi Resolve」。
  BDMV source 插件仿 `plugins/avox_torrent` 的 resolve 模式(TorrentSource:
  open 探测→list 枚举→resolve 出可播 URL)。
- FFmpeg 已有的相关能力:直接读 m2ts/`bluray:` 协议需 libbluray 编入(当前裁剪版无);
  mkv/mp4 容器 chapters 在 ffprobe 层现成,只是引擎没读。

## 任务拆解

- [ ] T1 libbluray 引入:预编译入库(3rdparty/library 五平台,参照 libsmb2/libtorrent 口径)
      + cmake Find 模块;LGPL 动态链接口径与现有 FFmpeg 裁剪构建兼容性核对。
- [ ] T2 通用 chapter 暴露(不依赖蓝光,先行):`IMediaPlayer` 层章节数据结构
      (index/title/ptsIn/ptsOut)+ AVSource 从 FFmpeg 容器读 chapters → mkv/mp4 e2e 先行,
      这个能力对所有片源有用,别绑死蓝光。
- [ ] T3 BDMV source 插件:`plugins/avox_bluray`(或并进 avox_remote 的 resolve 语义):
      BDMV 目录/ISO 探测 → playlist 枚举(标题/时长)→ resolve 主故事线;
      ISO 走 libbluray 的 image 直读,不做系统挂载。
- [ ] T4 章节来源打通:蓝光 clpi/mpls 章节经 libbluray API 读出 → 并入 T2 的同一套
      chapter 结构;Jellyfin IntroSkipper 对接口留给产品(P-8)。
- [ ] T5(可选)片头静音检测:音频能量扫描 CLI/接口,独立小工具,不阻塞主线。

## 验收

- BDMV 目录 + ISO 样盘(无 DRM,自制或公开测试盘)三平台(先 Windows)播放主故事线;
  mkv 章节用例先行全绿;章节列表经 FFI 可读。

## 风险与开放问题

- libbluray 五平台预编译是主要工作量(尤其 iOS/Android 工具链);AACS 加密盘**不做**(合规),
  只支持解密原盘/自制盘,文档写明。
- chapter 结构是新的公开接口(动 AvoxPlayer.h + SWIG),设计时把「通用容器章节」和
  「蓝光章节」统一成一个模型,别出两套。
- resolve 特殊容器语义(AvoxBase.h:233-234)与 a05 的 T1 契约设计是同一处口子,先后呼应。
