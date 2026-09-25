#include "WasAudioRender.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace avox {

namespace {

// 默认设备变更通知器:回调来自系统线程,这里只置共享标志,绝不碰渲染对象/COM 链
class WasDeviceNotifier : public IMMNotificationClient {
 public:
  explicit WasDeviceNotifier(std::shared_ptr<std::atomic<bool>> pending)
      : pendingFlag(std::move(pending)) {}

  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&refCount);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG remaining = InterlockedDecrement(&refCount);
    if (remaining == 0) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** ppvObject) override {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IMMNotificationClient) ||
        riid == __uuidof(IUnknown)) {
      *ppvObject = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,
                                                 DWORD) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
      LPCWSTR, const PROPERTYKEY) override {
    return S_OK;
  }
  // 流按 eConsole 角色开,只跟这个角色的默认切换(系统每次会发 eConsole+eMultimedia 两枪)
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                   LPCWSTR deviceId) override {
    char narrow[128] = {};
    if (deviceId) {
      WideCharToMultiByte(CP_UTF8, 0, deviceId, -1, narrow, sizeof(narrow),
                          nullptr, nullptr);
    }
    LOGFLF(LogLevel::info, "device notify flow=", (int)flow, " role=", (int)role,
           " id=", (const char*)narrow);
    if (flow == eRender && role == eConsole && pendingFlag) {
      pendingFlag->store(true);
    }
    return S_OK;
  }

 private:
  LONG refCount = 1;
  std::shared_ptr<std::atomic<bool>> pendingFlag;
};

}  // namespace

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
  deviceSwitchPending = std::make_shared<std::atomic<bool>>(false);
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
  initLocked();
}

bool WasAudioRender::initLocked() {
  if (!desc.bValid()) {
    LOGFLF(LogLevel::warn, "desc is not valid");
    return false;
  }
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&deviceEnumerator);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to create device enumerator:", hr);
    return false;
  }
  // 注册默认设备切换监听;注册失败只降级为「不跟切」,不挡播放
  if (!deviceNotifier) {
    deviceNotifier = new WasDeviceNotifier(deviceSwitchPending);
  }
  hr = deviceEnumerator->RegisterEndpointNotificationCallback(
      deviceNotifier.Get());
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to register device notification:", hr);
  }
  hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                 device.GetAddressOf());
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get default audio endpoint:", hr);
    return false;
  }
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        (void**)&audioClient);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to activate audio client:", hr);
    return false;
  }
  // 获取系统默认的音频格式
  WAVEFORMATEX* format = nullptr;
  hr = audioClient->GetMixFormat(&format);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get default audio format:", hr);
    return false;
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
  // 重采样(新设备混音格式可能不同,重建时必须重跑)
  if (!resample->init(desc, renderDesc)) {
    LOGFLF(LogLevel::warn, "resample init failed");
  }
#endif
  // 释放默认格式
  CoTaskMemFree(format);
  if (FAILED(hr)) {
    AVOX_WIN_LOG(hr, "failed to initialize audio client");
    return false;
  }
  hr = audioClient->GetService(__uuidof(IAudioRenderClient),
                               (void**)&renderClient);
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get render client:", hr);
    return false;
  }
  // 音量走软件缩放(见 onRender),不获取 ISimpleAudioVolume:
  // 后者改的是 WASAPI 会话音量,会联动 Windows 音量混合器,影响系统级音量
  // 能放多少个采样点(总buffer大小=sampleCount*channel*preSampleSize)
  hr = audioClient->GetBufferSize(&sampleCount);
  // 缓冲区能存多少ms的数据
  int32_t bufferMs = sampleCount * 1000 / renderDesc.sampleRate;
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to get buffer size:", hr);
    return false;
  }
  hr = audioClient->Start();
  if (FAILED(hr)) {
    LOGFLF(LogLevel::warn, "Failed to start audio client:", hr);
    return false;
  }
  // 初始化平滑值为缓冲区大小的一半，避免从0追赶导致时钟偏差
  lastQueueMs = bufferMs / 2;
  log(LogLevel::info, "WASAPI audio render init success, desc: ", desc,
      " renderDesc: ", renderDesc, " bufferSize:", sampleCount,
      " bufferMs:", bufferMs);
  return true;
}

void WasAudioRender::onRender(const AvoxData& frame) {
  std::unique_lock<std::mutex> lock(mtx);
  // 默认设备已切换/失效:整链重抓(新混音格式重跑重采样),音量/desc 成员天然保留
  reinitIfPendingLocked();
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
    // 客户端已坏(设备拔出等):标记重建,当前帧丢弃
    scheduleDeviceReinitLocked();
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
  if (hr == AUDCLNT_E_DEVICE_INVALIDATED ||
      hr == AUDCLNT_E_RESOURCES_INVALIDATED) {
    scheduleDeviceReinitLocked();
  }
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

void WasAudioRender::scheduleDeviceReinitLocked() {
  deviceSwitchPending->store(true);
  retryAtMs = GetTickCount64();
}

void WasAudioRender::reinitIfPendingLocked() {
  if (!deviceSwitchPending->load() || GetTickCount64() < retryAtMs) {
    return;
  }
  LOGFLF(LogLevel::info, "audio device switch: rebuilding WASAPI render");
  deviceSwitchPending->store(false);
  closeLocked();
  if (!initLocked()) {
    // 设备暂不可用(全拔等):退避后由后续 onRender 重试
    deviceSwitchPending->store(true);
    retryAtMs = GetTickCount64() + 500;
    return;
  }
  if (pausedState && audioClient) {
    // 重建发生在暂停期:保持暂停态,别提前出声
    audioClient->Stop();
  }
}

bool WasAudioRender::empty() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioClient) {
    return true;
  }
  UINT32 padding = 0;
  if (FAILED(audioClient->GetCurrentPadding(&padding))) {
    // 时钟线程也会进到这里:客户端失效先标记,重建交给 onRender
    scheduleDeviceReinitLocked();
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
    scheduleDeviceReinitLocked();
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
    scheduleDeviceReinitLocked();
    return lastQueueMs;
  }
  int32_t paddingMs = padding * 1000 / renderDesc.sampleRate;
  // 顶到设备缓冲的一半(至少2块)。原来只顶到 frameMs*2(80ms), 而渲染线程唤醒
  // 抖动实测可达64ms、每次只喂40ms, 余量不足1块 —— 实测8秒内 padding 归零一次,
  // 设备缓冲被抽干就是爆音/杂音的直接来源。
  // 共享模式下设备缓冲通常200ms, 顶到100ms只多约20ms延迟。
  int32_t deviceMs = sampleCount * 1000 / renderDesc.sampleRate;
  int32_t targetMs = std::max(frameMs * 2, deviceMs / 2);
  if (paddingMs >= targetMs) {
    return true;
  }
  return false;
}

void WasAudioRender::pause(bool pause) {
  std::unique_lock<std::mutex> lock(mtx);
  pausedState = pause;
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
  closeLocked();
}

void WasAudioRender::closeLocked() {
  // 先注销监听再放 COM 引用;注销返回后系统保证不再回调,通知器可安全释放
  if (deviceEnumerator && deviceNotifier) {
    deviceEnumerator->UnregisterEndpointNotificationCallback(
        deviceNotifier.Get());
  }
  if (audioClient) {
    audioClient->Stop();
  }
  renderClient.Reset();
  audioClient.Reset();
  device.Reset();
  deviceNotifier.Reset();
  deviceEnumerator.Reset();
  sampleCount = 0;
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
