# 媒体读取接口 IMediaReader 实施方案（审查稿）

> 状态: 进行中 · 上次核对: 2026-10-29 · 权威源: -

avoxkit 会话 `session_dd65acec0458d06eac9020adba5239aa` 提出了把 avoxkit 里
`ak_probe` / `ak_index` / `ak_frames` / `ak_engine` 的直连 FFmpeg 收回 avox 的
`IMediaReader` 方案。本文按 avox 真实代码逐条核对可行性，给出修正后的接口、
实现落点、迁移步骤与验收矩阵。**本文只是方案，代码未动。**

---

## 0. 结论

1. **方向成立**：avox 已有离屏 Vulkan 渲染链、`ISourceInfo` 轨道模型、
   `MetaExtractor` HDR/DV 元数据提取器，能支撑“探测 + 索引 + 抓帧”的只读接口，
   不需要产品侧再维护第二套 demux/decode 代码。
2. **不能按原设计稿直接写**：原稿有十余处会直接导致回归或实现卡死，见 §2。
   其中 4 处是必须改的硬问题（像素格式覆盖、HDR/DV 元数据、index 的
   `has_audio`/`waveAvailable`、取消 open），其余是语义和边界要拍板。
3. **建议 v1 只承诺本地文件**（含挂载的网络盘/UNC），不承诺 HTTP/HLS/RTSP。
   理由是 `IOParseFF` 的 HTTP 病态 mp4 修补（逐段 mdat、段缓存、seek 闩）
   不是 `avformat_open_input` 能复用的；直连 HTTP 会在已知病态片源上重演
   开流数分钟不返回。等 avoxkit 真正开放 URL 输入时走 v2，把 IO 换成 avox 的
   IO 路径，公共接口不变。
4. **ABI 可行**：`ISourceInfo` / `ISTrackDesc` 只追加虚函数；不插到中间、不改
   结构体大小。旧宿主二进制不调用新槽位即安全；但 avoxkit、playtest runner、
   panvox 仍按发布节奏重建。

---

## 1. 原设计里已经成立的部分

| 设计点 | 结论 | 代码依据 |
|---|---|---|
| MediaInfo 合并进 ISourceInfo | 成立。`durationMs/bitRate/formatName` 追加到 `ISourceInfo` 末尾即可；全仓只有 `BaseSource` 一个实现，默认实现可保旧二进制 | `src/avox/AvoxSource.h:210`、`src/avox/source/BaseSource.hpp:40` |
| 抓帧走 VK 管线 | 成立。`SurfaceRenderVk::setOffSurface(YuvType::other)` + `enableImage(IImageBuffer*)` + `render(YUVFrame)` 是现成的离屏图像分支，`render()` 内部 `vkWaitForFences` 后才返回，返回时数据已就绪 | `src/avox/video/SurfaceRenderVk.cpp:48/80/396`、`src/avox_vulkan/layer/VkPipeGraph.cpp` 的 `onRun()` |
| enableImage 只支持 rgba8 | 成立，且 `VideoRender::enableImage` 有显式校验；输出宽度/高度由 buffer 的 ImageFormat 决定，GPU 侧 `VkResizeLayer` 缩放 | `src/avox/video/VideoRender.cpp:38-58` |
| 首次 enableImage 后先渲染 dummy 帧 | 成立，`QEnhanceFilter` 已经踩过这个坑并留了注释：图重建会销毁旧 staging，用户 buffer 在重建后到下一帧输出前悬存 | `src/avox/muxer/QEnhanceFilter.cpp:51-73` |
| 无 Vulkan 不兜底 | 成立。`SurfaceRenderVk` 构造后 `vkVideoRender == nullptr`，`getVkVideoRender()` 返回空；probe/index 不需要 VK，可正常工作 | `src/avox/video/SurfaceRenderVk.cpp:11-32` |
| 旋转在缩图后 CPU 做 | 成立。当前 `ak_frames` 读 `AV_PKT_DATA_DISPLAYMATRIX` 并按 90/180/270 旋转采样；播放端目前确实不消费 display matrix，缩略图必须保留这段否则竖拍视频横躺 | `avoxkit/native/avtool/src/ak_frames.cpp:96-118/188-267` |
| 事件驱动 observer 模型 | 成立，和 `IRecorderOb` 的 `onStateChange/onProgress/onComplete` 是同一套生命周期风格 | `src/avox/AvoxMuxer.h:79-95` |
| 峰值返回线性 float[0,1] | 成立。`sqrt` 单调，先逐帧 sqrt 再取桶最大 与 先取桶最大再 sqrt 结果一致；shim 转 `sqrt(peak)*100` 可保持现有 UI | `avoxkit/native/avtool/src/ak_index.cpp:208-213` |
| v1 在 avox_ffmpeg 内直接用 libav | 成立，且不算“产品侧第二套”：avoxkit 最终只 include/link `avox.lib`。但 v1 网络范围要收窄，见 §2.12 | - |

---

## 2. 必须先收口的 10 个问题

### 2.1 HDR 说法自相矛盾（硬问题）

原稿 §2.3 写“HDR（PQ/HLG）v1 仍按 SDR 近似处理，不在本次接口里做 tone mapping”，
但 §4/§5 又写“setHdrMode(follow) + VK tone map 输出 SDR rgba8”。两处必须统一。

**结论**：v1 就做 tone map，`HdrMode::follow` + 默认 soft knee。这样 HDR10/DV
缩略图比现在 CPU 路径（完全忽略 PQ）更正确。§2.3 的“SDR 近似”整段删掉。

### 2.2 VK 像素格式覆盖不足（硬问题）

`ffYuvType` 只映射：

- `yuv420P/yuv422P/yuv444P`（含 yuvj 变体）
- `yuv420P10LE/BE` → `yuv420P10`
- `yuv422P10LE/BE` → `yuv422P10`
- `P010LE/BE` → `p010`
- `NV12` → `nv12`

没有：`gray8`（`YuvType::gray` 存在但 `ffYuvType` 不产出）、12bit（420P12/422P12/
444P12）、444P10、NV21、YUYV/UYVY 打包格式。而当前 `ak_frames` 用
`AVPixFmtDescriptor` 通用采样，**任何非 RGB 平面格式都能出图**。

=> 迁移后会出现“以前有缩略图、现在没有”的回归。三种处理：

- **A（建议）**：只承诺 VK 已支持的格式；其余返回
  `MediaError::unsupportedPixFmt`，消息里带 `av_get_pix_fmt_name`。同时把
  `AV_PIX_FMT_GRAY8 → YuvType::gray` 补进 `ffYuvType`（VK 对 gray 有 r8 路径，
  见 `yuv2ImageFormat` 和 `getVkFormat`）。12bit/444P10 明确列为不支持。
- B：为 12bit/444P10 扩 `YuvType` 与 VK 输入层，工作量大，收益低。
- C：保留 CPU 采样兜底，等于把要删的第二套又留下，不推荐。

