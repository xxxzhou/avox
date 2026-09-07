#include "PlayerBridge.h"
#include "GpuPassthrough.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/hardware_buffer.h>
#endif

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

PlayerBridge::PlayerBridge(uint32_t id) : playerId(id) {
  std::lock_guard<std::mutex> lock(g_regMutex);
  g_registry[playerId] = this;
}

PlayerBridge::~PlayerBridge() { destroyPlayer(); }

void PlayerBridge::setHardDecode(bool bEnable) { hardDecode = bEnable; }

void PlayerBridge::setVolume(float volume_) { volume = volume_; }

void PlayerBridge::setIoPlan(int32_t plan) { ioPlan = plan; }

void PlayerBridge::setSpeed(double speed_) { speed = speed_; }

void PlayerBridge::open(const char* url) {
  // 复用中的播放器直接重建, 规避流切换的状态残留 (同 godot/UE 插件)
  destroyPlayer();
  createPlayer();
  stateCache.store((int32_t)avox::PlayerState::opening);
  pushState((int32_t)avox::PlayerState::opening);
  player->open(url);
}

void PlayerBridge::close() { destroyPlayer(); }

void PlayerBridge::pause() {
  if (player) player->pause();
}

void PlayerBridge::resume() {
  if (player) player->resume();
}

void PlayerBridge::seek(int64_t pos) {
  if (player) player->seek(pos);
}

int64_t PlayerBridge::duration() const {
  return player ? player->getDuration() : 0;
}

int64_t PlayerBridge::position() const {
  return player ? player->getPosition() : 0;
}

double PlayerBridge::progress() const {
  return player ? player->getProcess() : 0.0;
}

void PlayerBridge::createPlayer() {
  if (player) return;
  player = avox::createMediaPlayer();
  // 本类同时是 IMediaPlayerOb 与 ISurfaceRenderOb
  avox::addMediaPlayerOb(player, this);
  player->setHardDecode(hardDecode);
  if (ioPlan != 0) {
    player->setIoPlan(static_cast<avox::IoPlan>(ioPlan));
  }
  if (volume >= 0.0f && volume < 1.0f) {
    if (auto* audio = player->getAudioRender()) audio->setVolume(volume);
  }
  bindSurface();
}

void PlayerBridge::bindSurface() {
  surface = player->getSurfaceRender();
  if (!surface) return;
  // GPU 直通模式: 保持默认 Vulkan 渲染, 尺寸就绪后 enableVkOutput (同 godot)
  // CPU 回退模式: 离屏渲染 + YUV输出, setOffSurface传ytype即自动enableYuvOut
  bGpuMode = unityGpuPassthroughAvailable();
  if (!bGpuMode) {
    // CPU 回退: 保留 avox 自身 Vulkan 离屏管线渲染 (enableYuvOut 走 VK 回读),
    // 需要部署 assets/glsl 着色器; 不能 setVulkan(false) (DX11 回读未启用, 无帧)
    surface->setOffSurface(avox::YuvType::nv12);
    surface->enableYuvOut(avox::YuvType::nv12);
  }
  avox::addSurfaceRenderOb(surface, this);
}

void PlayerBridge::unbindSurface() {
  if (!surface) return;
  avox::removeSurfaceRenderOb(surface, this);
  if (bGpuMode) {
    // 先释放导入再断输出 (surface render 仍有效, 同 godot unbindSurface)
    releaseGpuImport();
    if (gpuOutputOn) {
      if (unityGpuImportFlavor() != 1) {
        avox::disableVkOutputDx11(surface);
      } else {
        avox::disableVkOutput(surface);
      }
      gpuOutputOn = false;
    }
    pendingGpuInit.store(false);
    pendingGpuResize.store(false);
    gpuErrorPushed = false;
    gpuRetry = 0;
  }
  surface = nullptr;
}

