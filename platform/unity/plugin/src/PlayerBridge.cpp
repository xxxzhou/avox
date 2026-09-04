#include "PlayerBridge.h"
#include "GpuPassthrough.h"

#include <cstdio>
#include <unordered_map>

#include <windows.h>

// ── 注册表: id → bridge (C API 与纹理更新回调共用) ──
namespace {
std::mutex g_regMutex;
std::unordered_map<uint32_t, PlayerBridge*> g_registry;
}  // namespace

PlayerBridge* findBridge(uint32_t id) {
  std::lock_guard<std::mutex> lock(g_regMutex);
  auto it = g_registry.find(id);
  return it == g_registry.end() ? nullptr : it->second;
}

PlayerBridge::PlayerBridge(uint32_t id) : id_(id) {
  std::lock_guard<std::mutex> lock(g_regMutex);
  g_registry[id_] = this;
}

PlayerBridge::~PlayerBridge() { destroyPlayer(); }

void PlayerBridge::setHardDecode(bool bEnable) { hardDecode_ = bEnable; }

void PlayerBridge::setVolume(float volume) { volume_ = volume; }

void PlayerBridge::setIoPlan(int32_t plan) { ioPlan_ = plan; }

void PlayerBridge::setSpeed(double speed) { speed_ = speed; }

void PlayerBridge::open(const char* url) {
  // 复用中的播放器直接重建, 规避流切换的状态残留 (同 godot/UE 插件)
  destroyPlayer();
  createPlayer();
  stateCache_.store((int32_t)avox::PlayerState::opening);
  pushState((int32_t)avox::PlayerState::opening);
  player_->open(url);
}

void PlayerBridge::close() { destroyPlayer(); }

void PlayerBridge::pause() {
  if (player_) player_->pause();
}

void PlayerBridge::resume() {
  if (player_) player_->resume();
}

void PlayerBridge::seek(int64_t pos) {
  if (player_) player_->seek(pos);
}

int64_t PlayerBridge::duration() const {
  return player_ ? player_->getDuration() : 0;
}

int64_t PlayerBridge::position() const {
  return player_ ? player_->getPosition() : 0;
}

double PlayerBridge::progress() const {
  return player_ ? player_->getProcess() : 0.0;
}

void PlayerBridge::createPlayer() {
  if (player_) return;
  player_ = avox::createMediaPlayer();
  // 本类同时是 IMediaPlayerOb 与 ISurfaceRenderOb
  avox::addMediaPlayerOb(player_, this);
  player_->setHardDecode(hardDecode_);
  if (ioPlan_ != 0) {
    player_->setIoPlan(static_cast<avox::IoPlan>(ioPlan_));
  }
  if (volume_ >= 0.0f && volume_ < 1.0f) {
    if (auto* audio = player_->getAudioRender()) audio->setVolume(volume_);
  }
  bindSurface();
}

void PlayerBridge::bindSurface() {
  surface_ = player_->getSurfaceRender();
  if (!surface_) return;
  // GPU 直通模式: 保持默认 Vulkan 渲染, 尺寸就绪后 enableVkOutput (同 godot)
  // CPU 回退模式: 离屏渲染 + YUV输出, setOffSurface传ytype即自动enableYuvOut
  gpuMode_ = unityGpuPassthroughAvailable();
  if (!gpuMode_) {
    surface_->setVulkan(false);
    surface_->setOffSurface(avox::YuvType::nv12);
    surface_->enableYuvOut(avox::YuvType::nv12);
  }
  avox::addSurfaceRenderOb(surface_, this);
}

void PlayerBridge::unbindSurface() {
  if (!surface_) return;
  avox::removeSurfaceRenderOb(surface_, this);
  if (gpuMode_) {
    // 先释放导入再断输出 (surface render 仍有效, 同 godot unbindSurface)
    releaseGpuImport();
    if (gpuOutputOn_) {
      avox::disableVkOutput(surface_);
      gpuOutputOn_ = false;
    }
    pendingGpuInit_.store(false);
    pendingGpuResize_.store(false);
  }
  surface_ = nullptr;
}

void PlayerBridge::destroyPlayer() {
  if (!player_) return;
  // 先解绑/断GPU输出再关播放器 (解绑需要 surface render 仍有效, 同 godot)
  unbindSurface();
  player_->close();
  avox::removeMediaPlayerOb(player_, this);
  // createMediaPlayer() 是裸 new 无 destroy API, delete 是唯一释放路径
  delete player_;
  player_ = nullptr;
  stateCache_.store((int32_t)avox::PlayerState::none);
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameBgra_.clear();
    frameW_ = 0;
    frameH_ = 0;
  }
  {
    std::lock_guard<std::mutex> lock(eventMutex_);
    events_.clear();
  }
  pushState((int32_t)avox::PlayerState::none);
  videoW_.store(0);
  videoH_.store(0);
}