建议先按 A 落，验收里把“不支持格式返回明确错误”写成用例，不要静默空图。

### 2.3 HDR/DV 元数据没有来源（硬问题）

原稿只写“把流的矩阵、量程、HDR 元数据喂进去”，但没说元数据从哪来。播放端是
`FFVDecoder` 从帧 side data/裸流 SEI 里解析后经 `IVideoDecoderOb::onHdrMeta/
onDoviMeta` 下发；直接 `avcodec` 解码不会自动走这条链。

**结论**：复用 `src/avox/video/MetaExtractor.hpp`：

- `setCodec(ffVCodec(codecpar->codec_id))`
- `setDvProfile(容器 AV_PKT_DATA_DOVI_CONF 解出的 profile)`
  （取法与 `IOParseFF.cpp:1181` 同）
- `setAvcc(checkAvccPacket(...))`（`src/avox/source/PacketBuf.hpp:17`）
- 每个视频包 decode 前 `extract(ffAvoxPacket(pkt))`，回调里存最新
  `HdrMeta` / `DoviMeta`
- 渲染前 `surface->setHdrMeta(...)` / `setDoviMeta(...)`；seek 后
  `reset()` 清 RPU 上下文与去重状态

不接这一步，DV P5/P8 缩略图会偏色（P5 没有 ST2086/CLL，RPU 是唯一正确色彩来源），
HDR10 的 tone map 膝点也会退化成默认值。**这是硬要求，不是优化项。**

### 2.4 色彩空间取流级会与当前行为不一致（硬问题）

当前 `ak_frames` 用的是**解码帧**的 `frame->colorspace` / `frame->color_range`
（`ak_frames.cpp:216-217`），而 `ffColorSpace` 的入参是 `AVCodecParameters*`
（`FFHelper.cpp:418`）。两者多数文件一致，但容器 VUI 与帧级标记不一致时，
low-level 行为会变。

**结论**：给 reader 加一个 frame 级色彩空间 helper（或给 `ffColorSpace` 加重载），
按 `frame->colorspace / color_range / color_trc` 取值，缺失时回退 `codecpar`；
保持与当前 CPU 路径同源。HDR/DV 的 `transfer` 也从这里来。

### 2.5 Index 结果缺 hasAudio / waveAvailable（硬问题）

当前 `ak_media_index` 的 JSON 有 `has_audio`，且“有音轨但解码器打不开”时
`has_audio=true`、`peaks=[]`（`ak_index.cpp:191/287-288`）。Dart 侧
`MediaIndex.hasAudio` 会消费它。原稿的 `MediaIndexResult` 只有 peaks/keyframes，
丢了这个区分。

**结论**：`MediaIndexResult` 增加：

```cpp
bool hasAudio = false;       // 选中音频轨存在（与解码器是否可用无关）
bool waveAvailable = false;  // 音频解码器成功打开并产出过帧
```

`ak_index` 用 `hasAudio` 填 `has_audio`，`waveAvailable` 决定 peaks 是否为空。

### 2.6 Duration 回退与直播语义（硬问题）

当前顺序是：

1. `fmtCtx->duration / 1000`
2. 若 <=0，取各 stream `duration` 最大值
3. 若仍 <=0，用音频解码最后一帧时间 `last_ms`
4. `bucketMs = max(1, duration/buckets)`

原稿 §4 写“索引要求有限时长；直播流返回 unsupported”，与第 2/3 步冲突。
另外当前实现有一个隐藏 bug：`add_peak` 只在 `duration_ms > 0` 时写桶，
如果时长是最后才由 `last_ms` 推出来的，peaks 会全 0。

**结论**：保留 1→2→3 的回退顺序；直播（duration 一直为 0 且读不到 EOF）返回
`unsupported`。实现上建议先收集 `(ms, framePeak)` 再在 duration 确定后映射到桶，
顺手修掉上面这个全 0 bug；普通文件结果不变。

### 2.7 视频/音频轨缺原始 codec 名（硬问题）

`ISourceInfo` 只有 `VCodecId/ACodecId` 枚举。`VCodecId` 没有 vp8/theora/h261/
cinepak 等，`ffVCodec` 对这些返回 `none`；而 `ak_probe` 的 `codec` 字段是
`avcodec_get_name`，UI 会显示 `VP8` 之类。迁移后这些文件会显示 `NONE`。

**结论**：`ISourceInfo` 追加两个只读方法（默认返回空串，只有 reader 覆盖）：

```cpp
virtual const char* getVideoCodecName(int32_t index) { return ""; }
virtual const char* getAudioCodecName(int32_t index) { return ""; }
```

字幕轨仍走 `ISTrackDesc::codecName()`（见 §3），因为 `prettySubtitleCodec`
需要 `subrip/mov_text/hdmv_pgs_subtitle` 这类原始名。

### 2.8 取消 open 无法打断（硬问题）

原稿承诺 `cancel()` 可跨线程取消 open/index/frame。如果 v1 直接
`avformat_open_input`，HTTP/坏盘 open 期间没有 `AVIOInterruptCB`，cancel 只能
等它自己返回。

**结论**：

- `avformat_alloc_context()` 先建上下文，装 `interrupt_callback`（回调里读
  `std::atomic<bool> cancelFlag`），再 `avformat_open_input(ctx, ...)`
- index/frame 的读写循环每个包检查 flag；seek 前后也检查
- `cancel()` 只置 atomic，不阻塞
- 取消经 `onError(op, cancelled)` 收尾，状态回 `ready`/`idle`

### 2.9 Vulkan 不能在建 reader 时初始化（硬问题）

`probe` 是高频调用（每次选文件都调）。如果 `createMediaReader()` 或 `open()`
里构造 `SurfaceRenderVk`，会把 Vulkan 设备/图创建带进 probe/index，既慢，也会让
“无 Vulkan 时 probe/index 仍正常”这条承诺失效。

**结论**：**首次 `grabFrame` 才构造 `SurfaceRenderVk`**；`open/buildIndex` 完全不碰
Vulkan。无 Vulkan 时 `grabFrame` 直接 `onError(frame, renderUnavailable)`，
probe/index 不受影响。

另外 `canVulkan()` 现在是 `static bool checked/result` 的无锁缓存
（`src/avox_vulkan/VkContext.cpp:322`），多 isolate 首次并发调用有数据竞争。
改成 `std::once_flag` 或加锁，作为本方案的附带小修。

### 2.10 轨索引语义与字段范围（语义问题）

- 原稿 `MediaIndexOptions.audioTrack/videoTrack` 写“局部轨序号”，但
  `ISourceInfo` 里 `VTrackDesc.trackId/ATrackDesc.trackId/ISTrackDesc.trackId()`
  都是**容器 stream index**（`IOParseFF.cpp:1170/1235/1265`）。两套语义混用会
  选错轨。**统一用容器 stream index，`-1` 表示自动。**