void PlayerBridge::destroyPlayer() {
  if (!player) return;
  // 先解绑/断GPU输出再关播放器 (解绑需要 surface render 仍有效, 同 godot)
  unbindSurface();
  stopRecord();
  player->close();
  avox::removeMediaPlayerOb(player, this);
  // createMediaPlayer() 是裸 new 无 destroy API, delete 是唯一释放路径
  delete player;
  player = nullptr;
  stateCache.store((int32_t)avox::PlayerState::none);
  {
    std::lock_guard<std::mutex> lock(frameMutex);
    frameBgra.clear();
    frameW = 0;
    frameH = 0;
  }
  {
    std::lock_guard<std::mutex> lock(eventMutex);
    events.clear();
  }
  pushState((int32_t)avox::PlayerState::none);
  videoW.store(0);
  videoH.store(0);
}

bool PlayerBridge::pollEvent(AvoxUnityEvent* out) {
  std::lock_guard<std::mutex> lock(eventMutex);
  if (events.empty()) return false;
  *out = events.front();
  events.pop_front();
  return true;
}

bool PlayerBridge::frameInfo(int32_t* w, int32_t* h) {
  if (bGpuMode) {
    if (unityGpuImportFlavor() != 1) {
      // D3D11/D3D12 拷贝模式: 渲染线程打开共享纹理后回填实际尺寸
      const int32_t dw = dx11W.load();
      const int32_t dh = dx11H.load();
      if (dw <= 0 || dh <= 0) return false;
      if (w) *w = dw;
      if (h) *h = dh;
      return true;
    }
    if (!importedImage || gpuW <= 0 || gpuH <= 0) return false;
    if (w) *w = gpuW;
    if (h) *h = gpuH;
    return true;
  }
  std::lock_guard<std::mutex> lock(frameMutex);
  if (frameW <= 0 || frameH <= 0) return false;
  if (w) *w = frameW;
  if (h) *h = frameH;
  return true;
}

void PlayerBridge::updateGpu() {
  if (!bGpuMode || !surface) return;
  if (!unityVulkanInit()) return;
  const bool flavorDx11 = (unityGpuImportFlavor() != 1);
  // 就绪标志: VK=已导入 Unity 设备 / D3D11=共享句柄已取到
  const bool ready = flavorDx11 ? dx11Handle.load() != 0 : importedImage != 0;
  // 尺寸变化: 释放旧导入 → 断输出 → 重新 enable + 导入
  if (ready && pendingGpuResize.exchange(false)) {
    releaseGpuImport();
    if (flavorDx11) {
      avox::disableVkOutputDx11(surface);
    } else {
      avox::disableVkOutput(surface);
    }
    gpuOutputOn = false;
    startGpuImport(videoW.load(), videoH.load());
  }
  if (!ready && pendingGpuInit.exchange(false)) {
    startGpuImport(videoW.load(), videoH.load());
    // enable/句柄获取失败多为渲染管线尚未建好 (outputLayer 未生成), 限次重试
    const bool nowReady = flavorDx11 ? dx11Handle.load() != 0 : importedImage != 0;
    if (!nowReady) {
      if (gpuRetry == 0) {
        gpuRetryStart = std::chrono::steady_clock::now();
      }
      gpuRetry++;
      // 按时间窗口重试 (batchmode 帧率不定, 纯计数会瞬间烧完)
      const auto retryMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - gpuRetryStart)
                               .count();
      if (retryMs < 15000) {
        pendingGpuInit.store(true);
      } else if (!gpuErrorPushed) {
        gpuErrorPushed = true;
        pushError(-1, "gpu passthrough import failed repeatedly");
      }
    } else {
      gpuRetry = 0;
    }
  }
}

