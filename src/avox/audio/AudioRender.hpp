#pragma once

#include <string>

#include "../audio/AudioFrame.hpp"
#include "../player/Player.hpp"
#include "AudioProcess.hpp"
#include "AudioTap.hpp"

namespace avox {
struct ARenderDesc {
  std::string name = "AudioRender";
};
// 音频渲染通用中间层
// 负责:模板方法 render()(3A 处理 +
// 设备分发)、AudioProcess(AEC/3A)、AudioTap(外部读取)
// 设备专属方法(empty/full/getQueueMS/pause/flush/speed)不在本层,见 AudioOutput
// 裸 AudioRender(不派生 AudioOutput)可用于无设备场景(如转码),onRender
// 默认空体不发声,tap 照常工作
class AVOX_EXPORT AudioRender : public IAudioRender, public IAudioProcessOb {
 public:
  AudioRender();
  virtual ~AudioRender();

 protected:
  AudioDesc desc = {};
  int32_t frameMs = 40;
  float volume = 1.0f;
  int32_t frameSize = 0;
  bool enableProcess = false;
  bool initProcess = false;
  bool closeOutput = false;
  std::unique_ptr<AudioProcess> audioProcess = nullptr;
  std::unique_ptr<AudioTap> audioTap = nullptr;
  // tap 满队列策略(唯一真相源):tap 未创建时只存此,创建时据此初始化
  bool bTapBlock = false;
  // tap 延迟打开:openTap 时 desc 未就绪则缓存参数,setDesc 后自动 open
  bool bTapPending = false;
  AudioDesc pendingTapOutDesc = {};
  int32_t pendingTapFrameMs = 40;

 protected:
  // 设备钩子:默认空体。AudioOutput override 推硬件;裸 AudioRender 不发声
  virtual void onInit() {}
  virtual void onRender(const AvoxData& frame) {}
  virtual void onClose() {}

 public:
  void setDesc(AudioDesc desc, int32_t frameMs = 40);
  void render(const AvoxData& frame, int64_t pts = 0);
  void close();
  // 是否打开设备输出(默认 true,即发声),tap 照常工作
  void enableOutput(bool bEnable) { closeOutput = !bEnable; }
  // tap observer 增删(供 addAudioTapOb/removeAudioTapOb free function 调用)
  void addTapOb(IAudioTapOb* ob);
  void removeTapOb(IAudioTapOb* ob);
  // tap 满队列策略:set 时同步给已存在的 audioTap,tap 未创建只存成员
  void setTapBlock(bool b);

 public:
  // IAudioProcessOb
  virtual void onAudioProcess(const AvoxAFrame& frame) override;

 public:
  virtual void setVolume(float volume) override {};
  virtual float getVolume() override { return 1.0f; };
  virtual void enableAec(const AudioAec& aec) override;
  virtual void disableAec() override;
  // IAudioRender tap
  virtual void openTap(const AudioDesc& outDesc, int32_t frameMs) override;
  virtual void closeTap() override;
};

ARenderType getDefaultAudioType();
}