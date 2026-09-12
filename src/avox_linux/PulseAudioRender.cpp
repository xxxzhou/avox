#include "PulseAudioRender.hpp"

#ifdef __ONLY_LINUX__

#ifdef AVOX_ENABLE_PULSE

#include "avox/module/AvoxManager.hpp"

namespace avox {

// 软件音量缩放(与 WasAudioRender 同策略): 逐点乘 volume, 整数饱和钳位
static void scalePcm(void* dst, const void* src, size_t size,
                     AudioFormat fmt, float vol) {
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
      // pulse spec 之外的格式不会出现(见 toPaFormat), 直接拷贝
      memcpy(dst, src, size);
      break;
  }
}

// AudioFormat -> pulse packed 采样格式; planar/其他不支持, 由重采样兜底转FLT
static pa_sample_format_t toPaFormat(AudioFormat fmt) {
  switch (fmt) {
    case AudioFormat::AVOX_AUDIO_U8:
      return PA_SAMPLE_U8;
    case AudioFormat::AVOX_AUDIO_S16:
      return PA_SAMPLE_S16LE;
    case AudioFormat::AVOX_AUDIO_S32:
      return PA_SAMPLE_S32LE;
    case AudioFormat::AVOX_AUDIO_FLT:
      return PA_SAMPLE_FLOAT32LE;
    default:
      return PA_SAMPLE_INVALID;
  }
}

