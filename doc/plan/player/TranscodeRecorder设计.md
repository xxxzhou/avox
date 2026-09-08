# TranscodeRecorder 设计

将网络流/本地文件解码、通过 VkVideoRender 处理（缩放/水印等）后，重新编码保存。

## 1. 数据流

解码与编码通过队列解耦，队列满时阻塞解码，反压到 IO 层。GPU 处理在解码回调线程完成，处理后的帧拷贝入队。

```
[解码线程]                              [编码线程 - RunTask]
AMediaSource
  → onVideoFrame:
      VkVideoRender::renderFrame(frame)    videoQueue出队 → processVideo → RawMuxer::pushFrame
      → getCpuFrame → copyBufHost 入队      audioQueue出队 → processAudio → RawMuxer::pushFrame
  → onGpuFrame:
      copyBufGpu 入队
  → onAudioFrame:
      copyAudioBuf 入队
```

## 2. 类定义

```cpp
// src/avox/muxer/TranscodeRecorder.hpp

class TranscodeRecorder : public IRecorder,
                          public IRawSourceOb,
                          public RunTask,
                          public Observer<IRecorderOb> {
 public:
  TranscodeRecorder();
  virtual ~TranscodeRecorder();

 protected:
  std::unique_ptr<AMediaSource> source;
  std::unique_ptr<RawMuxer> muxer;
  std::unique_ptr<VideoRender> videoRender;       // VkVideoRender (AVOX_ENABLE_VULKAN)
  RecorderState state = RecorderState::none;
  IoPlan ioPlan = IoPlan::ffmpeg;
  MuxerType muxerType = MuxerType::ffmpeg;
  std::string inputUrl;
  std::string outputFile;
  bool bHardDecode = false;
  // 默认走平台原生硬编(AndVEncoder/IOSVEncoder/ff_*_dx11); 需要更小文件可显式关走 FFmpeg 软编
  bool bHardEncode = true;
  VideoDesc outVideoDesc = {};
  AudioDesc outAudioDesc = {};
  bool bSetOutVideo = false;
  bool bSetOutAudio = false;
  std::atomic<bool> bDraining{false};
  // 帧队列，上限5，满时阻塞解码线程，反压到 IO 层
  RingBuffer<VideoFramePtr> videoQueue{5};
  RingBuffer<AudioFramePtr> audioQueue{5};
  ACodecId aCodecid = ACodecId::aac;
  // 默认 H.264: 硬编兼容性远好于 h265(低端安卓/老设备 hevc 编码器常缺失), 兼容性敏感的转码录不赌设备能力
  VCodecId vCodecId = VCodecId::h264;

  // IRecorder
 public:
  virtual void setIoPlan(IoPlan plan) override;
  virtual void setMuxerType(MuxerType type) override;
  virtual bool open(const char* url, const char* file) override;
  virtual void close() override;
  virtual RecorderState getState() override;

 public:
  void setVideoDesc(const VideoDesc& desc);
  void setAudioDesc(const AudioDesc& desc);
  VideoRender* getVkVideoRender();

  // IRawSourceOb - 解码回调
 public:
  virtual void onReady() override;
  virtual void onClose() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame, int32_t trackId) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId) override;
  virtual void onAudioFrame(const AvoxAFrame& frame, int32_t trackId) override;

  // RunTask - 编码线程
 protected:
  virtual void onRunTask() override;

 private:
  void processVideo(VideoFramePtr frame);
  void processAudio(AudioFramePtr frame);
};
```

## 3. 关键流程

### 3.1 构造

- 创建 `VkVideoRender`（离屏，不绑定窗口），用于 GPU 帧处理
- 默认软解 + 平台原生硬编（H.264）；需要更小文件经 `rec.hard.encode=false` 走 FFmpeg 软编
- 音频默认编码格式：AAC, S16, 32000Hz, 2ch

### 3.2 open

1. 创建 `AMediaSource`，设置 URI、硬解开关，注册本对象为 observer
2. 创建 `RawMuxer`，设置 MuxerType、硬编开关
3. `startTask()` 启动编码线程（RunTask）
4. `source->open()` 启动解码

### 3.3 onReady（解码就绪，解码线程回调）

1. `muxer->open(outputFile)` 打开封装
2. 配置视频：
   - `muxer->setVideoCodec(vCodecId)`
   - 根据硬编/软编设置 `videoRender->enableYuvOut()`：硬编 NV12，软编 YUV420P
   - 如 `bSetOutVideo` 且宽高与源不同，调用 `videoRender->enableSizeChange()`
   - `muxer->setInVideoDesc(vdesc)`
3. 配置音频：
   - `muxer->setAudioCodec(aCodecid)`
   - 如 `bSetOutAudio` 且有效，`muxer->setAudioDesc(outAudioDesc)`（触发重采样）
   - `muxer->setInAudioDesc(aTracks[0])`
4. `muxer->ready()`，state → recording

> 音频格式修正：`AMediaSource::onAudioDesc()` 在解码器打开后拿真实 PCM 输出格式（`audioDecoder->getOutDesc()`），通过 `setAudioDesc` 回调到 TranscodeRecorder，再传给 muxer。避免了源 track desc 与实际解码输出格式不匹配导致的杂音问题。

### 3.4 onVideoFrame（解码线程，入队前 GPU 处理）

