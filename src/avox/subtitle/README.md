# Subtitle 字幕模块

统一管理字幕文件、语音识别(ASR)和翻译，为 MediaPlayer/SourcePlayer 提供字幕能力。

## 架构

```
SubtitleView : public ISubtitle, public ISurfaceRenderOb
├── unique_ptr<Clock>           // 时间同步
├── unique_ptr<SubtitleFile>    // 文件字幕（SRT）
│   └── IOnnxTranslator         // 查找时翻译
└── unique_ptr<SubtitleAsr>     // ASR 管理
    ├── AudioStt                // 语音识别器（识别线程）
    └── IOnnxTranslator         // 翻译器（翻译线程）
```

### 线程模型

**streaming 模式**：
- 识别线程：流式识别，~100ms 延迟
- 无翻译，快速显示

**ptsSync 模式**：
- 识别线程：离线识别，~500ms
- 翻译线程：神经网络翻译，~500ms
- 两个线程并行处理，总延迟约 1s

## 核心接口

```cpp
// 公共接口
class ISubtitle {
    virtual bool loadSrt(const char* path) = 0;    // 加载 SRT
    virtual void enableAsr(AsrMode mode) = 0;      // 启用 ASR
    virtual void close() = 0;                       // 关闭
    virtual void enableTranslation() = 0;          // 启用翻译
    virtual void disableTranslation() = 0;         // 禁用翻译
};

// 内部接口
void setWindowRender(ISurfaceRender* render);            // 绑定渲染窗口
void setAudioDesc(AudioDesc desc);                      // 设置音频格式
void inputSpeech(const AvoxData& data, int64_t pts);     // 输入音频
Clock* getClock();                                      // 获取时钟
```

## 识别模式

| 模式 | 场景 | 识别器 | 翻译 | 总延迟 |
|------|------|--------|------|--------|
| `streaming` | SourcePlayer 实时采集 | 流式 | 否 | ~100ms |
| `ptsSync` | MediaPlayer 视频播放 | 离线 | 是 | ~1s |

## 同步机制

### streaming 模式

识别快（~100ms），直接显示，无需特殊同步：

```
音频输入 → 识别线程 → onPartialResult → 立即显示
```

主打快速，边说边显示。

### ptsSync 模式

识别和翻译较慢，通过音频缓冲队列延迟渲染：

```
音频解码帧
    │
    ├──────────────────────────────┐
    │                              │
    ▼                              ▼
识别线程 (~500ms)            frameQueue (4秒缓冲)
    │                              │
    ▼                              ▼
识别结果                     渲染 → 播放
    │                         (延迟约1s)
    ▼
翻译线程 (~500ms)
    │
    ▼
subtitleQueue (PTS标签)
    │
    ▼
按PTS查找显示
```

**关键**：渲染队列领先约 1 秒，识别和翻译在各自线程并行处理，完成后按 PTS 同步显示。

## 使用示例

### MediaPlayer（ptsSync 模式）

```cpp
// 初始化
subtitleView->setWindowRender(windowRender);
subtitleView->setAudioDesc(audioDesc);
subtitleView->enableAsr(AsrMode::ptsSync);
subtitleView->enableTranslation();  // 可选

// 音频解码后输入（AudioTrack::onDecode）
subtitleView->inputSpeech(audioData, pts);

// 渲染线程自动调用 onRender()，按 PTS 显示字幕
```

### SourcePlayer（streaming 模式）

```cpp
// 初始化
subtitleView->setWindowRender(windowRender);
subtitleView->setAudioDesc(audioDesc);
subtitleView->enableAsr(AsrMode::streaming);  // 用户手动启用

// 音频帧输入
subtitleView->inputSpeech(audioData, 0);

// 渲染线程自动显示 getStreamingText()
```

## 组件职责

| 组件 | 职责 |
|------|------|
| `SubtitleView` | 对外协调，渲染调度 |
| `SubtitleAsr` | ASR 模式管理，翻译调度 |
| `SubtitleFile` | SRT 解析，查找翻译 |
| `AudioStt` | 语音识别（内部线程） |
| `Clock` | 时间同步，外部操作 |