- `MediaFrameRequest.width` 原稿 §2.3 写 48–1280、§3 写 48–1920；当前 shim 是
  48–1280。**统一 48–1280，超出钳位而不是报错**（保持现有行为）。
- `MediaIndexOptions.buckets` 当前钳 64–4000，保持。
- `closed` 状态原稿没定义能否再 `open`。**建议去掉 `closed`，close 后回 `idle`，
  reader 可复用；或保留但明确 open 可从 closed 进入。** 本文按“close 回 idle”写。

### 2.11 封面图（attached_pic）与 ISourceInfo 的关系

`ISourceInfo` 就是 reader 的轨道清单（`videoSize/getVideoDesc/...`）。MP3/M4A
的封面图在容器里是一个带 `AV_DISPOSITION_ATTACHED_PIC` 的视频流
（MJPEG/PNG），它不是可播放的视频轨。问题就是：这个流算不算 `videoSize()`
里的一条视频轨。

- avox 现有的 `IOParseFF` 在填 `ISourceInfo` 时**明确跳过** attached_pic
  （`IOParseFF.cpp:1158`），播放端的 `ISourceInfo` 里没有封面；
- `ak_frames` 选视频轨时也跳过（`ak_frames.cpp:311`）；
- 只有当前 `ak_probe` 是例外：它遍历所有视频流，所以带封面的 MP3 会报一条
  `video: [{codec:"mjpeg", width:600, height:600}]`。UI 会把它当视频轨，
  转换页甚至可能按“有视频”规划。

**结论（建议，待你确认）**：reader 的 `ISourceInfo` **排除** attached_pic，
与 `IOParseFF`/`ak_frames` 一致。这样带封面的音频文件 `videoSize()==0`，
`ak_probe` 不再报视频轨——这是修正，不是回归。封面图展示如果以后要做，
单独提供 `getCoverImage` 一类接口，不要混进视频轨。

如果坚持 probe 原样，则要 include 并额外暴露 `isAttachedPic(index)`；但那会
保留“音频文件有视频轨”的误导，不推荐。

### 2.12 HTTP 不要放进 v1 承诺

原稿 §5.6 写 v1 支持“FFmpeg 能直接打开的 HTTP 源”。但 `IOParseFF` 为了
逐段 mdat 的 mp4（迅雷/Twitch 产物）做了 wrapper/段缓存/`AVSEEK_SIZE` 谎报，
直接用 `avformat_open_input` 会重演开流几分钟的已知问题。

**结论**：v1 实现层面接受任意 URI，但**验收与产品承诺只覆盖本地文件**
（普通路径、挂载盘、UNC）。`http/https/rtsp` 在 v1 返回
`MediaError::unsupported` 并给明确消息；v2 再换成 avox IO 会话。

### 2.13 其他小项

- `ffFps` 与 `ak_probe` 的 fps 回退顺序不同：`ak_probe` 是
  `avg_frame_rate → r_frame_rate`（`ak_probe.cpp:93-99`），`ffFps` 是
  `codecpar->framerate → avg_frame_rate`（`FFHelper.cpp:506`）。迁移后 fps 可能
  变化。reader 自己填充 `VideoDesc.fps` 时**照抄 `ak_probe` 的顺序**。
- `MediaIndexResult` / `MediaFrameInfo` 建议加 `int32_t structSize` 首字段
  （同 `SurfaceRenderEvent` 的做法），以后追加字段不破旧宿主。
- `MediaError` / `MediaReaderState` / `MediaOp` 建议用仓内既有的
  `AVOX_MAP_*` 宏 + `getXxxStr` 风格，值只增不改。
- `onIndexProgress` 百分比：0 起、最多到 99，`onIndexDone` 即 100；时长未知时
  不发进度。
- observer 回调不得抛异常；回调内只能调 `cancel()`，不得重入
  `open/buildIndex/grabFrame/close`。
- reader 析构函数只做资源释放，不派发回调；observer 生命周期必须覆盖到
  `close()` 返回。

---

## 3. 修正后的公共接口

新增 `src/avox/AvoxMedia.h`（示意，最终以头文件为准）：

