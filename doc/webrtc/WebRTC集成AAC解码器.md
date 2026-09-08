# WebRTC 集成 AAC 解码器

> 注: faad2(GPL) 已从工程整体移除(闭源商业渠道合规), 文中 FaadDecoder 相关实现为历史方案存档。
> 现行 AAC 解码走 fdk-aac(Fraunhofer 许可, `src/avox_aac/FdkaacDecoder`)或 FFmpeg 原生解码器兜底,
> 下述 adts/asc 头初始化、WebRTC 强制 S16 输出等注意事项同样适用于 fdk-aac。

在上篇集成 [WebRTC 各平台的视频硬解 H256](WebRTC集成解码器.md) 后，因 WebRTC 自身没有集成对音频 AAC 的解码器，本文说明如何在 WebRTC 中集成音频 AAC 解码器，和上篇一样，不改动原 WebRTC 代码与结构，在本项目封装 WebRTC 的模块对 WebRTC 扩展。

因 WebRTC 自带 opus/g711 等解码器，其还集成音频数据检测，如燥音静音特殊处理等，如果像上篇集成视频解码器全改成本项目内实现，则会丢失这些功能，得益 WebRTC 自身良好的扩展性，本文是在原 WebRTC 的已经实现的解码器工厂上，把 AAC 解码器放入工厂中。

```C++
// 添加一个AudioDecoderAAC的结构，此结构仿AudioDecoderOpus
// 实现几个函数，一是提供SDP信息，二是生成解码器
scoped_refptr<AudioDecoderFactory> CreateAvoxAudioDecoderFactory() {
  return CreateAudioDecoderFactory<AudioDecoderOpus， AudioDecoderAAC，
                                   AudioDecoderG722， AudioDecoderG711>();
}

// SDP信息，用于生成本地SDP音频支持格式，与远程SDP协商
void AudioDecoderAAC::AppendSupportedDecoders(
    std::vector<webrtc::AudioCodecSpec> *specs) {
  // AudioCodecInfo aacinfo{48000， 1， 64000， 6000， 510000};
  specs->push_back({{"mpeg4-generic"， 8000， 1}， {8000， 1， 128000}});
  specs->push_back({{"mpeg4-generic"， 16000， 1}， {16000， 1， 128000}});
  specs->push_back({{"mpeg4-generic"， 16000， 2}， {16000， 2， 128000}});
  // WebRTC会检查采样率，44100这个采样率会报错
  // specs->push_back({{"mpeg4-generic"， 44100， 2}， {44100， 2， 128000}});
  specs->push_back({{"mpeg4-generic"， 48000， 2}， {48000， 2， 128000}});
}

std::unique_ptr<webrtc::AudioDecoder>
AudioDecoderAAC::MakeAudioDecoder(AudioDecoderAAC::Config config，
                                  std::optional<AudioCodecPairId> codec_pair_id，
                                  const FieldTrialsView *field_trials) {
  AudioDesc adesc = {};
  adesc.channels = config.num_channels;
  adesc.sampleRate = config.sample_rate_hz;
  adesc.format = AudioFormat::AVOX_AUDIO_S16;
  return std::make_unique<RtcAudioDecoder>(adesc);
}
```

如上，提供一个工厂，如果双方 SDP 里音频信息都用 aac(mpeg4-generic)，则会生成相应的 AAC 解码器 RtcAudioDecoder。

同上篇集成视频解码器一样，项目中有已有用 FAAD 实现的 AAC 音频解码器，其 RtcAudioDecoder 也是对 FaadDecoder 的接口转发，再看 FaadDecoder 实现之前，先说几点需要事项。