bool PlayerBridge::pollEvent(AvoxUnityEvent* out) {
  std::lock_guard<std::mutex> lock(eventMutex_);
  if (events_.empty()) return false;
  *out = events_.front();
  events_.pop_front();
  return true;
}

bool PlayerBridge::frameInfo(int32_t* w, int32_t* h) {
  if (gpuMode_) {
    if (!importedImage_ || gpuW_ <= 0 || gpuH_ <= 0) return false;
    if (w) *w = gpuW_;
    if (h) *h = gpuH_;
    return true;
  }
  std::lock_guard<std::mutex> lock(frameMutex_);
  if (frameW_ <= 0 || frameH_ <= 0) return false;
  if (w) *w = frameW_;
  if (h) *h = frameH_;
  return true;
}

void PlayerBridge::updateGpu() {
  if (!gpuMode_ || !surface_) return;
  if (!unityVulkanInit()) return;
  // 尺寸变化: 释放旧导入 → 断输出 → 重新 enable + 导入
  if (importedImage_ && pendingGpuResize_.exchange(false)) {
    const int32_t w = videoW_.load();
    const int32_t h = videoH_.load();
    if (w > 0 && h > 0 && (w != gpuW_ || h != gpuH_)) {
      releaseGpuImport();
      avox::disableVkOutput(surface_);
      gpuOutputOn_ = false;
      startGpuImport(w, h);
    }
  }
  if (!importedImage_ && pendingGpuInit_.exchange(false)) {
    startGpuImport(videoW_.load(), videoH_.load());
  }
}

void PlayerBridge::startGpuImport(int32_t w, int32_t h) {
  if (w <= 0 || h <= 0) return;
  if (!avox::enableVkOutput(surface_, w, h)) {
    pushError(-1, "enableVkOutput failed");
    return;
  }
  gpuOutputOn_ = true;
  avox::VkSharedHandle handle = {};
  if (!avox::getVkOutputHandle(surface_, &handle) || handle.memHandle == 0) {
    pushError(-1, "getVkOutputHandle failed");
    return;
  }
  uint64_t image = 0;
  uint64_t memory = 0;
  if (!unityImportSharedImage((uint64_t)handle.memHandle, w, h, &image, &memory)) {
    pushError(-1, "import shared image into Unity Vulkan device failed");
    return;
  }
  importedImage_ = image;
  importedMemory_ = memory;
  gpuW_ = w;
  gpuH_ = h;
  // 所有权归调用方: NT 句柄用完即关
  CloseHandle((HANDLE)handle.memHandle);
}

void PlayerBridge::releaseGpuImport() {
  if (!importedImage_) return;
  unityReleaseImported(&importedImage_, &importedMemory_);
  importedImage_ = 0;
  importedMemory_ = 0;
  gpuW_ = 0;
  gpuH_ = 0;
}

bool PlayerBridge::allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData) {
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

// ── avox::IMediaPlayerOb (avox 线程) ──

void PlayerBridge::onStateChange(avox::PlayerState preState, avox::PlayerState state) {
  (void)preState;
  stateCache_.store((int32_t)state);
  pushState((int32_t)state);
}

void PlayerBridge::onReady() {
  // onReady 时 sourceInfo 一定有效, 取视频尺寸 (同 godot)
  avox::ISourceInfo* info = player_ ? player_->getSourceInfo() : nullptr;
  if (info && info->videoSize() > 0) {
    avox::VTrackDesc vd = info->getVideoDesc(0);
    videoW_.store(vd.desc.width);
    videoH_.store(vd.desc.height);
    if (gpuMode_ && !gpuOutputOn_) pendingGpuInit_.store(true);
  }
  AvoxUnityEvent e;
  e.type = (int32_t)AvoxUnityEvent::EType::ready;
  pushEvent(e);
}

void PlayerBridge::onComplete() {
  AvoxUnityEvent e;
  e.type = (int32_t)AvoxUnityEvent::EType::complete;
  pushEvent(e);
}

void PlayerBridge::onIoError(avox::AVError error, const char* msg) {
  pushError((int32_t)error, msg ? msg : "io error");
}

void PlayerBridge::onDecodeError(avox::TrackType trackType, avox::DecodeResult error) {
  char buf[192];
  snprintf(buf, sizeof(buf), "decode error [%s]: %s", avox::getTrackTypeStr(trackType),
           avox::getDecodeResultStr(error));
  pushError((int32_t)error, buf);
}

// ── avox::ISurfaceRenderOb (avox 渲染线程) ──

void PlayerBridge::onFrame(const avox::YUVFrame& frame) {
  if (gpuMode_) return;
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  if (!frame.data[0] || w <= 0 || h <= 0) return;
  if (frame.format.type != avox::YuvType::nv12 && frame.format.type != avox::YuvType::yuv420P) return;
  std::vector<uint8_t> bgra;
  bgra.resize((size_t)w * h * 4);
  if (frame.format.type == avox::YuvType::nv12) {
    convertNv12(frame, bgra.data());
  } else {
    convertYuv420P(frame, bgra.data());
  }
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameBgra_ = std::move(bgra);
    frameW_ = w;
    frameH_ = h;
  }
}