```
videoRender->renderFrame(frame)    // GPU pipeline: YUV→RGBA→effects→RGBA→YUV(NV12/YUV420P)
  ↓
videoRender->bCpuOut()             // 确认 CPU 输出启用
  ↓
videoRender->getCpuFrame(yframe)   // GPU readback 到 CPU
  ↓
videoQueue.enqueueWait(yframe, copyBufHost)  // 拷贝入队，满则阻塞
```

> GPU 处理在解码线程完成是为了避免 GPU 共享 buffer 被下一帧覆盖。处理后的帧通过 `copyBufHost` 深拷贝入队，释放 GPU 资源。

### 3.5 onGpuFrame（硬解 GPU 帧）

```
videoQueue.enqueueWait(frame, copyBufGpu)  // GPU 帧拷贝入队
```

### 3.6 onAudioFrame

```
audioQueue.enqueueWait(frame, copyAudioBuf)  // 音频 PCM 拷贝入队
```

### 3.7 onRunTask（编码线程）

```
while (running() || !videoQueue.empty() || !audioQueue.empty()) {
    videoQueue.dequeue → processVideo → muxer->pushFrame(yframe)
    audioQueue.dequeue → processAudio → muxer->pushFrame(aframe)
    // IO 源结束且两队全空 → state = completed，退出
    sleepTask(true)
}
// 清理
source->close(); source.reset();
muxer->close(); muxer.reset();
dispatch(&IRecorderOb::onComplete)
```

### 3.8 processVideo

1. `SwVideoBuffer::to(yframe)` 还原 YUVFrame 结构
2. 恢复 pts/dts（使用源帧 PTS，非 wall-clock）
3. `muxer->pushFrame(yframe)`

### 3.9 processAudio

1. 从 `AudioFramePtr` 取 PCM 数据指针和 pts
2. `muxer->pushFrame(aframeOut)`

### 3.10 close

1. `state = stopped`
2. `stopTask()` 停止编码线程

## 4. 编码器配置（FFVEncoder）

`onPreEncoder()` 按实际选中的编码器名称分发 preset/码控参数：

| 编码器 | preset | 码控 |
|--------|--------|------|
| libx264 / libx265 | `ultrafast` | CRF 23, bit_rate=0 |
| \*_qsv (Intel) | `veryfast` | 保持计算的 bit_rate |
| \*_nvenc (NVIDIA) | `p1` + `tune=ll` | 保持计算的 bit_rate |
| \*_amf (AMD) | `quality=speed` | 保持计算的 bit_rate |

- PTS 使用源帧 PTS（`yframe.pts`），time_base = {1, 1000}（毫秒）
- 禁用 B 帧，4 秒 GOP

## 5. 队列策略

| 参数 | 值 | 说明 |
|------|---|------|
| videoQueue 容量 | 5 | 满时阻塞解码线程，反压到 IO 层 |
| audioQueue 容量 | 5 | 同上 |
| enqueue 模式 | `enqueueWait` + 拷贝 | 阻塞等待 + 深拷贝入队 |
| dequeue 模式 | 非阻塞轮询 | `dequeue()` 取到就处理，无数据 `sleepTask(true)` |

## 6. 帧处理职责划分

| 组件 | 线程 | 职责 |
|------|------|------|
| AMediaSource | 解码线程 | IO 解封装 + 音视频解码 |
| VkVideoRender | 解码线程 | GPU 帧处理：YUV 格式转换/缩放/水印/LUT/字体 |
| RingBuffer (videoQueue/audioQueue) | 跨线程 | 解码与编码线程解耦，5帧上限，满则阻塞 |
| RawMuxer | 编码线程 | 编码（FFVEncoder/FFAEncoder）+ FFmpeg 封装 |
| FFVEncoder | 编码线程 | 视频编码器，支持 x264/x265/QSV/NVENC/AMF |
| FdkaacEncoder | 编码线程 | AAC 音频编码（fdk-aac），输入格式 S16 |

## 7. 与 StreamRecorder 对比

| | StreamRecorder | TranscodeRecorder |
|---|---|---|
| 数据源 | AVSource（编码包） | AMediaSource（解码帧） |
| 帧处理 | 无 | VkVideoRender（GPU，解码线程） |
| 编解码 | 无（纯 remux） | 解码 + 编码 |
| 线程模型 | 无额外线程 | RunTask 编码线程 |
| 缩放/水印 | 不支持 | 支持 |
| 速度 | 极快（IO 瓶颈） | 受限于解码+GPU+编码 |

## 8. 已知约束

- **GPU 往返开销**：即使无需缩放/滤镜，解码帧仍需经过 GPU graph（YUV→RGBA→YUV）再 readback 到 CPU，每帧约 3~5ms GPU 等待 + 计算。无效果场景可考虑 bypass GPU 直通优化。
- **Vulkan 同步**：`VkPipeGraph::onRun()` submit 后 CPU 等待 fence 完成，保证 graph 输出与下游 window/present 无 race（修复了软解画面撕裂问题）。代价是 CPU 阻塞等 GPU。
- **解码为瓶颈**：软解模式下 hyperfast encoder 快于解码，吞吐由解码+GPU 链决定。
- **硬编 CPU→GPU 二次上传**：当前 graph readback 到 CPU 后再 upload 给 QSV/NVENC，非零拷贝路径，硬编优势无法充分发挥。

## 9. 工厂函数

```cpp
AVOX_EXPORT IRecorder* createTranscodeRecorder();
```

## 10. 测试程序

`transcoderecordertest` — 创建 TranscodeRecorder，打开源文件并转码输出。