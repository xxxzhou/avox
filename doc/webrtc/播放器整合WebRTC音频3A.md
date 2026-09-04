# 播放器整合WebRTC音频3A处理

在完成音视频数据源框架以及编码，复用器相关的功能后，先在windows平台整合录音/录屏及相应的编码然后推流出去，发现声音有噪音及回音现象，记录下整合WebRTC音频3A的流程。

3A处理有二个比较常见库，一个是speex,一个是webrtc，但是speex已经很久没更新了，webrtc的3A处理还一直完善中，添加了许多新的处理，加上前期把完成的H265硬解/aac外接到webrtc模块中，因此在这选择webrtc的3A处理。

## 前期处理

写音频的3A算法复杂，整合就比较简单了，本文主要记录整合3A算法需要注意的点，首先对音频格式有要求，比如老版本的3A不能处理超过16000采样率，现在44100好像还是不行，但是32000/48000可以了，需要S16的格式，S8/float都不行，最新可以处理单/双声道，超过双声道不行，平面的也不行，需要交叉格式。其次需要传入固定大小的音频数据，WebRTC是要求10ms的数据，所以需要针对PCM数据做分段处理。

重采样与分段几乎是3A处理都需要的，所以这部分处理放到基类中。

```c++
class IAudioProcessOb {
public:
  virtual ~IAudioProcessOb() = default;
  virtual void OnAudioProcess(const AvoxAFrame &frame) {};
};

// 音频处理基类
// AudioProcess 自身包含重采样，分隔固定音频数据
// 子类RtcAudioProcess 包含webrtc里的3A处理
class AudioProcess : public Observer<IAudioProcessOb> {
public:
  AudioProcess() = default;
  virtual ~AudioProcess() = default;

protected:
  AudioDesc desc = {};
  // 音频处理可能要求特定的输出格式，如采样率需要是16000的倍数
  AudioDesc outDesc = {};
  // 是否启用降噪
  bool enableNs = false;
  // 是否启用回声消除
  bool enableAec = false;
  // 是否启用增强
  bool enableAgc = false;
  // webrtc音频处理需要10ms的音频数据
  AudioFrame curFrame = {};
  int32_t bufferPreMs = 0;
  // 重采样,如果音频处理需要特定格式,需要重采样
#ifdef AVOX_ENABLE_FFMPEG
  std::unique_ptr<FFResample> resample = nullptr;
#endif
  // 处理后的buffer
  std::vector<uint8_t> pbuffer;

public:
  // desc可能被修改，使用AudioProcess的需要注意使用返回的desc
  bool initConfig(const AudioDesc &desc, AudioDesc &outDesc);
  void process(const AvoxAFrame &frame);

protected:
  virtual bool onInit() { return true; };
  // process填充curFrame满后，调用onProcess
  virtual void onProcess() {};
};
bool AudioProcess::initConfig(const AudioDesc &desc_, AudioDesc &outDesc_) {
  desc = desc_;
  outDesc = desc;
  if (!onInit()) {
    return false;
  }
  outDesc_ = outDesc;
  bufferPreMs = getSizePreMs(outDesc);
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  resample = std::make_unique<FFResample>();
  resample->init(desc, outDesc);
#endif
  // 默认10ms的buffer
  curFrame.setSize(10 * bufferPreMs);
  curFrame.setPts(AVOX_NOVALID_PTS);
  pbuffer.resize(10 * bufferPreMs);
  return true;
}

void AudioProcess::process(const AvoxAFrame &frame) {
  uint8_t *data = frame.buffer.data;
  int32_t size = frame.buffer.size;
#ifdef AVOX_ENABLE_FFMPEG
  AvoxData inData = {data, size, true};
  int ret = resample->resample(inData);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "resample failed");
    return;
  }
  data = inData.data;
  size = ret;
#endif
  // frame的开始pts
  int64_t spts = frame.pts;
  // 第一次进来，初始化当前包的时间戳
  if (curFrame.getPts() == AVOX_NOVALID_PTS) {
    curFrame.setPts(spts);
  }
  // 写入数据,数据每次组成固定bufferMs的长度bufferSize
  while (size > 0) {
    // 当前BUFFER剩余空间
    int32_t spaceleft = curFrame.spaceLeft();
    if (size >= spaceleft) {
      // 写入当前BUFFER并写满
      curFrame.writeBytes(data, spaceleft);
      // 通知子类处理
      onProcess();
      // 开始新的curFrame
      curFrame.clear();
      // 下一帧的开始时间
      spts += spaceleft / bufferPreMs;
      curFrame.setPts(spts);
      // 剩余数据继续
      data += spaceleft;
      size -= spaceleft;
    } else {
      // 写入当前BUFFER未满
      curFrame.writeBytes(data, size);
      size = 0;
    }
  }
}
```

重采样就直接用的ffmpeg封装的，没有用webrtc本身的，webrtc重采样限制多些，比如只有S16格式，没有planned格式等，需要注意ffmpeg里planned格式，在调用swr_convert时，相应的in/out指针需要指向planned的数据，比如双通道，需要指明相应的[1]指针位置，否则会出错。

## WebRTC 3A整合

这里简单整合了下3A，具体算法及参数并没具体设置，后面根据需求再做修改。