void regPulseAudioRender() {
  RegFunc pulseRenderReg = {
      "PulseAudio audio render init", []() {
        ARenderDesc renderDesc = {};
        renderDesc.name = "PulseAudio Audio Render";
        AvoxManager::Get().aRender.regInitFunc(
            ARenderType::pulse, renderDesc,
            []() -> AudioRender* { return new PulseAudioRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(pulseRenderReg);
}

void PulseAudioRender::contextStateCb(pa_context* c, void* userdata) {
  auto* self = static_cast<PulseAudioRender*>(userdata);
  pa_threaded_mainloop_signal(self->mainloop, 0);
}

void PulseAudioRender::streamStateCb(pa_stream* s, void* userdata) {
  auto* self = static_cast<PulseAudioRender*>(userdata);
  pa_threaded_mainloop_signal(self->mainloop, 0);
}

void PulseAudioRender::timingUpdateCb(pa_stream* s, int success,
                                      void* userdata) {
  auto* self = static_cast<PulseAudioRender*>(userdata);
  if (success && s == self->stream) {
    const pa_timing_info* info = pa_stream_get_timing_info(s);
    if (info && info->read_index > 0) {
      self->playBytes = info->read_index;
    }
  }
  pa_threaded_mainloop_signal(self->mainloop, 0);
}

// flush/cork 等 success 回调, 仅 signal 唤醒 wait 方
void PulseAudioRender::streamSuccessCb(pa_stream* s, int success,
                                       void* userdata) {
  auto* self = static_cast<PulseAudioRender*>(userdata);
  pa_threaded_mainloop_signal(self->mainloop, 0);
}

PulseAudioRender::PulseAudioRender() {
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  resample = std::make_unique<FFResample>();
#endif
  mainloop = pa_threaded_mainloop_new();
  if (!mainloop) {
    LOGFLF(LogLevel::warn, "pa_threaded_mainloop_new failed");
    return;
  }
  context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "avox");
  if (!context) {
    LOGFLF(LogLevel::warn, "pa_context_new failed");
    return;
  }
  pa_context_set_state_callback(context, contextStateCb, this);
  if (pa_threaded_mainloop_start(mainloop) < 0) {
    LOGFLF(LogLevel::warn, "pa_threaded_mainloop_start failed");
    return;
  }
}

PulseAudioRender::~PulseAudioRender() {
  close();
  if (context) {
    pa_threaded_mainloop_lock(mainloop);
    pa_context_disconnect(context);
    pa_context_unref(context);
    context = nullptr;
    pa_threaded_mainloop_unlock(mainloop);
  }
  if (mainloop) {
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
    mainloop = nullptr;
  }
}

void PulseAudioRender::onInit() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!desc.bValid()) {
    LOGFLF(LogLevel::warn, "desc is not valid");
    return;
  }
  if (!mainloop || !context) {
    LOGFLF(LogLevel::warn, "pulse mainloop/context not ready");
    return;
  }
  // 分辨率/格式可能变化, 先回收旧流
  if (stream) {
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = nullptr;
  }
  // 连接默认 server(NULL = PULSE_SERVER env / X11 发现, WSLg 兼容)
  pa_threaded_mainloop_lock(mainloop);
  pa_context_set_state_callback(context, contextStateCb, this);
  if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
    LOGFLF(LogLevel::warn, "pa_context_connect failed:",
           pa_strerror(pa_context_errno(context)));
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  pa_context_state_t cstate = pa_context_get_state(context);
  while (cstate != PA_CONTEXT_READY && cstate != PA_CONTEXT_FAILED &&
         cstate != PA_CONTEXT_TERMINATED) {
    pa_threaded_mainloop_wait(mainloop);
    cstate = pa_context_get_state(context);
  }
  if (cstate != PA_CONTEXT_READY) {
    LOGFLF(LogLevel::warn, "pulse context not ready");
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  // 设备格式: 输入是 pulse 支持的 packed 格式则直用, 否则转 FLT
  renderDesc = desc;
  if (toPaFormat(renderDesc.format) == PA_SAMPLE_INVALID) {
    renderDesc.format = AudioFormat::AVOX_AUDIO_FLT;
  }
  spec.format = toPaFormat(renderDesc.format);
  spec.rate = static_cast<uint32_t>(renderDesc.sampleRate);
  spec.channels = renderDesc.channels;
  if (!pa_sample_spec_valid(&spec)) {
    LOGFLF(LogLevel::warn, "sample spec invalid");
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
#ifdef AVOX_ENABLE_FFMPEG
  // 输入desc与设备格式不同(含 planar->packed)时重采样
  if (renderDesc != desc && !resample->init(desc, renderDesc)) {
    LOGFLF(LogLevel::warn, "resample init failed");
  }
#endif
  stream = pa_stream_new(context, "avox playback", &spec, NULL);
  if (!stream) {
    LOGFLF(LogLevel::warn, "pa_stream_new failed");
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  pa_stream_set_state_callback(stream, streamStateCb, this);
  // 缓冲目标 4*frameMs, 交给 pulse 按时延自动调整
  size_t bytesPerSec = pa_bytes_per_second(&spec);
  pa_buffer_attr attr = {};
  attr.maxlength = static_cast<uint32_t>(-1);
  attr.tlength = static_cast<uint32_t>(bytesPerSec * frameMs / 1000) * 4;
  attr.prebuf = static_cast<uint32_t>(-1);
  attr.minreq = static_cast<uint32_t>(-1);
  pa_stream_flags_t flags = PA_STREAM_ADJUST_LATENCY;
  pa_stream_connect_playback(stream, NULL, &attr, flags, NULL, NULL);
  pa_stream_state_t sstate = pa_stream_get_state(stream);
  while (sstate != PA_STREAM_READY && sstate != PA_STREAM_FAILED &&
         sstate != PA_STREAM_TERMINATED) {
    pa_threaded_mainloop_wait(mainloop);
    sstate = pa_stream_get_state(stream);
  }
  if (sstate != PA_STREAM_READY) {
    LOGFLF(LogLevel::warn, "pulse stream not ready:",
           pa_strerror(pa_context_errno(context)));
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  // 初始化平滑值为缓冲区的一半，避免从0追赶导致时钟偏差(对齐 WASAPI)
  int32_t bufferMs = attr.tlength * 1000 / bytesPerSec;
  lastQueueMs = bufferMs / 2;
  playBytes = 0;
  writeBytes = 0;
  pa_threaded_mainloop_unlock(mainloop);
  log(LogLevel::info, "PulseAudio render init success, desc: ", desc,
      " renderDesc: ", renderDesc, " bufferMs:", bufferMs);
}

void PulseAudioRender::onRender(const AvoxData& frame) {
  std::unique_lock<std::mutex> lock(mtx);
  if (!stream || frame.size == 0) {
    return;
  }
  AvoxData inData = frame;
#ifdef AVOX_ENABLE_FFMPEG
  // 如果格式不同,先重采样
  if (renderDesc != desc) {
    if (resample->resample(inData) <= 0 || inData.size == 0) {
      LOGFLF(LogLevel::warn, "resample failed");
      return;
    }
  }
#endif
  pa_threaded_mainloop_lock(mainloop);
  if (!stream || pa_stream_get_state(stream) != PA_STREAM_READY) {
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  // 可写空间不足时按采样帧对齐截断(对齐 WASAPI 的 maxSamples 策略)
  size_t writable = pa_stream_writable_size(stream);
  size_t sampleFrame = renderDesc.channels *
                       audioFormatSize(renderDesc.format);
  if (sampleFrame == 0) {
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  size_t toWrite = inData.size;
  if (writable < toWrite) {
    toWrite = writable / sampleFrame * sampleFrame;
  }
  if (toWrite == 0) {
    pa_threaded_mainloop_unlock(mainloop);
    return;
  }
  const void* data = inData.data;
  std::vector<uint8_t> volBuffer;
  if (volume != 1.0f) {
    // 软件音量: 只缩放即将写入的部分
    volBuffer.resize(toWrite);
    scalePcm(volBuffer.data(), inData.data, toWrite, renderDesc.format, volume);
    data = volBuffer.data();
  }
  if (pa_stream_write(stream, data, toWrite, nullptr, 0, PA_SEEK_RELATIVE) <
      0) {
    LOGFLF(LogLevel::warn, "pa_stream_write failed:",
           pa_strerror(pa_context_errno(context)));
  } else {
    writeBytes += toWrite;
  }
  // 异步刷新时序信息, 回调里更新 playBytes(供 getQueueMS/empty/full 使用)
  pa_stream_update_timing_info(stream, timingUpdateCb, this);
  pa_threaded_mainloop_unlock(mainloop);
}

bool PulseAudioRender::empty() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!stream) {
    return true;
  }
  // 队列水位 < 一帧视为空(对齐 WASAPI)
  return queueMSLocked() < frameMs;
}

// 计算当前队列水位ms, 调用方必须已持有 mtx
int32_t PulseAudioRender::queueMSLocked() {
  if (!stream) {
    return lastQueueMs;
  }
  int64_t queueBytes = writeBytes.load() - playBytes.load();
  if (queueBytes < 0) {
    queueBytes = 0;
  }
  size_t bytesPerSec = pa_bytes_per_second(&spec);
  if (bytesPerSec == 0) {
    return lastQueueMs;
  }
  lastQueueMs = static_cast<int32_t>(queueBytes * 1000 / bytesPerSec);
  return lastQueueMs;
}

int32_t PulseAudioRender::getQueueMS() {
  std::unique_lock<std::mutex> lock(mtx);
  return queueMSLocked();
}

bool PulseAudioRender::full() {
  // 大约超过 2*frameMs 视为满(对齐 WASAPI)
  return getQueueMS() >= frameMs * 2;
}

void PulseAudioRender::pause(bool pause) {
  std::unique_lock<std::mutex> lock(mtx);
  if (!stream || !mainloop) {
    return;
  }
  pa_threaded_mainloop_lock(mainloop);
  pa_stream_cork(stream, pause ? 1 : 0, nullptr, nullptr);
  pa_threaded_mainloop_unlock(mainloop);
}

void PulseAudioRender::flush() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!stream || !mainloop) {
    return;
  }
  pa_threaded_mainloop_lock(mainloop);
  // 清空设备侧缓冲; success 回调 signal, 等待完成后写计数对齐已播计数
  pa_operation* op = pa_stream_flush(stream, streamSuccessCb, this);
  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(mainloop);
    }
    pa_operation_unref(op);
  }
  pa_threaded_mainloop_unlock(mainloop);
  writeBytes = playBytes.load();
  lastQueueMs = 0;
}

void PulseAudioRender::onClose() {
  std::unique_lock<std::mutex> lock(mtx);
  if (stream && mainloop) {
    pa_threaded_mainloop_lock(mainloop);
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = nullptr;
    pa_threaded_mainloop_unlock(mainloop);
  }
}

void PulseAudioRender::speed(double speed) {
  // PulseAudio 不直接支持变速，与 WASAPI 一致由应用层重采样处理
}

void PulseAudioRender::setVolume(float cvolume) {
  std::unique_lock<std::mutex> lock(mtx);
  // 软件音量: 只存值,onRender 里按样本缩放
  volume = cvolume;
  if (volume < 0.0f) {
    volume = 0.0f;
  } else if (volume > 1.0f) {
    volume = 1.0f;
  }
}

float PulseAudioRender::getVolume() {
  std::unique_lock<std::mutex> lock(mtx);
  return volume;
}

}

#endif  // AVOX_ENABLE_PULSE
#endif  // __ONLY_LINUX__
