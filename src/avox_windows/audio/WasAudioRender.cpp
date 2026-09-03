#include "WasAudioRender.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace avox {

// 软件音量缩放:按样本格式逐点乘 vol 后写入 dst,整数格式做饱和钳位
// 用于替代 WASAPI ISimpleAudioVolume:那个接口改的是音频会话音量,
// 会联动 Windows 音量混合器(系统级),软件缩放只改本进程输出波形
static void scalePcm(void* dst, const void* src, size_t size, AudioFormat fmt,
                     float vol) {
  switch (fmt) {
    case AudioFormat::AVOX_AUDIO_FLT: {
      const float* s = static_cast<const float*>(src);
      float* d = static_cast<float*>(dst);
      size_t n = size / sizeof(float);
      for (size_t i = 0; i < n; ++i) d[i] = s[i] * vol;
      break;
    }
    case AudioFormat::AVOX_AUDIO_S16: {
      const int16_t* s = static_cast<const int16_t*>(src);
      int16_t* d = static_cast<int16_t*>(dst);
      size_t n = size / sizeof(int16_t);
      for (size_t i = 0; i < n; ++i) {
        float v = static_cast<float>(s[i]) * vol;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        d[i] = static_cast<int16_t>(v);
      }
      break;
    }
    case AudioFormat::AVOX_AUDIO_S32: {
      const int32_t* s = static_cast<const int32_t*>(src);
      int32_t* d = static_cast<int32_t*>(dst);
      size_t n = size / sizeof(int32_t);
      for (size_t i = 0; i < n; ++i) {
        double v = static_cast<double>(s[i]) * vol;
        if (v > 2147483647.0) v = 2147483647.0;
        if (v < -2147483648.0) v = -2147483648.0;
        d[i] = static_cast<int32_t>(v);
      }
      break;
    }
    default:
      // 其他格式(DBL/S64/U8 等)WASAPI 共享模式不会出现,直接拷贝不缩放
      memcpy(dst, src, size);
      break;
  }
}

void regWasAudioRender() {
  RegFunc wasRenderReg = {
      "WASAPI audio render init", []() {
        ARenderDesc renderDesc = {};
        renderDesc.name = "WASAPI Audio Render";
        AvoxManager::Get().aRender.regInitFunc(
            ARenderType::wasapi, renderDesc,
            []() -> AudioRender* { return new WasAudioRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(wasRenderReg);
}

WasAudioRender::WasAudioRender() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  resample = std::make_unique<FFResample>();
#endif
}

WasAudioRender::~WasAudioRender() {
  close();
  CoUninitialize();
}

void WasAudioRender::onInit() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!desc.bValid()) {
    LOGFLF(LogLevel::warn, "desc is not valid");
    return;
  }
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&deviceEnumerator);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to create device enumerator:", hr);
    return;
  }
  hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                 device.GetAddressOf());
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get default audio endpoint:", hr);
    return;
  }
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        (void**)&audioClient);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to activate audio client:", hr);
    return;
  }
  // 获取系统默认的音频格式
  WAVEFORMATEX* format = nullptr;
  hr = audioClient->GetMixFormat(&format);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get default audio format:", hr);
    return;
  }

  DWORD flags = 0;
  // 请求更大的缓冲区，比如120ms,默认是22ms
  REFERENCE_TIME bufferDuration = 5 * frameMs * 10000;
  hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration,
                               0, format, nullptr);
  // 使用默认格式初始化音频客户端,否则会失败
  // 如果格式不同,需要重采样
  renderDesc.sampleRate = format->nSamplesPerSec;
  renderDesc.channels = format->nChannels;
  // 根据format->wFormatTag和wBitsPerSample决定renderDesc.format
  renderDesc.format = waveFormatToAudioFormat(format);
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  if (!resample->init(desc, renderDesc)) {
    LOGFLF(LogLevel::warn, "resample init failed");
  }
#endif
  // 释放默认格式
  CoTaskMemFree(format);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "failed to initialize audio client");
    return;
  }
  hr = audioClient->GetService(__uuidof(IAudioRenderClient),
                               (void**)&renderClient);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get render client:", hr);
    return;
  }
  // 音量走软件缩放(见 onRender),不获取 ISimpleAudioVolume:
  // 后者改的是 WASAPI 会话音量,会联动 Windows 音量混合器,影响系统级音量
  // 能放多少个采样点(总buffer大小=sampleCount*channel*preSampleSize)
  hr = audioClient->GetBufferSize(&sampleCount);
  // 缓冲区能存多少ms的数据
  int32_t bufferMs = sampleCount * 1000 / renderDesc.sampleRate;
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get buffer size:", hr);
    return;
  }
  hr = audioClient->Start();
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to start audio client:", hr);
    return;
  }
  // 初始化平滑值为缓冲区大小的一半，避免从0追赶导致时钟偏差
  lastQueueMs = bufferMs / 2;
  log(LogLevel::info, "WASAPI audio render init success, desc: ", desc,
      " renderDesc: ", renderDesc, " bufferSize:", sampleCount,
      " bufferMs:", bufferMs);
}

