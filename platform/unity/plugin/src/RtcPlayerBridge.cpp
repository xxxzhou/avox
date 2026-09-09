#include "RtcPlayerBridge.h"
#include "PlayerBridge.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>

// ── RTC 桥注册表 (id → bridge, 纹理更新回调第三级查找) ──
static std::mutex g_rtcRegMutex;
static std::map<uint32_t, RtcPlayerBridge*> g_rtcRegistry;

RtcPlayerBridge* findRtcBridge(uint32_t id) {
  std::lock_guard<std::mutex> lock(g_rtcRegMutex);
  auto it = g_rtcRegistry.find(id);
  return it != g_rtcRegistry.end() ? it->second : nullptr;
}

RtcPlayerBridge::RtcPlayerBridge(uint32_t id) : playerId(id) {
  std::lock_guard<std::mutex> lock(g_rtcRegMutex);
  g_rtcRegistry[playerId] = this;
}

RtcPlayerBridge::~RtcPlayerBridge() {
  destroyPlayer();
  std::lock_guard<std::mutex> lock(g_rtcRegMutex);
  g_rtcRegistry.erase(playerId);
}

// ── 配置 (播放器未建时缓存, createPlayer 统一应用; 已建则立即可用) ──

void RtcPlayerBridge::setRollType(int32_t type) {
  rollType = type;
  if (player) player->setRollType(static_cast<avox::RtcRollType>(type));
}

void RtcPlayerBridge::setVideoDirection(int32_t dir) {
  videoDir = dir;
  if (player) player->setVideoDirection(static_cast<avox::RtpDirection>(dir));
}

void RtcPlayerBridge::setAudioDirection(int32_t dir) {
  audioDir = dir;
  if (player) player->setAudioDirection(static_cast<avox::RtpDirection>(dir));
}

void RtcPlayerBridge::addIceServer(const char* url, const char* user,
                                   const char* pwd) {
  if (!url) return;
  iceServers.push_back({url, user ? user : "", pwd ? pwd : ""});
  if (player) player->addIceServer(url, user, pwd);
}

void RtcPlayerBridge::setAutoReconnect(bool bEnable, int32_t retries) {
  bAutoReconnect = bEnable;
  maxReconnectRetries = retries;
  if (player) player->setAutoReconnect(bEnable, retries);
}

void RtcPlayerBridge::setSendVideoBitrate(int32_t kbps) {
  maxVideoBitrateKbps = kbps;
  if (player) player->setSendVideoBitrate(kbps);
}

void RtcPlayerBridge::setSendVideoFps(int32_t fps) {
  maxVideoFps = fps;
  if (player) player->setSendVideoFps(fps);
}

void RtcPlayerBridge::setPreferredVideoCodec(const char* codec) {
  preferredCodec = codec ? codec : "";
  if (player) player->setPreferredVideoCodec(preferredCodec.c_str());
}

void RtcPlayerBridge::setEnableDataChannel(bool bEnable) {
  bEnableDataChannel = bEnable;
  if (player) player->setEnableDataChannel(bEnable);
}

void RtcPlayerBridge::setVolume(float v) {
  volume = v;
  if (player) {
    if (auto* audio = player->getRemoteAudioRender()) audio->setVolume(v);
  }
}

// ── 连接控制 ──

void RtcPlayerBridge::createPlayer() {
  if (player) return;
  player = avox::createWebRtcPlayer();
  if (!player) {
    pushError((int32_t)avox::AVError::urlNoSupport,
              "createWebRtcPlayer failed: avox_webrtc plugin not deployed");
    return;
  }
  // 本类是 IRtcPlayerOb (状态+rtc 两路回调), addRtcPlayerOb 自动双注册
  avox::addRtcPlayerOb(player, this);
  // 配置须在 open 前
  player->setRollType(static_cast<avox::RtcRollType>(rollType));
  player->setVideoDirection(static_cast<avox::RtpDirection>(videoDir));
  player->setAudioDirection(static_cast<avox::RtpDirection>(audioDir));
  for (const RtcIceServerCfg& server : iceServers) {
    player->addIceServer(server.url.c_str(),
                         server.user.empty() ? nullptr : server.user.c_str(),
                         server.pwd.empty() ? nullptr : server.pwd.c_str());
  }
  if (maxVideoBitrateKbps > 0) player->setSendVideoBitrate(maxVideoBitrateKbps);
  if (maxVideoFps > 0) player->setSendVideoFps(maxVideoFps);
  if (!preferredCodec.empty()) player->setPreferredVideoCodec(preferredCodec.c_str());
  if (bEnableDataChannel) player->setEnableDataChannel(true);
  player->setAutoReconnect(bAutoReconnect, maxReconnectRetries);
  bindSurface();
}