```cpp
#pragma once

#include "AvoxDef.h"
#include "AvoxImage.h"
#include "AvoxSource.h"  // ISourceInfo / VTrackDesc / ATrackDesc / ISTrackDesc

namespace avox {

struct MediaIndexOptions {
  // 音频峰值桶数 [64, 4000]
  int32_t buckets = 1200;
  // 容器 stream index; -1 = best stream
  int32_t audioTrack = -1;
  int32_t videoTrack = -1;
};

struct MediaIndexResult {
  int32_t structSize = sizeof(MediaIndexResult);
  // 有效时长: 容器时长优先, 否则 stream duration, 再否则音频末帧推算
  int64_t durationMs = 0;
  int32_t bucketMs = 0;
  // 选中音频轨存在(与解码器是否可用无关)
  bool hasAudio = false;
  // 音频解码器成功打开并产出过帧
  bool waveAvailable = false;
  // 线性幅度 [0,1]; 只在 onIndexDone 回调内有效
  const float* peaks = nullptr;
  int32_t peakCount = 0;
  // 毫秒时间戳, 单调递增; 只在 onIndexDone 回调内有效
  const int64_t* keyframesMs = nullptr;
  int32_t keyframeCount = 0;
};

struct MediaFrameRequest {
  // 目标时间(ms); <0 按 0, 超出片长按片尾
  int64_t timeMs = 0;
  // 输出宽度 [48, 1280]; 这是旋转后的显示宽度, 高度按显示比例算, 不是固定16:9
  int32_t width = 320;
  // 容器 stream index; -1 = 第一条非封面视频轨
  int32_t videoTrack = -1;
};

struct MediaFrameInfo {
  int32_t structSize = sizeof(MediaFrameInfo);
  int64_t requestedMs = 0;
  // 实际解码帧时间, 可能晚于请求(从关键帧向后解)
  int64_t actualMs = 0;
  // 显示尺寸(已按 display matrix 旋正); 竖拍 9:16 时 height > width
  int32_t width = 0;
  int32_t height = 0;
};

#define AVOX_MAP_MEDIA_ERROR(XX)                  \
  XX(none, 0, "none")                             \
  XX(inputOpenFailed, 1, "input open failed")     \
  XX(findStreamInfoFailed, 2, "stream info failed") \
  XX(noVideo, 3, "no video")                      \
  XX(decoderMissing, 4, "decoder missing")        \
  XX(seekFailed, 5, "seek failed")                \
  XX(decodeFailed, 6, "decode failed")            \
  XX(unsupported, 7, "unsupported")               \
  XX(unsupportedPixFmt, 8, "unsupported pix fmt") \
  XX(renderUnavailable, 9, "render unavailable")  \
  XX(cancelled, 10, "cancelled")                  \
  XX(invalidState, 11, "invalid state")           \
  XX(invalidArgument, 12, "invalid argument")     \
  XX(timeout, 13, "timeout")                      \
  XX(other, 100, "other")

enum class MediaError : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_ERROR(XX)
#undef XX
};

#define AVOX_MAP_MEDIA_READER_STATE(XX) \
  XX(idle, 0, "idle")                   \
  XX(opening, 1, "opening")             \
  XX(ready, 2, "ready")                 \
  XX(indexing, 3, "indexing")           \
  XX(grabbing, 4, "grabbing")           \
  XX(closing, 5, "closing")

enum class MediaReaderState : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_READER_STATE(XX)
#undef XX
};

#define AVOX_MAP_MEDIA_OP(XX) \
  XX(open, 0, "open")         \
  XX(index, 1, "index")       \
  XX(frame, 2, "frame")       \
  XX(close, 3, "close")

enum class MediaOp : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_OP(XX)
#undef XX
};

class IMediaReaderOb {
 public:
  virtual ~IMediaReaderOb() = default;

  // 状态迁移; 所有回调串行, 不并发
  virtual void onStateChange(MediaReaderState pre, MediaReaderState state) {}
  // open 成功; ISourceInfo 从这一刻起有效
  virtual void onReady() {}
  // buildIndex 进度 0-99; 时长未知时不回调
  virtual void onIndexProgress(int32_t percent) {}
  // buildIndex 完成; peaks/keyframesMs 只在回调内有效
  virtual void onIndexDone(const MediaIndexResult& result) {}
  // grabFrame 完成; image 只在回调内有效
  virtual void onFrame(const MediaFrameInfo& info, IImageBuffer* image) {}
  // op 指明失败阶段; 回调后状态回退
  virtual void onError(MediaOp op, MediaError code, const char* message) {}
  // close 完成
  virtual void onClose() {}
};

// 只读媒体读取器: 一次 open, 探测/索引/抓帧复用同一个容器上下文。
// 结果只经 observer 回调; 返回值只表示请求是否被接受。
// open/buildIndex/grabFrame/close 由调用方串行调用; cancel() 可跨线程。
class IMediaReader {
 public:
  virtual ~IMediaReader() = default;

  // 必须在 open 前设置; observer 生命周期覆盖到 close 返回
  virtual void setObserver(IMediaReaderOb* ob) = 0;

  // 返回 false = 请求未被接受(状态不对/参数非法), 不触发回调, 读 lastError
  virtual bool open(const char* uri) = 0;
  virtual bool buildIndex(const MediaIndexOptions& options) = 0;
  virtual bool grabFrame(const MediaFrameRequest& request) = 0;

  // 线程安全: 取消当前 open/index/frame, 经 onError(op, cancelled) 收尾
  virtual void cancel() = 0;
  // 同步关闭; 等待内部工作线程退出, 返回后不再回调
  virtual void close() = 0;

  // onReady 后有效, close 前; reader 托管
  virtual ISourceInfo* getSourceInfo() = 0;

  virtual MediaError lastError() = 0;
  // reader 托管, 下一次操作/close 前有效
  virtual const char* lastErrorMessage() = 0;
};

extern "C" {
AVOX_EXPORT IMediaReader* createMediaReader();
AVOX_EXPORT const char* getMediaReaderStateStr(MediaReaderState state);
AVOX_EXPORT const char* getMediaOpStr(MediaOp op);
AVOX_EXPORT const char* getMediaErrorStr(MediaError error);
}

}  // namespace avox
```

新增 `src/avox/AvoxRuntime.h`：

```cpp
#pragma once

#include "AvoxDef.h"

namespace avox {

struct FFmpegRuntimeInfo {
  const char* version = nullptr;      // av_version_info(), engine 托管
  const char* buildConfig = nullptr;  // avcodec_configuration(), engine 托管
  int32_t avcodecVersion = 0;
  int32_t avformatVersion = 0;
  int32_t avutilVersion = 0;
};

AVOX_EXPORT FFmpegRuntimeInfo getFFmpegRuntimeInfo();
// 只查编码器是否注册, 不做真编
AVOX_EXPORT bool isEncoderRegistered(const char* name);
AVOX_EXPORT bool hasMuxer(const char* name);
// 输入/输出协议名, engine 托管
AVOX_EXPORT int32_t getProtocolCount(bool output);
AVOX_EXPORT const char* getProtocolName(bool output, int32_t index);
// 抓帧所需的 Vulkan 渲染链是否可用
AVOX_EXPORT bool isVulkanAvailable();

}  // namespace avox
```

`ISourceInfo`（`AvoxSource.h`）末尾追加，全部带默认实现：

```cpp
// 容器总时长(ms); 未知/直播为 0
virtual int64_t durationMs() { return 0; }
// 容器总码率; 未知为 0
virtual int64_t bitRate() { return 0; }
// 容器格式名(avformat iformat name); 未知为空串
virtual const char* formatName() { return ""; }
// 原始 avcodec 名(如 "h264"/"hevc"/"vp8"); 无效索引/未知为空串
virtual const char* getVideoCodecName(int32_t index) { return ""; }
virtual const char* getAudioCodecName(int32_t index) { return ""; }
// 容器 display matrix 声明的旋转角(0/90/180/270); 无效索引为 0
virtual int32_t getVideoRotation(int32_t index) { return 0; }
```

`ISTrackDesc`（`AvoxCodec.h`）末尾追加：

```cpp
// 原始 avcodec 名(如 "subrip"/"mov_text"/"hdmv_pgs_subtitle")
virtual const char* codecName() const { return ""; }
// 容器默认轨
virtual bool isDefault() const { return false; }
```

实现侧：`AVSource` 覆盖 `durationMs()` 转发 `duration()`；`IOParseFF` 覆盖
`bitRate()/formatName()`；`RawSource` 用默认值。`STrackDescImpl` 补
`codecName/isDefault` 字段，`BaseSource::addSubtitleDesc` 增加两个带默认值的
参数，`IOParseFF` 填原始 codec 名与 `AV_DISPOSITION_DEFAULT`。

---

## 4. 行为契约

### 4.1 状态机

```text
idle --open--> opening --成功--> ready
opening --失败/取消--> idle
ready --buildIndex--> indexing --完成/失败/取消--> ready
ready --grabFrame--> grabbing --完成/失败/取消--> ready
任意 --close--> closing --onClose--> idle
```

- `open` 只允许 `idle`；`close` 后回 `idle`，reader 可再次 `open`。
- `buildIndex/grabFrame` 只在 `ready` 接受；其它状态返回 false +
  `lastError(invalidState)`，不触发回调。
- `close` 在 `idle` 是 no-op；在 `ready` 正常关闭。

### 4.2 回调顺序

- open 成功：`onStateChange(idle,opening)` → `onStateChange(opening,ready)` →
  `onReady()`。
- index：`onStateChange(ready,indexing)` → `onIndexProgress(...)` →
  `onIndexDone(...)` → `onStateChange(indexing,ready)`。
- frame：`onStateChange(ready,grabbing)` → `onFrame(...)` →
  `onStateChange(grabbing,ready)`。
