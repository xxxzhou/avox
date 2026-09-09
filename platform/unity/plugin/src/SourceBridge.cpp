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
    frameNv12_.clear();
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
    colorSpaceSet_ = true;
    if (surface_) surface_->setColorSpace(colorSpace_);
  }
}

// 只重排成紧凑 NV12, 色转交给 Unity 侧 shader (同 PlayerBridge)
void SourceBridge::onFrame(const avox::YUVFrame& frame) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  if (w <= 0 || h <= 0 || (w & 1) || (h & 1)) return;
  std::vector<uint8_t> nv12;
  nv12.resize((size_t)w * h * 3 / 2);
  if (!PlayerBridge::PackNv12(frame, nv12.data())) return;
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameNv12_ = std::move(nv12);
    frameW_ = w;
    frameH_ = h;
  }
}

int32_t SourceBridge::colorSpaceCode() const {
  if (!colorSpaceSet_) return -1;
  return (int32_t)colorSpace_.standard | ((int32_t)colorSpace_.range << 8);
}

bool SourceBridge::frameInfo(int32_t* w, int32_t* h) {
  std::lock_guard<std::mutex> lock(frameMutex_);
  if (frameW_ <= 0 || frameH_ <= 0) return false;
  if (w) *w = frameW_;
  if (h) *h = frameH_;
  return true;
}

// R8 的 w × h*3/2 (整帧 NV12); 无帧填中性黑 Y=16/UV=128 (同 PlayerBridge)
bool SourceBridge::allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData) {
  if (!texData || w == 0 || h == 0 || bpp != 1) return false;
  const size_t bytes = (size_t)w * h;
  uint8_t* buf = (uint8_t*)malloc(bytes);
  if (!buf) return false;
  bool hasFrame = false;
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!frameNv12_.empty() && frameW_ == (int32_t)w &&
        (int32_t)h == frameH_ * 3 / 2 && frameNv12_.size() == bytes) {
      memcpy(buf, frameNv12_.data(), bytes);
      hasFrame = true;
    }
  }
  if (!hasFrame) {
    const size_t ySize = (size_t)w * (h * 2 / 3);
    memset(buf, 16, ySize < bytes ? ySize : bytes);
    if (ySize < bytes) memset(buf + ySize, 128, bytes - ySize);
  }
  *texData = buf;
  return true;
}
