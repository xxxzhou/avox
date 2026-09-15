# Subtitle 字幕模块

统一字幕视图: 内封字幕轨(ASS/SSA/SRT/PGS)、外挂字幕文件(.ass/.srt)、
语音识别(ASR)三路内容共用一条 canvas 渲染通道, 三槽位引擎内仲裁。
架构来源: doc/plan/player/字幕模块合并计划.md(v3 统一 canvas 通道)。

## 架构

```
SubtitleView : public ISubtitle, public ISurfaceRenderOb   (统一视图)
├── SubtitleSlots               // 三槽位仲裁(后激活者胜, 空窗不回落)
├── Clock                       // 统一时钟(AVTrack 每帧 sync)
├── [轨槽] IAssOverlay          // libass 插件(avox_ass): chunk 流 → RGBA canvas
│   └── PGS 画布                // PgsDecoder(IO 线程解码) → setPgsCanvas
├── [文件槽] SubtitleFile       // .srt 解析 → 按时间查文本
│            或 IAssOverlay     // .ass/.ssa → 插件直载(扩展名内部分流)
├── [ASR槽] SubtitleAsr         // 识别结果/流式部分文本
│   └── AudioStt                // 语音识别(内部线程)
├── TextRasterizer              // 文本 → RGBA8 bbox canvas(FreeType, 样式可配)
└── ICanvasLayer                // 唯一混合出口(VkCanvasLayer, sourceOver)
```

所有内容统一产出 RGBA8(premultiplied) bbox canvas, 经同一个
`ICanvasLayer` sourceOver 混合出帧。渲染路径唯一: 谁画 canvas 的差别
只在内封轨(libass/PGS 位图)与纯文本(FreeType 光栅化)之间。

## 三槽位仲裁

| 槽位 | 激活入口 | 内容源 |
|------|----------|--------|
| track | `setSubtitleTrack(i)` | 内封 ASS/SSA/SRT/PGS 轨 |
| file | `loadSubtitle(path)` | 外挂 .ass/.ssa(插件) / .srt(光栅化器) |
| asr | `getSubtitle()->enableAsr()` | 语音识别结果 |

- **后激活者胜**: 激活新槽位时视图内自动拆除被顶掉的槽位(轨=关通道+PGS
  解码路由复位; 外挂=清文件; ASR=停识别), 引擎内保证任一时刻至多一路
  上屏, 不依赖调用方自觉。
- **空窗不回落**: 胜者无内容的时段就空屏, 不回落到其他槽位, 避免两路
  字幕闪替。「AI 字幕 vs 片源字幕」的产品切换 = 重新调对应激活接口。
- `setSubtitleTrack(-1)` 只关轨槽(轨本就是胜者时), 不影响外挂/ASR。
- 无 avox_ass 插件: 轨槽降级为不渲染, 纯文本(.srt/ASR)照常。

## 渲染细节

- **canvas 去重**: 单一 `lastSeq` 序号判重, 内容未变零上传; seq 域在
  槽位切换/源切换(ASS↔PGS)时置 -1, 强制清异源残留。
- **线程约定**: activate*/load*/close/resetEvents 只在播放器线程;
  pushChunk/setPgsCanvas 在 IO 线程(有界队列, 溢出丢最旧); onRender 在
  渲染线程(内部互斥, 锁内只做短操作)。
- **样式**: TextCanvasStyle(字体/字号/颜色/锚点/换行), 默认 simhei 40
  底部居中, 字号随帧高 DPI 缩放(参考 1080p), 对齐旧观感。
- **性能**: 文本光栅化 ~0.06ms@1080p(subtitletexttest SUBTEXT_PERF=1
  实测, 稳态); canvas 仅内容变化时上传, bbox 裁剪天然限制尺寸。

## 识别模式

| 模式 | 场景 | 识别 | 延迟 |
|------|------|------|------|
| `streaming` | SourcePlayer 实时采集 | 流式 | ~100ms |
| `ptsSync` | MediaPlayer 视频播放 | 离线, 结果按 PTS 入队 | ~0.5s |

翻译链路已删除(端上小模型质量不行, 产品侧翻译由应用层 AI 管线负责)。
翻译基础设施(ITranslator hub / avox_zlmediakit HttpTranslator /
avox_cmd translate)在字幕模块之外, 不受影响。

## 使用示例

### MediaPlayer(外挂/内封轨/ASR)

```cpp
player->loadSubtitle("movie.srt");     // 或 .ass/.ssa, 扩展名内部分流
player->setSubtitleTrack(0);           // 或选内封轨(三槽位自动互斥)
player->setSubtitleTrack(-1);          // 关轨槽
player->getSubtitle()->enableAsr();    // ASR(自动清轨/外挂槽)
```

### SourcePlayer(streaming ASR)

```cpp
subtitleView->setAsrMode(AsrMode::streaming);
subtitleView->setWindowRender(windowRender);
subtitleView->setAudioDesc(audioDesc);
subtitleView->enableAsr();             // getSubtitle() 直返视图
subtitleView->inputSpeech(audioData, pts);
```

## 组件职责

| 组件 | 职责 |
|------|------|
| `SubtitleView` | 统一视图: 三槽位仲裁, canvas 生产调度, 唯一混合出口 |
| `SubtitleSlots` | 槽位仲裁状态机(header-only, 单测覆盖) |
| `SubtitleFile` | .srt 解析(SrtParser), 按时间查当前文本 |
| `SubtitleAsr` | ASR 模式管理, 识别结果按 PTS 查找/流式文本 |
| `TextRasterizer` | 文本 → RGBA8 bbox canvas(FreeType, 样式参数化) |
| `IAssOverlay` | libass 插件通道接口(assOverlayHub 工厂, 缺插件降级) |
| `AudioStt` | 语音识别(内部线程) |
| `Clock` | 统一时钟(AVTrack::updateClock 每帧 sync) |
