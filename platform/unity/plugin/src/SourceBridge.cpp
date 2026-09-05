#include "SourceBridge.h"
#include "PlayerBridge.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <mutex>

namespace {
std::mutex g_srcRegMutex;
std::unordered_map<uint32_t, SourceBridge*> g_srcRegistry;
}  // namespace

SourceBridge* findSourceBridge(uint32_t id) {
  std::lock_guard<std::mutex> lock(g_srcRegMutex);
  auto it = g_srcRegistry.find(id);
  return it == g_srcRegistry.end() ? nullptr : it->second;
}

SourceBridge::SourceBridge(uint32_t id) : id_(id) {
  std::lock_guard<std::mutex> lock(g_srcRegMutex);
  g_srcRegistry[id_] = this;
}

SourceBridge::~SourceBridge() { destroyPlayer(); }

void SourceBridge::setDeviceIndex(int32_t index) { deviceIndex_.store(index); }

bool SourceBridge::open() {
  destroyPlayer();
  // 枚举 (默认 win_mf 摄像头)
  avox::IVideoManager* videoMgr = avox::getVideoManager(avox::getDefaltVideoSdk());
  if (!videoMgr) return false;
  videoMgr->refreshDevices();
  const int32_t count = videoMgr->getDeviceCount();
  if (count <= 0) return false;
  int32_t index = deviceIndex_.load();
  if (index < 0 || index >= count) index = 0;
  player_ = avox::createDevicePlayer();
  if (!player_) return false;
  {
    std::lock_guard<std::mutex> lock(g_srcRegMutex);
    g_srcRegistry[id_] = this;
  }
  player_->setVideoSource(videoMgr->getDevice(index));
  // 麦克风可选 (无音频设备也照常出图)
  if (auto* audioMgr = avox::getAudioManager(avox::getDefaltAudioSdk())) {
    if (audioMgr->getDeviceCount() > 0) player_->setAudioSource(audioMgr->getDevice(0));
  }
  surface_ = player_->getSurfaceRender();
  if (surface_) {
    // CPU 路径: 保留 VK 离屏管线 (enableYuvOut 走 VK 回读), 需要部署 assets/glsl
    surface_->setOffSurface(avox::YuvType::nv12);
    surface_->enableYuvOut(avox::YuvType::nv12);
    avox::addSurfaceRenderOb(surface_, this);
  }
  avox::addSourcePlayerOb(player_, this);
  stateCache_.store((int32_t)avox::PlayerState::opening);
  if (!player_->open()) {
    destroyPlayer();
    stateCache_.store((int32_t)avox::PlayerState::none);
    return false;
  }
  return true;
}

void SourceBridge::close() { destroyPlayer(); }

void SourceBridge::destroyPlayer() {
  if (!player_) return;
  if (surface_) {
    avox::removeSurfaceRenderOb(surface_, this);
    surface_ = nullptr;
  }
  avox::removeSourcePlayerOb(player_, this);
  player_->close();
  // createDevicePlayer() 裸指针无 destroy API, delete 是唯一释放路径 (同 PlayerBridge)
  delete player_;
  player_ = nullptr;
  stateCache_.store((int32_t)avox::PlayerState::none);
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameBgra_.clear();
    frameW_ = 0;
    frameH_ = 0;
  }
}

void SourceBridge::onStateChange(avox::PlayerState preState, avox::PlayerState state) {
  (void)preState;
  stateCache_.store((int32_t)state);
}

void SourceBridge::onReady() {
  avox::ISourceInfo* info = player_ ? player_->getSourceInfo() : nullptr;
  if (info && info->videoSize() > 0) {
    avox::VTrackDesc vd = info->getVideoDesc(0);
    colorSpace_ = vd.desc.colorSpace;
    if (surface_) surface_->setColorSpace(colorSpace_);
  }
}

void SourceBridge::onFrame(const avox::YUVFrame& frame) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  if (!frame.data[0] || w <= 0 || h <= 0) return;
  if (frame.format.type != avox::YuvType::nv12 && frame.format.type != avox::YuvType::yuv420P) return;
  std::vector<uint8_t> bgra;
  bgra.resize((size_t)w * h * 4);
  if (frame.format.type == avox::YuvType::nv12) {
    PlayerBridge::ConvertNv12(frame, bgra.data(), colorSpace_);
  } else {
    PlayerBridge::ConvertYuv420P(frame, bgra.data(), colorSpace_);
  }
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameBgra_ = std::move(bgra);
    frameW_ = w;
    frameH_ = h;
  }
}

bool SourceBridge::frameInfo(int32_t* w, int32_t* h) {
  std::lock_guard<std::mutex> lock(frameMutex_);
  if (frameW_ <= 0 || frameH_ <= 0) return false;
  if (w) *w = frameW_;
  if (h) *h = frameH_;
  return true;
}

bool SourceBridge::allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData) {
  if (!texData || w == 0 || h == 0 || bpp == 0) return false;
  const size_t bytes = (size_t)w * h * bpp;
  uint8_t* buf = (uint8_t*)malloc(bytes);
  if (!buf) return false;
  bool hasFrame = false;
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!frameBgra_.empty() && frameW_ == (int32_t)w && frameH_ == (int32_t)h) {
      memcpy(buf, frameBgra_.data(), bytes);
      hasFrame = true;
    }
  }
  if (!hasFrame) memset(buf, 0, bytes);
  *texData = buf;
  return true;
}