void PlayerBridge::onWinSizeChange(int32_t width, int32_t height) {
  if (gpuMode_) {
    // 渲染中途尺寸变化: 标记待主线程重导 (同 godot needReimport)
    if (width > 0 && height > 0) {
      videoW_.store(width);
      videoH_.store(height);
      if (gpuOutputOn_) pendingGpuResize_.store(true);
    }
    return;
  }
  (void)width;
  (void)height;
}

// ── 内部 ──

void PlayerBridge::pushState(int32_t state) {
  AvoxUnityEvent e;
  e.type = (int32_t)AvoxUnityEvent::EType::state;
  e.state = state;
  pushEvent(e);
}

void PlayerBridge::pushError(int32_t code, const char* msg) {
  AvoxUnityEvent e;
  e.type = (int32_t)AvoxUnityEvent::EType::error;
  e.code = code;
  if (msg) snprintf(e.msg, sizeof(e.msg), "%s", msg);
  pushEvent(e);
}

void PlayerBridge::pushEvent(const AvoxUnityEvent& e) {
  std::lock_guard<std::mutex> lock(eventMutex_);
  // 限长防堆积 (主线程卡住时丢弃最旧)
  if (events_.size() > 64) events_.pop_front();
  events_.push_back(e);
}

// ── YUV → BGRA (BT.601 full-range, 同 godot/UE 插件) ──

void PlayerBridge::convertNv12(const avox::YUVFrame& frame, uint8_t* dst) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  const uint8_t* yPlane = frame.data[0];
  const uint8_t* uvPlane = frame.data[1];
  const int32_t yStride = frame.stride[0];
  const int32_t uvStride = frame.stride[1];
  auto toByte = [](int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  for (int32_t j = 0; j < h; ++j) {
    const uint8_t* yRow = yPlane + (size_t)j * yStride;
    const uint8_t* uvRow = uvPlane + (size_t)(j / 2) * uvStride;
    uint8_t* dstRow = dst + (size_t)j * w * 4;
    for (int32_t i = 0; i < w; ++i) {
      const int y = yRow[i];
      const int uvIdx = (i & ~1);
      const int u = uvRow[uvIdx] - 128;
      const int v = uvRow[uvIdx + 1] - 128;
      const int r = y + ((v * 1436) >> 10);
      const int g = y - ((u * 352 + v * 731) >> 10);
      const int b = y + ((u * 1815) >> 10);
      dstRow[i * 4] = toByte(b);
      dstRow[i * 4 + 1] = toByte(g);
      dstRow[i * 4 + 2] = toByte(r);
      dstRow[i * 4 + 3] = 255;
    }
  }
}

void PlayerBridge::convertYuv420P(const avox::YUVFrame& frame, uint8_t* dst) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  const uint8_t* yPlane = frame.data[0];
  const uint8_t* uPlane = frame.data[1];
  const uint8_t* vPlane = frame.data[2];
  const int32_t yStride = frame.stride[0];
  const int32_t uvStride = frame.stride[1] > 0 ? frame.stride[1] : w / 2;
  auto toByte = [](int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  for (int32_t j = 0; j < h; ++j) {
    const uint8_t* yRow = yPlane + (size_t)j * yStride;
    const uint8_t* uRow = uPlane + (size_t)(j / 2) * uvStride;
    const uint8_t* vRow = vPlane + (size_t)(j / 2) * uvStride;
    uint8_t* dstRow = dst + (size_t)j * w * 4;
    for (int32_t i = 0; i < w; ++i) {
      const int y = yRow[i];
      const int u = uRow[i / 2] - 128;
      const int v = vRow[i / 2] - 128;
      const int r = y + ((v * 1436) >> 10);
      const int g = y - ((u * 352 + v * 731) >> 10);
      const int b = y + ((u * 1815) >> 10);
      dstRow[i * 4] = toByte(b);
      dstRow[i * 4 + 1] = toByte(g);
      dstRow[i * 4 + 2] = toByte(r);
      dstRow[i * 4 + 3] = 255;
    }
  }
}
