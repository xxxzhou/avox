#pragma once

#include "AvoxSdk.h"

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <vector>

// 设备源播放器桥接 (相机/采集卡 ISourcePlayer): CPU 帧槽 + 状态缓存。
// 纹理上传复用 avoxTextureUpdateCallback (userData = id), AvoxCameraCapture
// 组件用法与 AvoxPlayer CPU 回退一致。
class SourceBridge : public avox::IMediaPlayerOb, public avox::ISurfaceRenderOb {
 public:
  explicit SourceBridge(uint32_t id);
  ~SourceBridge() override;

  uint32_t id() const { return id_; }

  // 打开前设置: win_mf 摄像头设备索引 (-1 = 第一个)
  void setDeviceIndex(int32_t index);
  bool open();
  void close();
  // avox::PlayerState 数值
  int32_t state() const { return stateCache_.load(); }
  // 帧尺寸 (CPU 槽), 无帧返回 false
  bool frameInfo(int32_t* w, int32_t* h);
  // 纹理更新回调取帧 (同 PlayerBridge), 无帧/尺寸不符补黑
  bool allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData);

 protected:
  // ── avox::IMediaPlayerOb ──
  void onStateChange(avox::PlayerState preState, avox::PlayerState state) override;
  void onReady() override;

  // ── avox::ISurfaceRenderOb (avox 渲染线程) ──
  void onFrame(const avox::YUVFrame& frame) override;

 private:
  void destroyPlayer();

  uint32_t id_;
  avox::ISourcePlayer* player_ = nullptr;
  avox::ISurfaceRender* surface_ = nullptr;
  avox::ColorSpaceDesc colorSpace_;

  std::atomic<int32_t> stateCache_{0};
  std::atomic<int32_t> deviceIndex_{-1};
  std::mutex frameMutex_;
  std::vector<uint8_t> frameBgra_;
  int32_t frameW_ = 0;
  int32_t frameH_ = 0;
};

// 注册表查找 (AvoxUnityApi 与纹理更新回调共用)
SourceBridge* findSourceBridge(uint32_t id);