void PlayerBridge::startGpuImport(int32_t w, int32_t h) {
  if (unityGpuImportFlavor() != 1) {
    // D3D11/D3D12 拷贝模式: avox 底层自建共享纹理 (尺寸随管线), 句柄就绪后
    // 由渲染线程 avoxDx11RenderEvent 打开并拷贝
    if (!avox::enableVkOutputDx11(surface)) {
      // 可重试: 管线未建好等瞬态, 静默 (由 updateGpu 限次重试)
      return;
    }
    gpuOutputOn = true;
    const uint64_t handle = avox::getVkOutputDx11Handle(surface);
    if (!handle) return;
    dx11Handle.store(handle);
    dxFenceHandle.store(avox::getVkOutputDx11FenceHandle(surface));
    return;
  }
  if (w <= 0 || h <= 0) return;
  if (!avox::enableVkOutput(surface, w, h)) {
    // 可重试: 管线未建好等瞬态, 静默 (由 updateGpu 限次重试)
    return;
  }
  gpuOutputOn = true;
  avox::VkSharedHandle handle = {};
  if (!avox::getVkOutputHandle(surface, &handle)) return;
  uint64_t image = 0;
  uint64_t memory = 0;
#ifdef _WIN32
  if (handle.memHandle == 0) return;
  if (!unityImportSharedImage((uint64_t)handle.memHandle, w, h, &image, &memory)) {
    CloseHandle((HANDLE)handle.memHandle);
    return;
  }
  importedImage = image;
  importedMemory = memory;
  gpuW = w;
  gpuH = h;
  // 所有权归调用方: NT 句柄用完即关
  CloseHandle((HANDLE)handle.memHandle);
#else
  // Android: AHB 转移语义 (avox getVkOutputHandle 返回的引用归本类),
  // 导入失败当场释放; 成功后引用由 releaseGpuImport 释放
  void* ahb = handle.ahb;
  if (ahb == nullptr) return;
  if (!unityImportSharedImageAhb(ahb, w, h, &image, &memory)) {
    AHardwareBuffer_release((AHardwareBuffer*)ahb);
    return;
  }
  importedImage = image;
  importedMemory = memory;
  importedAhb = ahb;
  gpuW = w;
  gpuH = h;
#endif
}

void PlayerBridge::releaseGpuImport() {
  if (unityGpuImportFlavor() != 1) {
    // D3D11/D3D12 拷贝模式: 打开的资源仅渲染线程可碰, 转交渲染线程释放;
    // 句柄由 avox 持有, 不 CloseHandle
    dx11PendingClose.store(true);
    dx11Handle.store(0);
    dxFenceHandle.store(0);
    dx11W.store(0);
    dx11H.store(0);
    dx11Target.store(nullptr);
    return;
  }
  if (!importedImage) return;
  unityReleaseImported(&importedImage, &importedMemory);
#ifdef __ANDROID__
  if (importedAhb) {
    AHardwareBuffer_release((AHardwareBuffer*)importedAhb);
    importedAhb = nullptr;
  }
#endif
  gpuW = 0;
  gpuH = 0;
}