- AAC 有二种常见头封装格式，一种是 adts，一种是 asc，其中 zlmediakit/ffmpeg(直播流)解析 aac 直播流，其每个包都带 adts 头，而 webrtc/ffmpeg(本地)解析的包不带 adts 头信息，经测试，如果数据带 adts 头信息，则初始化也要使用 adts 头信息初始化，如果不带 adts 头信息，则需要用 asc 头信息初始化，否则初始化与包对不上，解码时会很容易一直报错。当然也可统一用 adts，先用 adts 初始化，再把 webrtc/ffmpeg 的包手动添加上 adts 头，但是这样就会多消耗 CPU 没必要。
- faad 解码器，不做默认设置，会把 16000 转 32000，单通道转双通道，我用播放器自身渲染时，只要让音频播放根据出的格式就行，但是 WebRTC 会自动调音频设备播放，这个转化后数据格式就会不匹配，其升通道有设置可以避免，而升通道则不能，需要自身处理下。
- 音频采样点格式问题，其 float/s16 比较常见，但是 WebRTC 里默认是 s16，因为在解码时，如果是 WebRTC 调用的，需要强制输出格式为 S16。

```C++
void regFaadDecoder() {
  RegFunc faadReg = {"faad init", []() {
                       ACodecDesc faadDesc = {};
                       faadDesc.name = "faad decoder";
                       faadDesc.bHardware = false;
                       AvoxManager::Get().aDecoders.regInitFunc(
                           ACodecId::aac, faadDesc, []() -> AudioDecoder * {
                             return new FaadDecoder();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(faadReg);
}

FaadDecoder::FaadDecoder() { handle = NeAACDecOpen(); }

FaadDecoder::~FaadDecoder() { close(); }

bool FaadDecoder::onVaild() { return true; }

DecodeResult FaadDecoder::onPreDecoder() {
  // ffmpeg需要配置帧填充extradata
  if (!confPkt) {
    return DecodeResult::noConfig;
  }
  // 如果配置使用adts头，则解析数据也需要带adts头，zlmediakt直播流
  // 如果配置文件使用asc头，则解析数据不带头，本地流,webrtc,ffmpeg解析流
  // adts头与ASC(Audio Specific Config)头
  if (bAdtsHeader(confPkt->buff.data(), confPkt->buff.size())) {
    bAdts = true;
    uint8_t *cfg = confPkt->buff.data();
    // 1. 解析 objectType (Profile)
    objectType = ((cfg[2] >> 6) & 0x03) + 1;
    // 2. 解析 sampleRate (采样率索引)
    confSampleIndex = (cfg[2] >> 2) & 0x0F;
    // 3. 解析 channel (声道配置)
    scrChannels = ((cfg[2] & 0x01) << 2) | ((cfg[3] >> 6) & 0x03);
  } else {
    bAdts = false;
    uint8_t *cfg = confPkt->buff.data();
    objectType = cfg[0] >> 3;
    confSampleIndex = ((cfg[0] & 0x07) << 1) | (cfg[1] >> 7);
    scrChannels = (cfg[1] & 0x7F) >> 3;
  }
  std::string adtsStr = bAdts ? "adts" : "asc";
  log(LogLevel::info, "faad init ", adtsStr, " objectType:", objectType,
      " sampleRateIndex:", confSampleIndex, " channel:", confChannel);
  unsigned long sampleRate = 0;
  uint8_t channels = 0;
  config = NeAACDecGetCurrentConfiguration(handle);
  // 检查是否WebRTC调用了当前解码器
  bWebRtc = option.value("bWebRtc", false);
  // 原始通道数
  scrChannels = outDesc.channels;
  if (bWebRtc) {
    // 强制S16输出
    config->outputFormat = FAAD_FMT_16BIT;
  }
  // 不要自动升采样(16000升32000)
  config->dontUpSampleImplicitSBR = 1;
  config->defObjectType = LC;
  config->downMatrix = 0;
  config->useOldADTSFormat = 0;
  NeAACDecSetConfiguration(handle, config);
  // NeAACDecInit2初始化有个二字节的音频信息，逻辑应该是如下
  // media-server\libflv\source\mpeg4-aac.c mpeg4_aac_audio_specific_config_load
  int32_t ret = 0;
  if (bAdts) {
    ret = NeAACDecInit(handle, confPkt->buff.data(), confPkt->buff.size(),
                       &sampleRate, &channels);
  } else {
    ret = NeAACDecInit2(handle, confPkt->buff.data(), confPkt->buff.size(),
                        &sampleRate, &channels);
  }
  outDesc.sampleRate = sampleRate;
  // faad会固定把单通道输出双通道
  outDesc.channels = channels;
  AvoxData confData = {confPkt->buff.data(), confPkt->buff.size(), true};
  log(LogLevel::info, "faad init out desc config:", confData,
      " sampleRate:", outDesc.sampleRate, " channels:", outDesc.channels,
      " format:", getAudioFormatStr(outDesc.format),
      " object type:", (int32_t)config->defObjectType);
  if (bWebRtc) {
    // 因为webrtc有自己的渲染，所以通道数需要还原
    outDesc.channels = scrChannels;
  }
  outDesc.format = faadAudioFromat(config->outputFormat);
  // ret不为0表示初始化失败
  if (ret != 0) {
    LOGFLF(LogLevel::warn, "faad init failed");
    return DecodeResult::openFailed;
  }
  return DecodeResult::success;
}

// 解码线程解码
DecodeResult FaadDecoder::decode(const AvoxPacket &packet) {
  if (!confPkt) {
    return DecodeResult::noConfig;
  }
  if (!config) {
    return onPreDecoder();
  }
  uint8_t *packetPtr = packet.data.data;
  int32_t dataSize = packet.data.size;
  if (bAdts) {
    // 如果用adts初始化，但是数据没adts头，自动添加adts头
    if (!bAdtsHeader(packetPtr, dataSize)) {
      const int32_t adtsHeaderSize = 7;
      if (aacData.size() < dataSize + adtsHeaderSize) {
        // 减少多次分配机率
        aacData.resize(dataSize * 2 + adtsHeaderSize);
      }
      // 添加adts头
      adtsHeader(aacData.data(), dataSize, confSampleIndex, confChannel,
                 objectType);
      // 复制数据
      memcpy(aacData.data() + adtsHeaderSize, packet.data.data, dataSize);
      packetPtr = aacData.data();
      dataSize = dataSize + adtsHeaderSize;
    }
  }
  NeAACDecFrameInfo info = {};
  uint8_t *outData =
      (uint8_t *)NeAACDecDecode(handle, &info, packetPtr, dataSize);
  if (info.error > 0) {
    LOGFLF(LogLevel::warn,
           "faad decode error:", NeAACDecGetErrorMessage(info.error));
    return DecodeResult::dataError;
  }
  // 如果 SBR为1，samples比正常要多二
  if (info.sbr == 1) {
    // 采样点会翻倍
  }
  AvoxAFrame aframe = {};
  aframe.pts = packet.pts;
  aframe.buffer.data = outData;
  // samples包含通道数，经测试双通道固定2048，如果是SBR，samples会翻倍
  aframe.buffer.size = info.samples * audioFormatSize(outDesc.format);
  aframe.buffer.bRef = true;
  // webrtc如果原始是单通道，这里需要把解码后的双道数据取单通道数据
  if (bWebRtc && scrChannels == 1) {
    int16_t *ori = (int16_t *)aframe.buffer.data;
    int16_t *dest = (int16_t *)aframe.buffer.data;
    // LRLRLR 改为LLLLLLL
    for (int i = 0, j = 0; i < info.samples; i += 2, j += 1) {
      dest[j] = ori[i];
      // dest[j + 1024] = ori[i + 1];
    }
    // 双声道只取单声道，这里需要除以2
    // webrtc如果以uint16_t计算长度，后面需要再除以2
    aframe.buffer.size = aframe.buffer.size / 2;
  }
  // 这里的data还在，需要在onDecode用掉或是保存
  dispatch(&IAudioDecoderOb::onDecode, aframe);
  // log(LogLevel::info, "audio pts:", aframe.pts);
  return DecodeResult::success;
}

void FaadDecoder::flush() {}

void FaadDecoder::onClose() {
  if (handle) {
    NeAACDecClose(handle);
    handle = nullptr;
  }
}
```