void WasAudioRender::onRender(const AvoxData& frame) {
  std::unique_lock<std::mutex> lock(mtx);
  if (!renderClient || frame.size == 0) {
    return;
  }
  AvoxData inData = frame;
#ifdef AVOX_ENABLE_FFMPEG
  // 如果格式不同,先重采样
  if (!resample->resample(inData)) {
    LOGFLF(LogLevel::warn, "resample failed");
    return;
  }
#endif
  // 已经填充但是没有播放的采样数
  UINT32 padding = 0;
  if (FAILED(audioClient->GetCurrentPadding(&padding))) {
    return;
  }
  // 缓冲区能填充的最大采样数
  int32_t maxSamples = sampleCount - padding;
  uint32_t sampleSize =
      renderDesc.channels * audioFormatSize(renderDesc.format);
  uint32_t inSamples = inData.size / sampleSize;
  if (inSamples > maxSamples) {
    // 输入的数据大于缓冲区时,这个是有问题的
    // LOGFLF(LogLevel::warn, "inSamples > maxSamples");
    inSamples = maxSamples;
  }
  BYTE* data = nullptr;
  HRESULT hr = renderClient->GetBuffer(inSamples, &data);
  if (SUCCEEDED(hr) && data) {
    // 软件音量:volume==1 直接拷贝(零开销),否则按格式缩放样本
    if (volume == 1.0f) {
      memcpy(data, inData.data, inData.size);
    } else {
      scalePcm(data, inData.data, inData.size, renderDesc.format, volume);
    }
    renderClient->ReleaseBuffer(inSamples, 0);
  }
}

bool WasAudioRender::empty() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioClient) {
    return true;
  }
  UINT32 padding = 0;
  // padding 是指当前音频缓冲区中已经填充但尚未播放的音频帧数
  if (FAILED(audioClient->GetCurrentPadding(&padding))) {
    return true;
  }
  int32_t paddingMs = padding * 1000 / renderDesc.sampleRate;
  // padding采样点与framesize不一样
  return paddingMs < frameMs;
}

int32_t WasAudioRender::getQueueMS() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioClient) {
    return lastQueueMs;
  }
  UINT32 padding = 0;
  if (FAILED(audioClient->GetCurrentPadding(&padding))) {
    return lastQueueMs;
  }
  int32_t paddingMs = padding * 1000 / renderDesc.sampleRate;
  return paddingMs;
}

bool WasAudioRender::full() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioClient) {
    return false;
  }
  UINT32 padding = 0;
  if (FAILED(audioClient->GetCurrentPadding(&padding))) {
    return lastQueueMs;
  }
  int32_t paddingMs = padding * 1000 / renderDesc.sampleRate;
  // 大约是超过80ms,就让它满了
  if (paddingMs >= frameMs * 2) {
    return true;
  }
  return false;
}

void WasAudioRender::pause(bool pause) {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioClient) {
    return;
  }
  if (pause) {
    audioClient->Stop();
  } else {
    audioClient->Start();
  }
}

void WasAudioRender::flush() {
  std::unique_lock<std::mutex> lock(mtx);
  // WASAPI 不支持直接清空缓冲区，需要停止并重新启动
  if (audioClient) {
    audioClient->Stop();
    audioClient->Start();
  }
  lastQueueMs = 0;  // 重置平滑值
}

void WasAudioRender::onClose() {
  std::unique_lock<std::mutex> lock(mtx);
  if (audioClient) {
    audioClient->Stop();
  }
  renderClient.Reset();
  audioClient.Reset();
  device.Reset();
  deviceEnumerator.Reset();
  // log(LogLevel::info, "WASAPI render close");
}

void WasAudioRender::speed(double speed) {
  // WASAPI 不直接支持变速，需要在应用层重采样处理
}

void WasAudioRender::setVolume(float cvolume) {
  std::unique_lock<std::mutex> lock(mtx);
  // 软件音量:只存值,onRender 里按样本缩放;不调 WASAPI 会话音量,
  // 以免联动 Windows 音量混合器导致系统级音量跟着变
  volume = cvolume;
  if (volume < 0.0f) {
    volume = 0.0f;
  } else if (volume > 1.0f) {
    volume = 1.0f;
  }
}

float WasAudioRender::getVolume() {
  std::unique_lock<std::mutex> lock(mtx);
  return volume;
}

AudioFormat waveFormatToAudioFormat(const WAVEFORMATEX* format) {
  if (!format) return AudioFormat::other;
  // 1. 处理扩展格式
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    PWAVEFORMATEXTENSIBLE pEx = (PWAVEFORMATEXTENSIBLE)format;
    // 浮点型 (Most common for WASAPI Shared Mode)
    if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
      if (format->wBitsPerSample == 32) return AudioFormat::AVOX_AUDIO_FLT;
      if (format->wBitsPerSample == 64) return AudioFormat::AVOX_AUDIO_DBL;
    }
    // 整型 PCM
    else if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_PCM, pEx->SubFormat)) {
      switch (format->wBitsPerSample) {
        case 8:
          return AudioFormat::AVOX_AUDIO_U8;
        case 16:
          return AudioFormat::AVOX_AUDIO_S16;
        case 32:
          return AudioFormat::AVOX_AUDIO_S32;
        case 64:
          return AudioFormat::AVOX_AUDIO_S64;
      }
    }
  }
  // 2. 处理标准 PCM 格式
  else if (format->wFormatTag == WAVE_FORMAT_PCM) {
    switch (format->wBitsPerSample) {
      case 8:
        return AudioFormat::AVOX_AUDIO_U8;
      case 16:
        return AudioFormat::AVOX_AUDIO_S16;
      case 32:
        return AudioFormat::AVOX_AUDIO_S32;
    }
  }
  // 3. 处理标准浮点格式
  else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    if (format->wBitsPerSample == 32) return AudioFormat::AVOX_AUDIO_FLT;
    if (format->wBitsPerSample == 64) return AudioFormat::AVOX_AUDIO_DBL;
  }
  return AudioFormat::other;
}

}