void PlayerBridge::renderDx11Copy() {
#ifndef _WIN32
  // Android 无 D3D 拷贝模式 (flavor 只有 0/1), C# 侧也不会下发渲染事件
  (void)this;
  return;
#else
  const bool dx12Mode = (unityGpuImportFlavor() == 3);
  dbgEvents.fetch_add(1);
  if (dx11PendingClose.exchange(false)) {
    if (dx12Mode) {
      unityDx12Close(&dx12);
    } else {
      unityDx11Close(&dx11);
    }
  }
  // 管线重建 (字幕层启停/分辨率变化等) 重造 outputLayer, dx11 声明随旧对象
  // 丢失 (新层句柄读回 0): 重新幂等声明触发重绑导出 (同 Godot 桥每帧重调);
  // 有句柄时每帧刷新: 变化即重开, C# 侧检测原生指针变化自动重建外部纹理包裹
  if (surface) {
    const uint64_t fresh = avox::getVkOutputDx11Handle(surface);
    if (fresh != 0) {
      if (fresh != dx11Handle.load()) {
        dx11Handle.store(fresh);
        dxFenceHandle.store(avox::getVkOutputDx11FenceHandle(surface));
      }
    } else if (dx11Handle.load() != 0) {
      // 曾有句柄而读不到了: 重建把绑定丢了, 声明后等新句柄 (重建窗口内层为
      // 空, enableVkOutputDx11 内部拒绝, 下帧重试)
      avox::enableVkOutputDx11(surface);
      return;
    }
  }
  const uint64_t handle = dx11Handle.load();
  if (!handle) return;
  if (dx12Mode) {
    // D3D12: OpenSharedHandle 打开, 每帧录 Unity 命令列表拷到 C# 纹理
    if (!unityDx12EnsureOpened(&dx12, handle, dxFenceHandle.load())) return;
    dx11W.store(dx12.width);
    dx11H.store(dx12.height);
    unityDx12SetTarget(&dx12, dx12Target.load());
    dbgFenceVal.store(unityDx12FenceValue(&dx12));
    const int r = unityDx12CopyFrame(&dx12);
    if (r == 0) return;
    if (r == 1) dbgTargetNull.fetch_add(1);
    return;
  }
  if (!unityDx11EnsureOpened(&dx11, handle, dxFenceHandle.load())) return;
  dx11W.store(dx11.width);
  dx11H.store(dx11.height);
  dbgFenceVal.store(unityDx11FenceValue(&dx11));
  // 拷到插件自建的目标纹理 (格式与共享纹理一致), C# 经 CreateExternalTexture 包裹
  if (unityDx11CopyFrame(&dx11)) return;
  dbgTargetNull.fetch_add(1);
#endif  // _WIN32
}

uint64_t PlayerBridge::dx11NativeTex() {
  return dx11.targetTex;
}

bool PlayerBridge::allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp, void** texData) {
  if (!texData || w == 0 || h == 0 || bpp == 0) return false;
  const size_t bytes = (size_t)w * h * bpp;
  uint8_t* buf = (uint8_t*)malloc(bytes);
  if (!buf) return false;
  bool hasFrame = false;
  {
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameBgra.empty() && frameW == (int32_t)w && frameH == (int32_t)h) {
      memcpy(buf, frameBgra.data(), bytes);
      hasFrame = true;
    }
  }
  if (!hasFrame) memset(buf, 0, bytes);
  *texData = buf;
  return true;
}

// ── Option/录制/字幕 ──

avox::IOption* PlayerBridge::option() const { return player ? player->getOption() : nullptr; }

bool PlayerBridge::setOptionBool(const char* key, bool value) {
  if (!option() || !key) return false;
  option()->setBool(key, value);
  return true;
}

bool PlayerBridge::setOptionInt(const char* key, int64_t value) {
  if (!option() || !key) return false;
  option()->setInt(key, value);
  return true;
}

bool PlayerBridge::setOptionNumber(const char* key, double value) {
  if (!option() || !key) return false;
  option()->setNumber(key, value);
  return true;
}

bool PlayerBridge::setOptionString(const char* key, const char* value) {
  if (!option() || !key) return false;
  option()->setString(key, value ? value : "");
  return true;
}

int32_t PlayerBridge::optionType(const char* key) {
  if (!option() || !key) return -1;
  return (int32_t)option()->getType(key);
}

int64_t PlayerBridge::optionInt(const char* key) {
  return option() && key ? option()->getInt(key) : 0;
}

double PlayerBridge::optionNumber(const char* key) {
  return option() && key ? option()->getDouble(key) : 0.0;
}

int32_t PlayerBridge::optionString(const char* key, char* buf, int32_t bufSize) {
  if (!option() || !key || !buf || bufSize <= 0) return -1;
  if (option()->getType(key) != avox::ArgType::String) return -1;
  const char* value = option()->getString(key);
  if (!value) return -1;
  snprintf(buf, (size_t)bufSize, "%s", value);
  return (int32_t)strlen(value);
}