- 失败：`onError(op,code,msg)` → `onStateChange(...,回退态)`。
- close：`onStateChange(...,closing)` → `onClose()` →
  `onStateChange(closing,idle)`。

回调全部串行；实现可以先同步（回调在调用线程），以后换内部工作线程不改接口。
observer 不得在回调里重入 reader 的操作方法；`cancel()` 是唯一允许的重入调用。

### 4.3 数据生命周期

- `ISourceInfo`：`onReady` 后到 `close` 前有效，reader 托管。
- `MediaIndexResult.peaks/keyframesMs`、`onFrame` 的 `image`、`onError` 的
  `message`：只在回调内有效，需要留存请立即拷贝/编码。
- `lastErrorMessage()`：到下一次操作或 `close` 前有效。

### 4.4 取消

- `cancel()` 从任意线程可调；只置原子标志，不阻塞。
- open 期间靠 `AVIOInterruptCB` 打断；index/frame 每个包检查。
- 取消后 reader 回 `ready`（open 取消回 `idle`），可继续使用。

### 4.5 错误

- 请求被接受后，所有结果/失败只经回调；返回值只表示“请求是否被接受”。
- index 的音频解码失败不算 index 失败：`hasAudio=true`、`waveAvailable=false`、
  `peaks=[]`，仍 `onIndexDone`。
- 无视频轨时 `grabFrame` 接受请求，回调 `onError(frame,noVideo)`；shim 据此输出
  `has_video=false`。
- 单帧抓取失败不关闭 reader，下一帧继续。

### 4.6 显示方向、宽高与 UI 放置（9:16 不是 16:9）

`MediaFrameRequest.width` 是**旋转后的显示宽度**，不是编码宽度；高度由显示宽高
比算出，不是固定 16:9。

例：手机竖拍，编码 1920×1080，display matrix=90°：

- 显示尺寸 = 1080×1920（9:16）
- 请求 `width=320` → `height = round(320 * 1920 / 1080) = 569`
- 中间 VK 缓冲 = 569×320（宽高对调），CPU 旋转后交付 320×569
- `MediaFrameInfo{width:320, height:569}`；`ak_media_frames` 顶层同样输出
  320×569，JPEG 也是 320×569

实测（2026-10-29，当前 shim）：用 `1920×1080 + rotation 90` 的 fixture 跑
`ak_smoke frames ... 320`，当前 `ak_frames` 已经输出 **320×569** 的 JPEG；
`ak_probe` 仍只报编码宽高 1920×1080。也就是说抓帧侧本来是对的，缺口在
probe 的显示尺寸和 UI 的 16:9 假设。

**当前 avoxkit 的 UI 确实把它当 16:9 用了**，这是迁移时要一起改的：

- `AkFramePreview` 默认 `aspectRatio: 16/9`，9:16 图放进 16:9 框后被
  `BoxFit.contain` 缩成中间一条，左右大片留黑；
- `AkFilmstrip` 硬编码 `naturalPx = height * 16 / 9` 且 `BoxFit.cover`，
  9:16 缩略图会被上下裁掉；
- `AkLivePreview` 默认 16/9，竖拍视频的原生预览窗也被放进 16:9 矩形；
- `clip_page` 的参考图槽位是固定 220×124，竖屏图无法按 9:16 放。

reader 返回的 `width/height` 已经是显示尺寸（当前 `ak_media_frames` 也是），
UI 必须消费它，而不是自己假设 16:9：

1. `MediaFrameSource` 暴露 `aspectRatio`（取 `strip.width/height`；精确帧与
   缩略图共用同一源比例）。
2. `AkFramePreview` 的 `aspectRatio` 由源比例传入，不再默认 16/9；图片
   `BoxFit.contain`，不要拉伸。
3. `AkFilmstrip` 增加 `frameAspect`（或 `frameWidth/frameHeight`），
   `naturalPx = height * frameAspect`；格宽小于自然宽度时留白/contain，
   不要 `cover` 裁切。竖屏帧在固定高度条里自然更窄，这是正确行为。
4. `AkLivePreview`（转码/截取的任务预览）由任务入参或源信息带出显示比例，
   原生窗口矩形按 9:16 放；外层布局用高度反算宽度，避免竖屏撑破页面。
5. `clip_page` 的 220×124 槽位改成按比例算（例如固定高 124，宽 =
   `124 * aspect`；竖屏 9:16 时约 70 宽），或给参考图独立的自适应区域。
6. `ak_probe` 增加 `rotation`（0/90/180/270）与 `display_width/display_height`，
   让 UI 在缩略图到达前就知道该用 9:16。实现上 `ISourceInfo` 追加
   `getVideoRotation(int32_t index)`（默认 0），reader 与 `IOParseFF` 覆盖。
7. 验收：竖拍 9:16 fixture（带角标）断言 `ak_media_frames` 输出 320×569、
   JPEG 方向正确、预览/缩略图条按 9:16 放置且无裁切。

不要为了“统一 16:9”去裁切或加黑边：横屏源才用 16:9，竖屏源就应该 9:16。

---

## 5. avox 侧实现落点

### 5.1 文件

| 文件 | 内容 |
|---|---|
| `src/avox/AvoxMedia.h` | 本文 §3 的公共接口 |
| `src/avox/AvoxRuntime.h` | 运行时能力查询 |
| `src/avox_ffmpeg/MediaReaderFF.hpp/.cpp` | `IMediaReader` 实现 + 工厂 |
| `src/avox_ffmpeg/AvoxRuntimeFF.cpp` | `AvoxRuntime` 的 FFmpeg 实现 |
| `src/avox/video/MediaThumbRenderer.hpp/.cpp` | VK 抓帧渲染器（可先内联在 MediaReaderFF，超过 ~300 行再拆） |
| `src/avox/source/BaseSource.hpp/.cpp` | 字幕轨描述补字段 |
| `src/avox/AvoxSource.h` / `AvoxCodec.h` | 追加虚函数 |
| `src/avox/source/AVSource.cpp`、`src/avox_ffmpeg/IOParseFF.hpp` | 覆盖新 `ISourceInfo` 方法 |
| `CMakeLists.txt` | `copy_head("avox/AvoxMedia.h")`、`copy_head("avox/AvoxRuntime.h")` |
| `tests/test_media_reader.cpp` | 纯逻辑单测（错误串/状态机/桶映射/旋转映射） |

`createMediaReader` 放在 `avox_ffmpeg`，`#ifdef AVOX_ENABLE_FFMPEG` 内定义；
未启用 FFmpeg 时核心侧给 stub 返回 `nullptr`。`MediaReaderFF` 不导出，只导出
工厂与字符串函数。

### 5.2 open / 探测