```c++
// 具体应用WebRTC的3A处理
class RtcAudioProcess : public AudioProcess {
public:
  RtcAudioProcess();
  virtual ~RtcAudioProcess();

private:
  webrtc::scoped_refptr<webrtc::AudioProcessing> apm = nullptr;
  webrtc::ProcessingConfig pConfig = {};

public:
  virtual bool onInit() override;

protected:
  virtual void onProcess() override;
};

RtcAudioProcess::RtcAudioProcess() {
  enableAec = true;
  enableNs = true;
  enableAgc = true;
}

RtcAudioProcess::~RtcAudioProcess() {
  // if (apm) {
  //   apm.reset();
  // }
}

bool RtcAudioProcess::onInit() {
  outDesc = desc;
  // 配置WebRTC音频处理流的格式
  if (desc.sampleRate == 44100) {
    outDesc.sampleRate = 48000;
  }
  if (desc.format != AudioFormat::AVOX_AUDIO_S16) {
    outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  }
  if (desc.channels > 2) {
    outDesc.channels = 2;
  }
  AudioProcessing::Config config;
  // 配置回声消除
  config.echo_canceller.enabled = enableAec;
  config.echo_canceller.mobile_mode = false;
  // 配置噪声抑制
  config.noise_suppression.enabled = enableNs;
  config.noise_suppression.level =
      AudioProcessing::Config::NoiseSuppression::kHigh;
  // 配置自动增益控制
  config.gain_controller2.enabled = enableAgc;
  // 测试 声音增大二倍
  // config.gain_controller2.fixed_digital.gain_db = 10.0f;
  apm = BuiltinAudioProcessingBuilder(config).Build(CreateEnvironment());
  if (!apm) {
    LOGFLF(LogLevel::warn, "failed to create WebRTC AudioProcessing instance");
    return false;
  }
  // 输入流配置
  pConfig.streams[0] = StreamConfig(outDesc.sampleRate, outDesc.channels);
  // 输出流配置
  pConfig.streams[1] = StreamConfig(outDesc.sampleRate, outDesc.channels);
  if (apm->Initialize(pConfig) != 0) {
    LOGFLF(LogLevel::warn,
           "failed to initialize WebRTC AudioProcessing with sample rate:",
           desc.sampleRate);
    return false;
  }
  return true;
}

void RtcAudioProcess::onProcess() {
  if (!apm) {
    return;
  }
  int32_t ret = apm->ProcessStream(
      (const int16_t *)curFrame.point(), pConfig.input_stream(),
      pConfig.output_stream(), (int16_t *)pbuffer.data());
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "WebRTC AudioProcessing process failed");
    return;
  }
  AvoxAFrame frame = {};
  frame.buffer = {pbuffer.data(), (int32_t)pbuffer.size(), true};
  frame.pts = curFrame.getPts();
  dispatch(&IAudioProcessOb::OnAudioProcess, frame);
}
```

## 音频源/渲染整合

应用3A，有二个常见的地方，一个是音频源，一个是音频渲染。音频源是在如录音等场景，需要应用降噪/回声消除等，音频渲染是应用在如播放器等场景，需要应用自动增益控制等。

```c++
// 音频源,如果有3A算法，截获音频源的音频数据，进行3A处理，然后再传递出去
class AudioSource : public IAudioProcessOb,
                    public DeviceSource<IAudioSourceOb> {
public:
  AudioSource();
  virtual ~AudioSource() = default;

protected:
  AudioDesc desc = {};
  // 是否添加音频处理
  bool enableProcess = false;
  // 音频处理能否初始化
  bool initProcess = false;
  // 音频3A处理
  std::unique_ptr<AudioProcess> audioProcess = nullptr;

protected:
  void onFrame(const AvoxAFrame &frame);

  // IAudioProcessOb
public:
  virtual void OnAudioProcess(const AvoxAFrame &frame) override;
};

AudioSource::AudioSource() {
  // 子类根据设备返回相应的desc
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = 16000;
  desc.channels = 2;
  // 是否添加音频处理
  enableProcess = true;
}

void AudioSource::onFrame(const AvoxAFrame &frame) {
  if (!bFirstFrame) {
    initProcess = false;
    if (enableProcess) {
      audioProcess = std::unique_ptr<AudioProcess>(createWebRtcAudioProcess());
      if (audioProcess) {
        initProcess = audioProcess->initConfig(desc, desc);
        if (initProcess) {
          audioProcess->addObserver(this);
        }
      }
      LOGFLF(LogLevel::info, "initProcess:", initProcess,
             " desc sampleRate:", desc.sampleRate, " channels:", desc.channels,
             " format:", getAudioFormatStr(desc.format));
    }
    dispatch(&IAudioSourceOb::onAudioDesc, desc);
    bFirstFrame = true;
  }
  if (initProcess) {
    audioProcess->process(frame);
  } else {
    dispatch(&IAudioSourceOb::onAudioFrame, frame);
  }
}

void AudioSource::OnAudioProcess(const AvoxAFrame &frame) {
  dispatch(&IAudioSourceOb::onAudioFrame, frame);
}
```

音频源如果应用3A算法，则会截获音频源的音频数据，进行3A处理，然后再传递出去。渲染音频类似，在交给平台原生音频渲染器播放前，截获音频数据，进行3A处理，然后再传递出去，代码就不贴了。