bool PlayerBridge::startRecord(const char* path, bool bTranscode) {
  if (!player || !path || !path[0]) return false;
  stopRecord();
  muxer = player->getMuxer(bTranscode);
  if (!muxer) return false;
  muxer->setMuxerType(avox::MuxerType::ffmpeg);
  return muxer->open(path);
}

void PlayerBridge::stopRecord() {
  if (!muxer) return;
  muxer->close();
  muxer = nullptr;
}

int32_t PlayerBridge::recordState() {
  return muxer ? (int32_t)muxer->getState() : 0;
}

bool PlayerBridge::loadSrt(const char* path) {
  auto* sub = player ? player->getSubtitle() : nullptr;
  return sub && path && path[0] ? sub->loadSrt(path) : false;
}

void PlayerBridge::closeSubtitle() {
  if (player) {
    if (auto* sub = player->getSubtitle()) sub->close();
  }
}

// ── avox::IMediaPlayerOb (avox 线程) ──

void PlayerBridge::onStateChange(avox::PlayerState preState, avox::PlayerState state) {
  (void)preState;
  stateCache.store((int32_t)state);
  pushState((int32_t)state);
}

void PlayerBridge::onReady() {
  // onReady 时 sourceInfo 一定有效, 取视频尺寸 (同 godot)
  avox::ISourceInfo* info = player ? player->getSourceInfo() : nullptr;
  if (info && info->videoSize() > 0) {
    avox::VTrackDesc vd = info->getVideoDesc(0);
    videoW.store(vd.desc.width);
    videoH.store(vd.desc.height);
    // 色彩空间就绪: 下发渲染管线 (shader 矩阵热更新) + CPU 回退转换共用
    if (vd.desc.colorSpace.standard != colorSpace.standard ||
        vd.desc.colorSpace.range != colorSpace.range) {
      colorSpace = vd.desc.colorSpace;
      if (surface) surface->setColorSpace(colorSpace);
    }
    if (bGpuMode && !gpuOutputOn) pendingGpuInit.store(true);
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
  static std::atomic<uint32_t> frameCount{0};
  if (bGpuMode) return;
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  if (frameCount.fetch_add(1) == 0)
  if (!frame.data[0] || w <= 0 || h <= 0) return;
  if (frame.format.type != avox::YuvType::nv12 && frame.format.type != avox::YuvType::yuv420P) return;
  std::vector<uint8_t> bgra;
  bgra.resize((size_t)w * h * 4);
  if (frame.format.type == avox::YuvType::nv12) {
    ConvertNv12(frame, bgra.data(), colorSpace);
  } else {
    ConvertYuv420P(frame, bgra.data(), colorSpace);
  }
  {
    std::lock_guard<std::mutex> lock(frameMutex);
    frameBgra = std::move(bgra);
    frameW = w;
    frameH = h;
  }
}

void PlayerBridge::onWinSizeChange(int32_t width, int32_t height) {
  if (bGpuMode) {
    // 渲染中途尺寸变化: 标记待主线程重导 (同 godot needReimport)
    if (width > 0 && height > 0) {
      videoW.store(width);
      videoH.store(height);
      if (gpuOutputOn) pendingGpuResize.store(true);
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
  std::lock_guard<std::mutex> lock(eventMutex);
  // 限长防堆积 (主线程卡住时丢弃最旧)
  if (events.size() > 64) events.pop_front();
  events.push_back(e);
}

// ── YUV → BGRA (矩阵标准/量程取自源 colorSpace, 与渲染管线 shader 同参) ──

namespace {
// BT.601/709/2020 full-range 转换系数 (<<10 定点)
void pickMatrix(avox::YuvStandard standard, int& rCof, int& guCof, int& gvCof, int& bCof) {
  switch (standard) {
    case avox::YuvStandard::bt709:
      rCof = 1613;
      guCof = 192;
      gvCof = 479;
      bCof = 1900;
      break;
    case avox::YuvStandard::bt2020:
      rCof = 1510;
      guCof = 168;
      gvCof = 585;
      bCof = 1926;
      break;
    default:
      rCof = 1436;
      guCof = 352;
      gvCof = 731;
      bCof = 1815;
      break;
  }
}
// limited(MPEG 16~235/240) → full(0~255) 展开
inline int expandY(int y, bool limited) { return limited ? ((1192 * y - 19072) >> 10) : y; }
inline int expandC(int c, bool limited) { return limited ? ((1166 * (c - 128)) >> 10) : c - 128; }
}  // namespace

void PlayerBridge::ConvertNv12(const avox::YUVFrame& frame, uint8_t* dst,
                               const avox::ColorSpaceDesc& cs) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  const uint8_t* yPlane = frame.data[0];
  const uint8_t* uvPlane = frame.data[1];
  const int32_t yStride = frame.stride[0];
  const int32_t uvStride = frame.stride[1];
  int rCof;
  int guCof;
  int gvCof;
  int bCof;
  pickMatrix(cs.standard, rCof, guCof, gvCof, bCof);
  const bool limited = cs.range == avox::YuvRange::limited;
  auto toByte = [](int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  for (int32_t j = 0; j < h; ++j) {
    const uint8_t* yRow = yPlane + (size_t)j * yStride;
    const uint8_t* uvRow = uvPlane + (size_t)(j / 2) * uvStride;
    uint8_t* dstRow = dst + (size_t)j * w * 4;
    for (int32_t i = 0; i < w; ++i) {
      const int y = expandY(yRow[i], limited);
      const int uvIdx = (i & ~1);
      const int u = expandC(uvRow[uvIdx], limited);
      const int v = expandC(uvRow[uvIdx + 1], limited);
      const int r = y + ((v * rCof) >> 10);
      const int g = y - ((u * guCof + v * gvCof) >> 10);
      const int b = y + ((u * bCof) >> 10);
      dstRow[i * 4] = toByte(b);
      dstRow[i * 4 + 1] = toByte(g);
      dstRow[i * 4 + 2] = toByte(r);
      dstRow[i * 4 + 3] = 255;
    }
  }
}

void PlayerBridge::ConvertYuv420P(const avox::YUVFrame& frame, uint8_t* dst,
                                  const avox::ColorSpaceDesc& cs) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  const uint8_t* yPlane = frame.data[0];
  const uint8_t* uPlane = frame.data[1];
  const uint8_t* vPlane = frame.data[2];
  const int32_t yStride = frame.stride[0];
  const int32_t uvStride = frame.stride[1] > 0 ? frame.stride[1] : w / 2;
  int rCof;
  int guCof;
  int gvCof;
  int bCof;
  pickMatrix(cs.standard, rCof, guCof, gvCof, bCof);
  const bool limited = cs.range == avox::YuvRange::limited;
  auto toByte = [](int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  for (int32_t j = 0; j < h; ++j) {
    const uint8_t* yRow = yPlane + (size_t)j * yStride;
    const uint8_t* uRow = uPlane + (size_t)(j / 2) * uvStride;
    const uint8_t* vRow = vPlane + (size_t)(j / 2) * uvStride;
    uint8_t* dstRow = dst + (size_t)j * w * 4;
    for (int32_t i = 0; i < w; ++i) {
      const int y = expandY(yRow[i], limited);
      const int u = expandC(uRow[i / 2], limited);
      const int v = expandC(vRow[i / 2], limited);
      const int r = y + ((v * rCof) >> 10);
      const int g = y - ((u * guCof + v * gvCof) >> 10);
      const int b = y + ((u * bCof) >> 10);
      dstRow[i * 4] = toByte(b);
      dstRow[i * 4 + 1] = toByte(g);
      dstRow[i * 4 + 2] = toByte(r);
      dstRow[i * 4 + 3] = 255;
    }
  }
}