1. `avformat_alloc_context()`，装 `interrupt_callback`（原子 cancel 标志）。
2. `avformat_open_input` + `avformat_find_stream_info`。
3. 逐 stream 填 `ISourceInfo`，**不按解码器支持过滤**（probe 要列全），
   但跳过 `AV_DISPOSITION_ATTACHED_PIC`（与 `IOParseFF.cpp:1158`、
   `ak_frames.cpp:311` 一致）：
   - 视频：`VTrackDesc`（trackId=st->index、codecId、width/height、fps、
     pixFmt→YuvType、colorSpace）；fps 照抄 `ak_probe` 的
     `avg_frame_rate → r_frame_rate`；display matrix 角度存 reader 内部，
     供 `getVideoRotation(index)` 返回。
   - 音频：`ATrackDesc`（trackId、codecId、channels、sampleRate、AudioFormat）。
   - 字幕：`STrackDescImpl`（trackId、SCodecId、lang、title、forced、default、
     codecName）。
   - 原始 codec 名按 track 存到 reader 自己的 `std::vector<std::string>`，
     供 `getVideoCodecName/getAudioCodecName`。
4. `durationMs` 只填容器时长；`bitRate`、`formatName`、`canSeek`
   （`pb && seekable & AVIO_SEEKABLE_NORMAL`）照 `ak_probe`。
5. `canSeek` 直接决定 `ISourceInfo::canSeek()`。

### 5.3 buildIndex

一个 `av_read_frame` 循环：

- 视频包：`AV_PKT_FLAG_KEY` → `(pts - start_time)` 转 ms，单调去重后入
  keyframes。
- 音频包：送 `avcodec_send_packet`，`receive_frame` 逐帧算峰值（U8/S16/S32/
  FLT/DBL，planar/interleaved 全支持，逻辑照抄 `ak_index.cpp:61-115`）。
- 进度：按 `avio_tell/avio_size` 或包 PTS 估算，每 1% 或 50ms 限频回调。
- 每包检查 cancel。
- EOF 后 `avcodec_send_packet(nullptr)` 排空尾帧。
- 结束后按 §2.6 的顺序确定有效时长与 `bucketMs`，再映射峰值到桶。
- 无视频轨/无音频轨都算成功；音频解码器打不开只影响 `waveAvailable`。

建议把 `frame_peak` 与桶映射抽成小函数放 avox（不要复制两份），单测覆盖
8/16/24/32bit、planar/interleaved。

### 5.4 grabFrame

1. 选轨：`videoTrack < 0` 时取第一条非 `ATTACHED_PIC`、宽高 > 0 的视频轨
   （与 `ak_frames.cpp:302-322` 同）。
2. `targetMs = clamp(request.timeMs, 0, duration-1)`（duration 已知时）。
3. `targetTs = origin + rescale(targetMs)`；`av_seek_frame(videoIndex,
   targetTs, AVSEEK_FLAG_BACKWARD)`；`avcodec_flush_buffers`；
   `metaExtractor->reset()`。
4. 向后解码，直到 `best_effort_timestamp >= target` 或包数 600 / 4s 预算
   （照抄当前值，防止坏文件卡死）；无 `best_effort_timestamp` 时用
   `targetMs` 作为 `actualMs`。
5. 对选中的 `AVFrame`：
   - 用帧级 `colorspace/color_range/color_trc` 生成 `ColorSpaceDesc`。
   - `ffYuvType(frame->format)`；`YuvType::other` → `unsupportedPixFmt`。
   - `YUVFrame` 直接引用 `frame->data/stride/linesize`（`SwVideoBuffer::form`
     会处理 padding/重排）。
6. 懒创建 `SurfaceRenderVk`（首次调用）：
   - `setOffSurface(YuvType::other)`
   - `setHdrMode(HdrMode::follow)`
   - `setColorSpace(cs)`
   - `setHdrMeta(latestHdr)` / `setDoviMeta(latestDovi)`
   - `enableImage(intermediateBuffer)`
7. **dummy 帧**：目标尺寸/`YuvType` 用真实首帧的（不要照 QEnhanceFilter 固定
   nv12，避免真实帧类型不同再触发一次重建）。渲染一张同尺寸黑帧让图重建落位。
8. `render(realYuv)`；`onRun()` 内部已 `vkWaitForFences`，返回后：
   - 旋转 0：直接交付 intermediate buffer；
   - 旋转 180/90/270：CPU 旋转到 rotateBuffer 后交付。
9. 填 `MediaFrameInfo{requestedMs, actualMs, 显示宽高}`，`onFrame(info, buf)`。
10. 源尺寸变化时（极少）先用该帧 render 一次重建，再 render 一次取输出；
    换目标宽度时新建 intermediate buffer，旧 buffer 等 dummy 重建完成后再释放。

旋转映射必须与 `ak_frames::frame_to_rgba` 同向：90/270 时中间图尺寸为
`(finalH, finalW)`，最终 `(finalW, finalH)`。例如编码 1920×1080 + 90°、
请求 `width=320`：显示高 = `round(320*1920/1080) = 569`，中间图 569×320，
旋转后交付 320×569（9:16）。用带角标（如左上角红点）的竖拍 fixture 做视觉
用例钉死方向，并断言最终宽高是 320×569 而不是 320×180。

### 5.5 AvoxRuntime

- `getFFmpegRuntimeInfo()`：直接返回 `av_version_info()` /
  `avcodec_configuration()` / 三个版本号。
- `isEncoderRegistered`：`avcodec_find_encoder_by_name`。
- `hasMuxer`：`av_guess_format(name, nullptr, nullptr)`。
- `getProtocolCount/Name`：首次调用用 `avio_enum_protocols` 缓存两个
  `std::vector<std::string>`，加 mutex；返回 `c_str()`。
- `isVulkanAvailable`：包装 `canVulkan()`；把 `canVulkan` 的静态缓存改成
  `std::once_flag`。

### 5.6 构建 / ABI

- 新头文件加入 `copy_head`（`CMakeLists.txt:352` 一带）。
- `ISourceInfo`/`ISTrackDesc` 只追加到末尾；`MediaReader` 是全新接口。
- 不改 `VTrackDesc`/`ATrackDesc` 结构体大小（原始 codec 名走 `ISourceInfo`
  方法，不加结构体字段）。
- `swig/common.i` 若不打算暴露 reader，可不动；若要暴露，加
  `%include "avox/AvoxMedia.h"` 与 `%feature("director") IMediaReaderOb;`。
- 无 FFmpeg / 无 Vulkan 两种构建都要能编：前者工厂返回 nullptr，后者抓帧
  `renderUnavailable`。

---

## 6. avoxkit 迁移

### 6.1 shim 封装

`native/avtool` 实现一个同步等待的 observer：

```cpp
struct SyncOb : avox::IMediaReaderOb {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  // onReady/onIndexDone/onFrame/onError 里加锁置结果 + done=true + notify
};
// 调用 open/buildIndex/grabFrame 前 reset done;
// 调用返回后 lock 检查 done, 未完成再 cv.wait_for(超时);
// 超时则 reader->cancel() 并报错。
```