void RtcPlayerBridge::destroyPlayer() {
  if (!player) return;
  // 先解绑纹理桥接与信令观察者, 再关播放器 (解绑需要 surface render 仍有效)
  unbindSurface();
  if (sdpAgent) {
    player->removeOb(sdpAgent);
    delete sdpAgent;
    sdpAgent = nullptr;
  }
  player->close();
  avox::removeRtcPlayerOb(player, this);
  // createWebRtcPlayer() 是裸 new, delete 是唯一释放路径 (基类析构 virtual)
  delete player;
  player = nullptr;
  stateCache.store(0);
}

void RtcPlayerBridge::bindSurface() {
  surface = player->getRemoteSurfaceRender();
  if (!surface) return;
  // 恒 CPU 路径 (GPU 直通二期): 离屏渲染 + YUV 输出, 渲染线程 onFrame 转 BGRA
  // (同 PlayerBridge CPU 回退: 保留 avox Vulkan 离屏管线, 需部署 assets/glsl)
  surface->setOffSurface(avox::YuvType::nv12);
  surface->enableYuvOut(avox::YuvType::nv12);
  avox::addSurfaceRenderOb(surface, this);
}

void RtcPlayerBridge::unbindSurface() {
  if (!surface) return;
  avox::removeSurfaceRenderOb(surface, this);
  surface = nullptr;
}

bool RtcPlayerBridge::connectSignaling(const char* url) {
  destroyPlayer();
  createPlayer();
  if (!player || !url) return false;
  // 内置信令观察者(ZLM/WHEP): onLocalSdp自动POST, 远端answer自动回填
  sdpAgent = createZlTestSdpAgent(player, url);
  if (!sdpAgent) {
    pushError((int32_t)avox::AVError::other, "createZlTestSdpAgent failed");
    return false;
  }
  player->addOb(sdpAgent);
  player->open();
  return true;
}

void RtcPlayerBridge::openRtc() {
  destroyPlayer();
  createPlayer();
  if (!player) return;
  // 自定义信令: 本地 SDP/ICE 经 IRtcEventOb::onLocalSdp/onIceCandidate → Unity 事件,
  // 远端消息 setRemoteSdp/addIceCandidate 回填
  player->open();
}

void RtcPlayerBridge::close() { destroyPlayer(); }

void RtcPlayerBridge::reconnect() {
  if (!player) return;
  // 已有实例: avox 内部重建 PeerConnection 并重新走信令
  player->reconnect();
}

// ── 信令回填 ──

void RtcPlayerBridge::setRemoteSdp(const char* sdp) {
  if (!player || !sdp) return;
  player->setRemoteSdp(sdp);
}

void RtcPlayerBridge::addIceCandidate(const char* candidate, const char* mid,
                                      int32_t mline) {
  if (!player || !candidate) return;
  player->addIceCandidate(candidate, mid, mline);
}

// ── DataChannel ──

bool RtcPlayerBridge::sendDataChannel(const uint8_t* data, int32_t size) {
  if (!player || !data || size <= 0) return false;
  return player->sendDataChannel((const char*)data, size);
}

// ── 查询 ──

int32_t RtcPlayerBridge::connectionState() const {
  return player ? (int32_t)player->getConnectionState()
                : (int32_t)avox::RtcConnState::closed;
}

double RtcPlayerBridge::fps() const { return player ? player->getFps() : 0.0; }

float RtcPlayerBridge::lossRate() const {
  return player ? player->getLossRate() : 0.0f;
}

int32_t RtcPlayerBridge::rttMs() const {
  return player ? player->getRttMs() : -1;
}

bool RtcPlayerBridge::pollEvent(AvoxRtcEvent* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lock(eventMutex);
  if (events.empty()) return false;
  *out = events.front();
  events.pop_front();
  return true;
}

int32_t RtcPlayerBridge::localSdp(char* buf, int32_t bufSize) {
  if (!buf || bufSize <= 0) return -1;
  std::lock_guard<std::mutex> lock(sdpMutex);
  if (localSdpStr.empty()) return -1;
  snprintf(buf, (size_t)bufSize, "%s", localSdpStr.c_str());
  return (int32_t)localSdpStr.size();
}

int32_t RtcPlayerBridge::pollDataChannel(uint8_t* buf, int32_t bufSize) {
  if (!buf || bufSize <= 0) return 0;
  std::lock_guard<std::mutex> lock(dcMutex);
  if (dcQueue.empty()) return 0;
  std::vector<uint8_t> msg = std::move(dcQueue.front());
  dcQueue.pop_front();
  const int32_t n = (int32_t)msg.size();
  memcpy(buf, msg.data(), (size_t)(n <= bufSize ? n : bufSize));
  return n;
}

bool RtcPlayerBridge::frameInfo(int32_t* w, int32_t* h) {
  std::lock_guard<std::mutex> lock(frameMutex);
  if (frameW <= 0 || frameH <= 0) return false;
  if (w) *w = frameW;
  if (h) *h = frameH;
  return true;
}

// ── avox::IRtcPlayerOb (avox 线程) ──