而 RtcAudioDecoder 则是对 FaadDecoder 的实现在 WebRTC 里的 AudioDecoder 接口转发，相比视频解码器的转发实现，需要注意几个点。

- 上面说过，如果 RTP 包里带 adts 头，则用 adts 初始化，否则用 asc 头初始化，因为 WebRTC 里 RTP 包的 AAC 数据是不带 adts 头的，所以需要生成 asc 头，再初始化 faad。
- WebRTC 需要解码器解析出 RTP 里面 AAC 数据，需要注意的是，一帧 AAC 数据可能会被拆分成多个 RTP 包，所以在解码 RTP 包时，要把同一帧 AAC 数据的 RTP 包合并后再丢给解码器。

```C++
#define AVOX_TEST_AUDIO_RECORDER 0

class RtcAudioDecoder : public webrtc::AudioDecoder，
                        public avox::IAudioDecoderOb {
public:
  RtcAudioDecoder(const AudioDesc &adesc);
  virtual ~RtcAudioDecoder();

protected:
  AudioDesc adesc = {};
  AvoxAFrame frame = {};
  std::unique_ptr<avox::AudioDecoder> decode = nullptr;
  std::vector<uint8_t> aacData;
  std::vector<uint8_t> rtpData;
  uint32_t preRtpTime = 0;

#if AVOX_TEST_AUDIO_RECORDER
  // 用来测试记录PCM数据
  bool bRecordPcm = false;
  std::ofstream fileStream;
  std::string filePath = "D:\\test.pcm";
#endif

protected:
  void findDecoder();

public:
  virtual void Reset() override;
  virtual int SampleRateHz() const override;
  virtual size_t Channels() const override;
  virtual int PacketDuration(const uint8_t *encoded，
                             size_t encoded_len) const override;
  virtual std::vector<ParseResult> ParsePayload(webrtc::Buffer &&payload，
                                                uint32_t timestamp) override;

protected:
  virtual int DecodeInternal(const uint8_t *encoded， size_t encoded_len，
                             int sample_rate_hz， int16_t *decoded，
                             SpeechType *speech_type) override;

  // IAudioDecoderOb
public:
  virtual void onDecode(const AvoxAFrame &frame) override;
};

using namespace webrtc;

RtcAudioDecoder::RtcAudioDecoder(const AudioDesc &adesc_) {
  adesc = adesc_;
  findDecoder();
}

void RtcAudioDecoder::findDecoder() {
// 应该只有aac才会进来这里
  bool bFind = AvoxManager::Get().aDecoders.hasObjectId(ACodecId::aac);
  if (!bFind) {
    log(LogLevel::info， "not find aac decoder");
    return;
  }
  const auto &decodes = AvoxManager::Get().aDecoders.initFuncs(ACodecId::aac);
  if (decodes.size() < 0) {
    return;
  }
  // 现在就用faad来初始化解码器
  auto &aDecode = decodes[0];
  decode = std::unique_ptr<avox::AudioDecoder>(aDecode.initFunc());
  if (!decode) {
    return;
  }
  decode->addObserver(this);
  decode->setBool("bWebRtc"， true);
  bool bInit = decode->setContext(aDecode.desc， adesc);
  if (!bInit) {
    log(LogLevel::info， "aac decoder not support");
    return;
  }
  AvoxPacket pkt = {};
  std::vector<uint8_t> asc(2);
  ascHeader(adesc， asc.data());
  pkt.data = {asc.data()， static_cast<int>(asc.size())， true};
  pkt.packtype = (int32_t)PackType::aconfig;
  pkt.prefixSize = 2;
  decode->pushConfig(pkt);
#if AVOX_TEST_AUDIO_RECORDER
  bRecordPcm = true;
  if (bRecordPcm) {
    fileStream.open(filePath， std::ios::binary | std::ios::out);
  }
#endif
}

RtcAudioDecoder::~RtcAudioDecoder() {
  if (decode) {
    decode->close();
    decode->removeObserver(this);
    decode.reset();
  }
#if AVOX_TEST_AUDIO_RECORDER
  if (fileStream.is_open()) {
    fileStream.close();
  }
#endif
}

void RtcAudioDecoder::Reset() { findDecoder(); }

int RtcAudioDecoder::SampleRateHz() const {
  // log(LogLevel::info， "sampleRateHz:"， adesc.sampleRate);
  return adesc.sampleRate;
}

size_t RtcAudioDecoder::Channels() const {
  // log(LogLevel::info， "channels:"， adesc.channels);
  return adesc.channels;
}

int RtcAudioDecoder::PacketDuration(const uint8_t *encoded，
                                    size_t encoded_len) const {
  // 返回的是单通道后解码采样时长*每毫秒采样数，单位时间内的采样数
  return 1024;
}

int RtcAudioDecoder::DecodeInternal(const uint8_t *encoded， size_t encoded_len，
                                    int sample_rate_hz， int16_t *decoded，
                                    SpeechType *speech_type) {
  if (!decode) {
    return -1;
  }
  AvoxPacket pkt = {};
  pkt.data = {const_cast<uint8_t *>(encoded)， static_cast<int>(encoded_len)，
              true};
  DecodeResult result = decode->decode(pkt);
  if (result != DecodeResult::success) {
    return 0;
  }
  *speech_type = AudioDecoder::SpeechType::kSpeech;
  // onDecod与这是同一线程，所以这里直接使用frame的buffer，肯定还在
  // decoded = (int16_t *)frame.buffer.data;
  memcpy(decoded， frame.buffer.data， frame.buffer.size);
  // log(LogLevel::info， "aac data buffer size:"， frame.buffer.size / 2);
#if AVOX_TEST_AUDIO_RECORDER
  if (fileStream.is_open()) {
    fileStream.write((char *)decoded， frame.buffer.size);
  }
#endif
  // webrtc以uint16_t计算长度，后面需要再除以2
  return frame.buffer.size / 2;
}

void RtcAudioDecoder::onDecode(const AvoxAFrame &frame_) { frame = frame_; }

// 这里现测试有点奇怪，payload不足一个包，需要多个palyload才能合成一个
std::vector<webrtc::AudioDecoder::ParseResult>
RtcAudioDecoder::ParsePayload(webrtc::Buffer &&payload， uint32_t timestamp) {
  std::vector<AudioDecoder::ParseResult> results;
  // uint32_t last_dts = 0;
  // rtp数据开始部分
  auto ptr = payload.data();
  size_t payloadSize = payload.size();
  // rtp数据末尾
  auto end = ptr + payloadSize;
  // AU-headers用多少字节表示长度，一般是16bit二个字节
  auto au_header_size = ((ptr[0] << 8) | ptr[1]) / 8;
  // 记录au_header起始指针
  auto au_header_ptr = ptr + au_header_size;
  // 从au_header读取长度
  uint16_t data_size = ((au_header_ptr[0] << 8) | au_header_ptr[1]) >> 3;
  int32_t payDatasize = payloadSize - au_header_size - 2;
  // AAC数据起始位置
  ptr = au_header_ptr + 2;
  if (end < ptr) {
    // 数据不够
    return results;
  }
  // 如果包没拆，其data_size = payDatasize
  if (data_size == payDatasize) {
    // 设置aac数据
    webrtc::Buffer new_payload((uint8_t *)ptr， data_size);
    std::unique_ptr<LegacyEncodedAudioFrame> frame(
        new LegacyEncodedAudioFrame(this， std::move(new_payload)));
    results.emplace_back(timestamp， 0， std::move(frame));
  } else if (data_size > payDatasize) {
    // 添加到包
    rtpData.insert(rtpData.end()， ptr， ptr + payDatasize);
    if (rtpData.size() >= data_size) {
      // 正常应该相等
      if (rtpData.size() > data_size) {
        log(LogLevel::warn， "rtp data size:"， rtpData.size()，
            " aac data size:"， data_size， " timestamp:"， timestamp，
            " preRtpTime:"， preRtpTime， " au_header_size:"， au_header_size);
      }
      webrtc::Buffer new_payload((uint8_t *)rtpData.data()， rtpData.size());
      std::unique_ptr<LegacyEncodedAudioFrame> frame(
          new LegacyEncodedAudioFrame(this， std::move(new_payload)));
      results.emplace_back(timestamp， 0， std::move(frame));
      rtpData.clear();
    }
  }
  if (!results.empty()) {
    // log(LogLevel::warn， "pay data size:"， payDatasize，
    //     " aac data size:"， data_size);
  }
  preRtpTime = timestamp;
  return results;
}
```

经测试，16000 单通道与双通道都能正常解码并播放。