不要在持有 mutex 的情况下调用 reader（同步实现的回调会在同线程加锁）。
`onFrame` 内立即用 `getImageBase64/getImageBytes` 编码，不跨回调持有 buffer。

### 6.2 四个文件

| shim | 迁移后 |
|---|---|
| `ak_probe.cpp` | `createMediaReader` → `open` → 等 `onReady` → 读 `ISourceInfo`；`format/duration_ms/bit_rate/can_seek/video/audio/subtitles` 字段名不变；视频/音频 `codec` 用 `getVideoCodecName/getAudioCodecName`，字幕 `codec` 用 `codecName()`、`text` 用 `isTextSubtitleCodec(codecName())`；视频轨增加 `rotation` 与 `display_width/display_height`（由 `getVideoRotation()` 和编码宽高算），供 UI 在缩略图到达前按 9:16 布局；带封面的音频文件不再报视频轨（attached_pic 被排除，见 §2.11）；`pix_fmt/sample_fmt/每轨 bit_rate` 可删（Dart 无消费者） |
| `ak_index.cpp` | `open` → `buildIndex`；`onIndexDone` 里拷 peaks/keyframes；`has_audio=result.hasAudio`；peaks 按 `(int)(sqrt(p)*100)` 转 0-100；`duration_ms/bucket_ms` 取 result |
| `ak_frames.cpp` | `open` → 每个时间点 `grabFrame`；`onFrame` 内 JPEG 编码；`reason` 映射 `MediaError`（`unsupportedPixFmt→unsupported_pix_fmt`、`renderUnavailable→render_unavailable`、`decoderMissing→decode_failed`）；目标时间生成（count/等距）留在 shim |
| `ak_engine.cpp` | `getFFmpegRuntimeInfo/isEncoderRegistered/hasMuxer/协议列表/isVulkanAvailable`；删 libav include；`encoder_probe_impl` 语义不变 |

`ak_media_index` 对直播/未知时长返回
`{"ok":false,"code":"unsupported",...}`；`ak_media_frames` 无 Vulkan 返回
`{"ok":true,"has_video":true,"frames":[],"reason":"render_unavailable",...}`。

### 6.3 构建

- `native/avtool/CMakeLists.txt`：去掉 `avformat.lib/avcodec.lib/avutil.lib/
  swresample.lib` 链接；去掉 `KIT_DIR/include`（让残留 `#include <libav...>`
  直接编译失败）。**保留 `KIT_DLLS` 拷贝**——`avox.dll` 运行时仍从这些 DLL 取
  FFmpeg 实现，删了会加载失败。
- `ak_capabilities_json` 顶层加 `"vulkan": isVulkanAvailable()`；
  `features.media_index/media_frames` 保持 true。
- Dart：`Engine` 读 `capabilities['vulkan']`；`MediaFrameSource` 在
  `vulkan=false` 或 frames 返回 `render_unavailable` 时给出
  “Vulkan 组件未启用，缩略图/画面预览不可用”，不再静默空白。
- 更新 `docs/architecture.md` 的“功能 → 引擎映射”、`native/avtool/README.md`。

### 6.4 阶段划分

1. **P0 接口骨架**：公共头 + stub + `AvoxRuntime` + 字符串函数；avox 构建通过。
2. **P1 probe/index**：纯 CPU demux/decode，先替换 `ak_probe/ak_index`，
   跑 golden 对比。此时 `ak_frames` 仍用旧 libav。
3. **P2 抓帧**：VK 渲染器 + 旋转 + HDR/DV 元数据；替换 `ak_frames`。
4. **P3 清账**：`ak_engine` 换 runtime API；删链接/头路径；能力位与 Dart 提示；
   文档；全量回归。
5. **P4（可选）handle API**：clip 页复用长生命周期 reader，省掉每次拖动重建
   VK 图；不在本次范围。

### 6.5 UI 显示比例（9:16）

reader 返回的 `width/height` 已经是显示尺寸，但当前 avoxkit UI 仍按 16:9
摆放（§4.6）。迁移时一起改：

| 位置 | 改动 |
|---|---|
| `lib/core/media_frames.dart` | `MediaFrames` 的 `width/height` 已是显示尺寸，保留；可直接算 `aspectRatio = width/height` |
| `lib/core/media_frame_source.dart` | 暴露 `aspectRatio`（取 `strip.width/height`） |
| `lib/ui/widgets/frame_preview.dart` | 去掉默认 `16/9`，由 source/调用方传比例；`BoxFit.contain` |
| `lib/ui/widgets/filmstrip.dart` | 增加 `frameAspect`，`naturalPx = height * frameAspect`；不要 `BoxFit.cover` 裁切 |
| `lib/ui/widgets/trim_timeline.dart` | 把 `MediaFrames.width/height` 传给 `AkFilmstrip` |
| `lib/ui/pages/clip_page.dart` | 220×124 固定槽位改为按比例算宽度（固定高 124，宽 = 124*aspect） |
| `lib/ui/widgets/live_preview.dart` / 任务页 | 转码/截取预览按源显示比例放原生窗口矩形；外层用高度反算宽度 |
| `lib/core/convert_plan.dart` 等 | 需要显示尺寸时优先用 probe 的 `display_width/display_height`，不要用编码宽高 |

竖屏在固定高度区域里自然更窄（9:16），不要为了填满横向空间去裁切或拉伸。
如果 UI 确实需要统一卡片高度，用“高度固定、宽度按比例、左右留白”的方式，
而不是把图 `cover` 到 16:9。

---

## 7. 验收

### 7.1 迁移前先留 golden

用当前构建的 `ak_smoke probe/index/frames`（或 `ak_features_test`）对同一组
fixture 输出 JSON/JPEG，存到临时目录（不入库，或存 avox-test 私有仓）。
迁移后逐字段对比。

### 7.2 fixture

- H.264/AAC MP4（SDR，基准）
- HEVC MKV 10bit
- HDR10 PQ（静态元数据）
- DV P5 MP4（RPU，色彩重点）
- 竖拍 9:16（编码 1920×1080 + display matrix 90°，带角标）
- 横拍 16:9（对照组）
- 无音轨
- 音频文件带封面（attached_pic）
- 12bit / 444P10（预期 `unsupported_pix_fmt`）
- 坏文件
- 长 GOP / 长片（600 包/4s 预算）
- 两个并发读者（VK 队列并发）

### 7.3 判据

- `ak_probe`：`format/duration_ms/bit_rate/can_seek/video/audio/subtitles`
  字段与迁移前一致；视频/音频 `codec` 原始名一致；字幕 `default/forced/text`
  一致。
- `ak_index`：`duration_ms/bucket_ms/keyframes_ms` 完全一致；`peaks` 逐桶
  ±1；`has_audio` 一致；无音轨/解码器缺失两种空 peaks 场景一致。
- `ak_frames`：`actual_ms/duration/width/height/旋转方向` 一致；SDR JPEG
  视觉无明显差异；HDR/DV 允许差异（新路径做 tone map/DV，旧路径近似）。
