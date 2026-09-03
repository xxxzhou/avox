#include "Wav2ArkitFace.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>

#include "avox/Avox.hpp"  // timeStampMS / LOGFLF

namespace avox {

namespace {
// ARKit52 blendshape 数量 (wav2arkit 输出 [1,frames,52])
constexpr int32_t kArkitBlendshapeCount = 52;
// 30fps: 每帧 ms (整数 1000/30 取 33, 帧间累积用 f*1000/30 保精度)
constexpr int32_t kArkitFps = 30;
}  // namespace

Wav2ArkitFace::Wav2ArkitFace() : chunkQueue(64) {
  taskName = "wav2arkit face task";
}

Wav2ArkitFace::~Wav2ArkitFace() {
  stop();
}

// ========== AudioFace 接口 ==========

bool Wav2ArkitFace::loading() { return running(); }

void Wav2ArkitFace::setAudioDesc(AudioDesc desc) {
  std::lock_guard<std::mutex> lock(mutex);
  // wav2arkit 需要 16kHz mono float32 原始波形 (无 mel/归一化)
  srcDesc = desc;
  outDesc = desc;
  outDesc.format = AudioFormat::AVOX_AUDIO_FLT;
  outDesc.channels = 1;
  outDesc.sampleRate = 16000;
  // 0.5s/块 (~15 帧/块, 推理 ~22ms; 块大上下文足质量好, 块小延迟低)
  frameMs = 500;
  if (!initConfig(srcDesc, outDesc)) {
    LOGFLF(LogLevel::warn, "Wav2ArkitFace reshaper init failed");
  }
}

void Wav2ArkitFace::start() {
  // 幂等: 已在跑不重启; 清上一轮残留队列 (避免跨段串帧)
  if (!running()) {
    chunkQueue.clear();
    startTask();
  }
}

void Wav2ArkitFace::feed(const AvoxData& pcm, int64_t pts) {
  if (!running()) {
    start();
  }
  if (!resample || pcm.size <= 0) {
    return;
  }
  AvoxAFrame frame = {};
  frame.buffer = pcm;
  frame.pts = (pts == 0) ? timeStampMS() : pts;
  AudioReshaper::process(frame);
}

void Wav2ArkitFace::stop() { stopTask(); }

// ========== AudioReshaper 接口 ==========

void Wav2ArkitFace::onProcess() {
  // 重采样后的 16kHz float32 数据, 深拷贝入队 (worker 线程推理)
  uint8_t* data = curFrame.point();
  int32_t size = curFrame.getSize();
  if (size <= 0) {
    return;
  }
  Wav2ArkitChunk item;
  item.data.assign(data, data + size);
  item.pts = curFrame.getPts();
  chunkQueue.enqueue(item, true);
}

// ========== RunTask ==========

void Wav2ArkitFace::onRunTask() {
  // 首次进入: 加载模型 (OnnxSessionCache 共享, 后续 start 复用不重载)
  if (!initEngine()) {
    LOGFLF(LogLevel::warn, "Wav2ArkitFace init engine failed (model missing?)");
    dispatch(&IAudioFaceOb::onFaceError, "wav2arkit model load failed");
    return;
  }
  FaceDesc fd = {kArkitFps, kArkitBlendshapeCount};
  dispatch(&IAudioFaceOb::onFaceDesc, fd);
  while (running()) {
    Wav2ArkitChunk item;
    if (chunkQueue.dequeue(item)) {
      runChunk(item);
    } else {
      sleepTask(false, 10);
    }
  }
  // running()=false: 短排空已入队的完整块 (不 flush 重采样尾, 让嘴在 barge-in 时尽快归位)。
  // 排空耗时 ≈ 队列深度 × 40ms (实时队列深 ~1, stop 通常 ~40ms 返回)。
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  while (std::chrono::steady_clock::now() < deadline) {
    Wav2ArkitChunk item;
    if (!chunkQueue.dequeue(item)) {
      sleepTask(false, 20);
      if (!chunkQueue.dequeue(item)) {
        break;
      }
    }
    runChunk(item);
  }
  chunkQueue.clear();
}

// ========== 内部方法 ==========

bool Wav2ArkitFace::initEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!bInited) {
    // 共享 session: 385MB 模型全局加载一次 (OnnxSessionCache), 多实例复用
    session = OnnxModelUser::session(OnnxModel::Wav2ArkitCpu, false, 0, 4);
    if (!session || !session->isLoaded()) {
      session = nullptr;
      return false;
    }
    auto ins = session->getInputNames();
    auto outs = session->getOutputNames();
    if (ins.empty() || outs.empty()) {
      return false;
    }
    inName = ins[0];
    outName = outs[0];
    bInited = true;
  }
  return session && session->isLoaded();
}

void Wav2ArkitFace::runChunk(const Wav2ArkitChunk& chunk) {
  if (!session || chunk.data.empty()) {
    return;
  }
  const float* samples = reinterpret_cast<const float*>(chunk.data.data());
  int32_t n = static_cast<int32_t>(chunk.data.size() / sizeof(float));
  if (n <= 0) {
    return;
  }
  std::vector<std::tuple<std::string, const float*, std::vector<int64_t>>>
      inputs = {{inName, samples, {1, static_cast<int64_t>(n)}}};
  std::vector<std::string> outNames = {outName};
  std::vector<std::vector<float>> outputs;
  if (!session->runShaped(inputs, outNames, outputs) || outputs.empty()) {
    dispatch(&IAudioFaceOb::onFaceError, "wav2arkit runShaped failed");
    return;
  }
  const auto& bs = outputs[0];
  if (bs.size() % kArkitBlendshapeCount != 0) {
    return;
  }
  int frames = static_cast<int>(bs.size() / kArkitBlendshapeCount);
  for (int f = 0; f < frames; ++f) {
    // 原生序 (browDownLeft@0..noseSneerRight@50, tongueOut@51) → canonical (AvoxAvatar.h 名表):
    // 51 实名同序整体 +1, index0=_neutral 补零, tongueOut 无槽丢弃 — 与视频路 (mediapipe) 单一顺序
    float out52[kArkitBlendshapeCount];
    out52[0] = 0.0f;  // _neutral 占位
    memcpy(out52 + 1, bs.data() + f * kArkitBlendshapeCount,
           (kArkitBlendshapeCount - 1) * sizeof(float));
    // 30fps: 帧 pts = 块首 pts + f*(1000/30)ms
    int64_t pts = chunk.pts + static_cast<int64_t>(f) * 1000 / kArkitFps;
    AvoxData raw = {};
    raw.data = reinterpret_cast<uint8_t*>(out52);
    raw.size = kArkitBlendshapeCount * static_cast<int32_t>(sizeof(float));
    raw.bRef = true;  // dispatch 同步, 回调内有效 (观察方需即时拷贝)
    bool isFinal = (f == frames - 1);
    dispatch(&IAudioFaceOb::onFaceBlendshape, raw, pts, isFinal);
  }
}

}
