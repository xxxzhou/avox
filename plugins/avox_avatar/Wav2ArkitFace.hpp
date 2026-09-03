#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "avox/audio/AudioFace.hpp"
#include "avox/audio/AudioReshaper.hpp"
#include "avox/module/Ringbuffer.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/vision/OnnxModel.hpp"
#include "avox/vision/OnnxModelUser.hpp"

namespace avox {

class IONNXSession;

// 单个重采样后的 16kHz float32 mono PCM 块 (onProcess 入队 -> worker 取出推理)
struct Wav2ArkitChunk {
  std::vector<uint8_t> data;  // float32 样本 (16kHz mono)
  int64_t pts = 0;            // 块首样本 pts(ms)
};

// wav2arkit_cpu 后端: PCM -> ARKit52 blendshape (虚拟人口型)。
// 对称 SherpaAudioStt 的三明治继承: AudioFace(对外接口+Observer 分发)
// + AudioReshaper(任意 PCM -> 16kHz mono float32) + RunTask(worker 推理) +
// OnnxModelUser(共享 session)。 数据流: feed -> AudioReshaper::process
// 重采样/切 0.5s 帧 -> onProcess 深拷贝入块队列
// -> onRunTask 取块 runShaped -> [1,frames,52] 逐帧 dispatch onFaceBlendshape
//   (pts=块pts+f*1000/30, 30fps; raw52 bRef 指向 outputs, dispatch
//   同步期内有效)。
// 模型经 OnnxSessionCache 全局复用 (385MB 加载一次); CPU 推理 ~45ms/秒 (22x
// 实时), 0.5s 块 ~22ms。 接口稳定: 换 LAM/NeuroSync 只换本插件内部 + manifest,
// IAudioFace 不变。
class Wav2ArkitFace : public AudioFace,
                      public AudioReshaper,
                      public RunTask,
                      public OnnxModelUser {
 public:
  Wav2ArkitFace();
  ~Wav2ArkitFace() override;

  // ========== AudioFace 接口 ==========
  void setAudioDesc(AudioDesc desc) override;
  void start() override;
  void feed(const AvoxData& pcm, int64_t pts) override;
  void stop() override;
  bool loading() override;

 protected:
  // RunTask - 推理主循环 (initEngine -> 循环取块推理 -> 短排空)
  void onRunTask() override;
  // AudioReshaper - 重采样帧就绪回调 (在 feed 调用线程触发), 深拷贝入队
  void onProcess() override;

 private:
  bool initEngine();
  void runChunk(const Wav2ArkitChunk& chunk);
  RingBuffer<Wav2ArkitChunk> chunkQueue;
  std::mutex mutex;
  // 借用 (OnnxSessionCache/OnnxModelUser), 不 delete
  IONNXSession* session = nullptr;
  std::string inName;
  std::string outName;
  bool bInited = false;
};

}