- 9:16 竖拍：`ak_media_frames` 输出 `width=320, height≈569`，JPEG 为
  320×569 且方向正确；`ak_probe` 的 `rotation=90`、
  `display_width/display_height=1080×1920`；UI 预览/缩略图条按 9:16 放置，
  无裁切、无拉伸、无 16:9 黑边。
- 封面图：带 attached_pic 的 MP3/M4A，`ak_probe` 不再输出 `video` 项，
  `ak_media_frames` 返回 `has_video=false`；普通音频播放/提取不受影响。
- 无 Vulkan：probe/index 正常；frames 返回 `render_unavailable`；UI 有提示。
- 取消：长片 index 中从另一线程 `cancel()` → `onError(index,cancelled)` →
  状态 ready → 可再次 index。
- 生命周期：open 前 `getSourceInfo()==nullptr`；正常
  `open→ready→index→frame→close`；close 返回后无回调；close 后可重新 open。
- 内存：同一 reader 连续 100 轮 index/frame，RSS 稳定；reader 析构后 VK 资源
  释放（`graph->waitIdle` 正常返回）。
- 并发：两个线程各自 create reader、并发 grabFrame，不崩、输出正确
  （`VkCommand::submit`/`graph->run` 已有 queue 锁）。
- 构建：`grep -r "libav" avoxkit/native/avtool/src` 无结果；去掉
  `KIT_DIR/include` 后能编；`dumpbin /dependents avoxkit_native.dll` 只有
  `avox.dll`（及系统库），FFmpeg 由 `avox.dll` 传递依赖。
- ABI：avox 重建；avoxkit/playtest runner/panvox 重建后跑一遍冒烟（追加虚函数
  对旧二进制安全，但发布仍统一重建）。

### 7.4 单测

- `tests/test_media_reader.cpp`：错误/状态字符串；桶映射与峰值归一化；
  旋转映射；状态机（fake reader）；`getFFmpegRuntimeInfo` 版本 > 0；
  `isEncoderRegistered("libx264")/("aac")`。
- avox-test：新增 media reader 集成用例（probe/index/frames + HDR/DV/竖拍），
  走离线子集。

---

## 8. 拍板记录

| # | 事项 | 结论 |
|---|---|---|
| 1 | 不支持像素格式（12bit/444P10/打包格式） | **已定（1029）**：按 A，VK 支持范围 + 明确 `unsupportedPixFmt`；补 `gray8` 映射；不保留 CPU 兜底 |
| 2 | v1 网络范围 | **已定（1029）**：只承诺本地文件/挂载盘/UNC；HTTP/RTSP v1 返回 unsupported，v2 走 avox IO |
| 3 | 封面图口径 | **建议排除**（与 `IOParseFF`/`ak_frames` 一致）；带封面音频的 `ak_probe` 不再报视频轨。待你确认 |
| 4 | `closed` 状态 | **已定（1029）**：去掉，close 回 idle，reader 可复用 |
| 5 | `structSize` 首字段 | 加，便于以后追加字段 |
| 6 | observer 挂载方式 | 保留 `setObserver`（单观察者）；若要统一仓内风格可改 `addMediaReaderOb/remove` |
| 7 | 原始 codec 名位置 | `ISourceInfo` 追加 `getVideoCodecName/getAudioCodecName`；字幕走 `ISTrackDesc::codecName()` |
| 8 | `MediaIndexResult` 增加 hasAudio/waveAvailable | 必加，否则 shim 无法保持现有 JSON 语义 |
| 9 | 旋转 | 缩图后 CPU 旋转（本方案）；给 `VkVideoRender` 补 transpose 是另一个改动 |
| 10 | 重复 seek 的 VK 图复用 | v1 每次 grabFrame 复用同一 reader 的图；shim 每次调用新建 reader（可接受）；P4 再考虑 handle API |
| 11 | 9:16 竖拍 UI | **已定（1029）**：缩图与 UI 槽位按显示比例 9:16，不转成 16:9、不裁切；`ak_probe` 补 `rotation/display_width/display_height`，UI 消费 `MediaFrames.width/height` |

---

## 9. 与 avoxkit 设计稿的差异清单

`avoxkit/docs/avox-media-reader-api.md` 需要同步修正：

1. §2.3 删除“HDR v1 按 SDR 近似”，改为 VK tone map。
2. §2.3 的 48–1280 与 §3 的 48–1920 统一为 48–1280。
3. §2.3 的“8/10/12bit 都能缩放”改为“VK 支持范围 + 其余明确 unsupported”。
4. §3 `MediaIndexResult` 增加 `hasAudio/waveAvailable/structSize`。
5. §3 `MediaIndexOptions/MediaFrameRequest` 的轨索引改为“容器 stream index”。
6. §4 “索引要求有限时长、直播 unsupported”与 duration 回退顺序统一：
   容器→stream→音频末帧；直播才 unsupported。
7. §4 补 HDR/DV 元数据来源（MetaExtractor、DOVI_CONF、avcc）。
8. §4 补取消 open 的 `AVIOInterruptCB`。
9. §4 抓帧补“懒创建 VK、dummy 用真实首帧类型/尺寸、换尺寸 rebuild”。
10. §5.6 v1 网络范围收窄为本地；HTTP 明确 v2。
11. §7 验收补封面图、无 Vulkan、并发 reader、`canVulkan` 线程安全。
12. 全文“MediaInfo”已合并进 `ISourceInfo` 的表述保留；补
    `getVideoCodecName/getAudioCodecName`、`getVideoRotation` 与
    `ISTrackDesc::codecName/isDefault`。
13. §3/§4 明确 `width` 是旋转后的显示宽度：竖拍 9:16 请求 320 应输出
    320×569，不是 320×180；`MediaFrameInfo`/`ak_media_frames` 的
    `width/height` 都是显示尺寸。
14. §6 avoxkit 迁移补 UI 比例：`AkFramePreview`/`AkFilmstrip`/
    `AkLivePreview` 不再硬编码 16:9，消费 `MediaFrames.width/height`。
15. §4 封面图口径按 §2.11 改为**排除** attached_pic（probe 行为变化视为修正），
    不再写“ISourceInfo 保留、选择跳过”。

---

## 10. 非目标

- 播放端 display matrix 自动旋正（本次只修缩略图/预览的显示方向；播放器窗口
  里的竖拍横躺是独立问题）。
- URL/HLS/RTSP/SMB/WebDAV/代理 IO（v2）。
- 图形字幕 OCR、字幕烧录、翻译。
- 在 avox 内做 JPEG/PNG 编码（仍由调用方用 `getImageBase64/getImageBytes`）。
- 硬解抓帧（v1 软件解码；后续可把硬解帧直接喂 `GpuFrame`，接口不变）。
- 封面图作为独立媒体资源展示（本次只从视频轨里排除；`getCoverImage` 另议）。