void RtcPlayerBridge::onStateChange(avox::PlayerState preState,
                                    avox::PlayerState state) {
  (void)preState;
  stateCache.store((int32_t)state);
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::state;
  e.state = (int32_t)state;
  pushEvent(e);
}

void RtcPlayerBridge::onReady() {
  // onReady 时远端 sourceInfo 一定有效, 取尺寸与色彩空间 (CPU 转换矩阵共用)
  avox::ISourceInfo* info = player ? player->getRemoteSourceInfo() : nullptr;
  if (info && info->videoSize() > 0) {
    const avox::VTrackDesc vd = info->getVideoDesc(0);
    if (!colorSpaceSet || vd.desc.colorSpace.standard != colorSpace.standard ||
        vd.desc.colorSpace.range != colorSpace.range) {
      colorSpace = vd.desc.colorSpace;
      colorSpaceSet = true;
      if (surface) surface->setColorSpace(colorSpace);
    }
  }
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::ready;
  pushEvent(e);
}

void RtcPlayerBridge::onIoError(avox::AVError error, const char* msg) {
  pushError((int32_t)error, msg);
}

void RtcPlayerBridge::onDecodeError(avox::TrackType trackType,
                                    avox::DecodeResult error) {
  (void)trackType;
  pushError((int32_t)error, avox::getDecodeResultStr(error));
}

void RtcPlayerBridge::onConnectionState(avox::RtcConnState state) {
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::connState;
  e.state = (int32_t)state;
  pushEvent(e);
}

void RtcPlayerBridge::onFirstVideoFrame() {
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::firstFrame;
  pushEvent(e);
}

void RtcPlayerBridge::onDataChannelMsg(const char* data, int32_t size) {
  if (!data || size <= 0) return;
  std::lock_guard<std::mutex> lock(dcMutex);
  dcQueue.emplace_back(data, data + size);
}

// ── avox::IRtcEventOb: 本地SDP/ICE → 事件 (信令线程回调) ──

void RtcPlayerBridge::onLocalSdp(const char* sdp) {
  {
    std::lock_guard<std::mutex> lock(sdpMutex);
    localSdpStr = sdp ? sdp : "";
  }
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::localSdp;
  pushEvent(e);
}

void RtcPlayerBridge::onIceCandidate(const char* candidate, const char* mid,
                                     int mlineIndex) {
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::iceCandidate;
  e.code = mlineIndex;
  snprintf(e.msg, sizeof(e.msg), "%s", candidate ? candidate : "");
  snprintf(e.mid, sizeof(e.mid), "%s", mid ? mid : "");
  pushEvent(e);
}

// ── avox::ISurfaceRenderOb (avox 渲染线程) ──

// 只重排成紧凑 NV12, 色转交给 Unity 侧 shader (打包器复用 PlayerBridge 静态实现)
void RtcPlayerBridge::onFrame(const avox::YUVFrame& frame) {
  const int32_t w = frame.format.width;
  const int32_t h = frame.format.height;
  if (w <= 0 || h <= 0 || (w & 1) || (h & 1)) return;
  std::vector<uint8_t> nv12;
  nv12.resize((size_t)w * h * 3 / 2);
  if (!PlayerBridge::PackNv12(frame, nv12.data())) return;
  {
    std::lock_guard<std::mutex> lock(frameMutex);
    frameNv12 = std::move(nv12);
    frameW = w;
    frameH = h;
  }
}

int32_t RtcPlayerBridge::colorSpaceCode() const {
  if (!colorSpaceSet) return -1;
  return (int32_t)colorSpace.standard | ((int32_t)colorSpace.range << 8);
}

void RtcPlayerBridge::onWinSizeChange(int32_t width, int32_t height) {
  (void)width;
  (void)height;
}

// ── 内部 ──

// R8 的 w × h*3/2 (整帧 NV12); 无帧填中性黑 Y=16/UV=128 (同 PlayerBridge)
bool RtcPlayerBridge::allocCpuFrame(uint32_t w, uint32_t h, uint32_t bpp,
                                    void** texData) {
  if (!texData || w == 0 || h == 0 || bpp != 1) return false;
  const size_t bytes = (size_t)w * h;
  uint8_t* buf = (uint8_t*)malloc(bytes);
  if (!buf) return false;
  bool hasFrame = false;
  {
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameNv12.empty() && frameW == (int32_t)w &&
        (int32_t)h == frameH * 3 / 2 && frameNv12.size() == bytes) {
      memcpy(buf, frameNv12.data(), bytes);
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

void RtcPlayerBridge::pushEvent(const AvoxRtcEvent& e) {
  std::lock_guard<std::mutex> lock(eventMutex);
  events.push_back(e);
}

void RtcPlayerBridge::pushError(int32_t code, const char* msg) {
  AvoxRtcEvent e;
  e.type = (int32_t)AvoxRtcEvent::EType::error;
  e.code = code;
  snprintf(e.msg, sizeof(e.msg), "%s", msg ? msg : "");
  pushEvent(e);
}
