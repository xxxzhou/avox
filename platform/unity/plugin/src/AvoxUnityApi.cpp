#include "AvoxUnityApi.h"
#include "PlayerBridge.h"
#include "SourceBridge.h"
#ifdef _WIN32
#include "RtcPlayerBridge.h"
#endif
#include "GpuPassthrough.h"

AVOX_UNITY_API void* avoxGetTextureUpdateCallback(void) {
  return (void*)&avoxTextureUpdateCallback;
}

// D3D11 拷贝模式渲染事件 (C# GL.IssuePluginEvent 触发, eventId = player id)
AVOX_UNITY_API void* avoxGetRenderEventFunc(void) {
  return (void*)&avoxDx11RenderEvent;
}

// 设置拷贝目标: C# 纹理 GetNativeTexturePtr (普通 Texture2D, BGRA32)
AVOX_UNITY_API void avoxPlayerSetDx11Target(avox_player_t player, void* nativeTex) {
  if (player) ((PlayerBridge*)player)->setDx11Target(nativeTex);
}

// 拷贝链路诊断: 渲染事件数/实际拷贝数/目标缺失数/最近 fence 值/共享纹理打开次数
AVOX_UNITY_API void avoxPlayerGetDx11Debug(avox_player_t player, int32_t* events,
                                           int32_t* copies, int32_t* targetNull,
                                           int64_t* fenceVal, int32_t* opens) {
  if (!player) return;
  uint32_t e = 0, c = 0, t = 0, o = 0;
  uint64_t f = 0;
  ((PlayerBridge*)player)->dx11Debug(&e, &c, &t, &f, &o);
  if (events) *events = (int32_t)e;
  if (copies) *copies = (int32_t)c;
  if (targetNull) *targetNull = (int32_t)t;
  if (fenceVal) *fenceVal = (int64_t)f;
  if (opens) *opens = (int32_t)o;
}

AVOX_UNITY_API int32_t avoxGetGpuPassthroughAvailable(void) {
  return unityGpuPassthroughAvailable() ? 1 : 0;
}

// 0 无 1 Vulkan 导入 2 D3D11 拷贝
AVOX_UNITY_API int32_t avoxGetGpuFlavor(void) {
  return unityGpuImportFlavor();
}

AVOX_UNITY_API const char* avoxGetVersion(void) {
  return AVOX_COMMIT_VERSION;
}

AVOX_UNITY_API avox_player_t avoxPlayerCreate(void) {
  static std::atomic<uint32_t> nextId{1};
  return new PlayerBridge(nextId.fetch_add(1));
}

AVOX_UNITY_API void avoxPlayerDestroy(avox_player_t player) {
  delete (PlayerBridge*)player;
}

AVOX_UNITY_API uint32_t avoxPlayerGetId(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->id() : 0;
}

AVOX_UNITY_API void avoxPlayerSetHardDecode(avox_player_t player, int32_t bEnable) {
  if (player) ((PlayerBridge*)player)->setHardDecode(bEnable != 0);
}

AVOX_UNITY_API void avoxPlayerSetVolume(avox_player_t player, float volume) {
  if (player) ((PlayerBridge*)player)->setVolume(volume);
}

AVOX_UNITY_API void avoxPlayerSetIoPlan(avox_player_t player, int32_t plan) {
  if (player) ((PlayerBridge*)player)->setIoPlan(plan);
}

AVOX_UNITY_API void avoxPlayerSetSpeed(avox_player_t player, double speed) {
  if (player) ((PlayerBridge*)player)->setSpeed(speed);
}

AVOX_UNITY_API void avoxPlayerOpen(avox_player_t player, const char* url) {
  if (player) ((PlayerBridge*)player)->open(url);
}

AVOX_UNITY_API void avoxPlayerClose(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->close();
}

AVOX_UNITY_API void avoxPlayerPause(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->pause();
}

AVOX_UNITY_API void avoxPlayerResume(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->resume();
}

AVOX_UNITY_API void avoxPlayerSeek(avox_player_t player, int64_t pos) {
  if (player) ((PlayerBridge*)player)->seek(pos);
}

AVOX_UNITY_API int32_t avoxPlayerGetState(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->state() : 0;
}

AVOX_UNITY_API int64_t avoxPlayerGetDuration(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->duration() : 0;
}

AVOX_UNITY_API int64_t avoxPlayerGetPosition(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->position() : 0;
}

AVOX_UNITY_API double avoxPlayerGetProgress(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->progress() : 0.0;
}

AVOX_UNITY_API int32_t avoxPlayerPollEvent(avox_player_t player, void* out) {
  if (!player || !out) return 0;
  return ((PlayerBridge*)player)->pollEvent((AvoxUnityEvent*)out) ? 1 : 0;
}

AVOX_UNITY_API int32_t avoxPlayerGetFrameInfo(avox_player_t player, int32_t* w, int32_t* h) {
  if (!player) return 0;
  return ((PlayerBridge*)player)->frameInfo(w, h) ? 1 : 0;
}

// 源色彩空间: standard(0 bt601 1 bt709 2 bt2020) | range(0 full 1 limited)<<8,
// 未就绪返回 -1。CPU 回退路径的 YUV shader 用它构解码矩阵
AVOX_UNITY_API int32_t avoxPlayerGetColorSpace(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->colorSpaceCode() : -1;
}

AVOX_UNITY_API int32_t avoxPlayerIsGpuMode(avox_player_t player) {
  return player ? (((PlayerBridge*)player)->gpuMode() ? 1 : 0) : 0;
}

AVOX_UNITY_API uint64_t avoxPlayerGetExternalTexture(avox_player_t player) {
  if (!player) return 0;
  auto* bridge = (PlayerBridge*)player;
  // D3D11 拷贝模式: 返回插件自建的目标纹理 (格式一致, CreateExternalTexture 包裹);
  // D3D12 拷贝模式不走外部纹理 (C# 普通纹理 + 每帧拷贝)
  if (unityGpuImportFlavor() == 2) return bridge->dx11NativeTex();
  return bridge->gpuImage();
}

AVOX_UNITY_API void avoxPlayerSetDx12Target(avox_player_t player, void* nativeTex) {
  if (player) ((PlayerBridge*)player)->setDx12Target(nativeTex);
}

AVOX_UNITY_API void avoxPlayerGetDx12Debug(avox_player_t player, int32_t* noTarget,
                                           int32_t* noCl, int32_t* copies, int32_t* opens) {
  if (!player) return;
  ((PlayerBridge*)player)->dx12Debug((uint32_t*)noTarget, (uint32_t*)noCl,
                                     (uint32_t*)copies, (uint32_t*)opens);
}

AVOX_UNITY_API void avoxDx11DumpSharedById(uint32_t playerId, const char* path) {
#ifdef _WIN32
  unityDx11DumpShared(nullptr, playerId, path);
#else
  (void)playerId; (void)path;
#endif
}

AVOX_UNITY_API void avoxPlayerUpdateGpu(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->updateGpu();
}

// ── Option ──

AVOX_UNITY_API int32_t avoxPlayerGetOptionType(avox_player_t player, const char* key) {
  return player ? ((PlayerBridge*)player)->optionType(key) : -1;
}

AVOX_UNITY_API int32_t avoxPlayerSetOptionBool(avox_player_t player, const char* key, int32_t value) {
  return player ? (((PlayerBridge*)player)->setOptionBool(key, value != 0) ? 1 : 0) : 0;
}

AVOX_UNITY_API int32_t avoxPlayerSetOptionInt(avox_player_t player, const char* key, int64_t value) {
  return player ? (((PlayerBridge*)player)->setOptionInt(key, value) ? 1 : 0) : 0;
}

AVOX_UNITY_API int32_t avoxPlayerSetOptionNumber(avox_player_t player, const char* key, double value) {
  return player ? (((PlayerBridge*)player)->setOptionNumber(key, value) ? 1 : 0) : 0;
}

AVOX_UNITY_API int32_t avoxPlayerSetOptionString(avox_player_t player, const char* key, const char* value) {
  return player ? (((PlayerBridge*)player)->setOptionString(key, value) ? 1 : 0) : 0;
}

AVOX_UNITY_API int64_t avoxPlayerGetOptionInt(avox_player_t player, const char* key) {
  return player ? ((PlayerBridge*)player)->optionInt(key) : 0;
}

AVOX_UNITY_API double avoxPlayerGetOptionNumber(avox_player_t player, const char* key) {
  return player ? ((PlayerBridge*)player)->optionNumber(key) : 0.0;
}

AVOX_UNITY_API int32_t avoxPlayerGetOptionString(avox_player_t player, const char* key, char* buf, int32_t bufSize) {
  return player ? ((PlayerBridge*)player)->optionString(key, buf, bufSize) : -1;
}

// ── 录制 ──

AVOX_UNITY_API int32_t avoxPlayerStartRecord(avox_player_t player, const char* path, int32_t bTranscode) {
  return player ? (((PlayerBridge*)player)->startRecord(path, bTranscode != 0) ? 1 : 0) : 0;
}

AVOX_UNITY_API void avoxPlayerStopRecord(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->stopRecord();
}

AVOX_UNITY_API int32_t avoxPlayerGetRecordState(avox_player_t player) {
  return player ? ((PlayerBridge*)player)->recordState() : 0;
}

// ── 字幕 ──

AVOX_UNITY_API int32_t avoxPlayerLoadSrt(avox_player_t player, const char* path) {
  return player ? (((PlayerBridge*)player)->loadSrt(path) ? 1 : 0) : 0;
}

AVOX_UNITY_API void avoxPlayerCloseSubtitle(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->closeSubtitle();
}

// ── 设备源 (相机/采集卡) ──

AVOX_UNITY_API avox_source_t avoxSourceCreate(void) {
  static std::atomic<uint32_t> nextSrcId{5001};
  return new SourceBridge(nextSrcId.fetch_add(1));
}

AVOX_UNITY_API void avoxSourceDestroy(avox_source_t source) {
  delete (SourceBridge*)source;
}

AVOX_UNITY_API uint32_t avoxSourceGetId(avox_source_t source) {
  return source ? ((SourceBridge*)source)->id() : 0;
}

AVOX_UNITY_API int32_t avoxVideoDeviceCount(void) {
  auto* mgr = avox::getVideoManager(avox::getDefaltVideoSdk());
  if (!mgr) return 0;
  mgr->refreshDevices();
  return mgr->getDeviceCount();
}

AVOX_UNITY_API int32_t avoxVideoDeviceName(int32_t index, char* buf, int32_t bufSize) {
  auto* mgr = avox::getVideoManager(avox::getDefaltVideoSdk());
  if (!mgr || index < 0 || index >= mgr->getDeviceCount()) return -1;
  auto* dev = mgr->getDevice(index);
  if (!dev || !dev->getDeviceName()) return -1;
  const char* name = dev->getDeviceName();
  int32_t len = (int32_t)strlen(name);
  if (buf && bufSize > 0) {
    int32_t copy = len < bufSize - 1 ? len : bufSize - 1;
    memcpy(buf, name, copy);
    buf[copy] = 0;
  }
  return len + 1;
}

AVOX_UNITY_API void avoxSourceSetDevice(avox_source_t source, int32_t index) {
  if (source) ((SourceBridge*)source)->setDeviceIndex(index);
}

AVOX_UNITY_API int32_t avoxSourceOpen(avox_source_t source) {
  return source && ((SourceBridge*)source)->open() ? 1 : 0;
}

AVOX_UNITY_API void avoxSourceClose(avox_source_t source) {
  if (source) ((SourceBridge*)source)->close();
}

AVOX_UNITY_API int32_t avoxSourceGetState(avox_source_t source) {
  return source ? ((SourceBridge*)source)->state() : 0;
}

AVOX_UNITY_API int32_t avoxSourceGetColorSpace(avox_source_t source) {
  return source ? ((SourceBridge*)source)->colorSpaceCode() : -1;
}

AVOX_UNITY_API int32_t avoxSourceGetFrameInfo(avox_source_t source, int32_t* w, int32_t* h) {
  if (!source) return 0;
  return ((SourceBridge*)source)->frameInfo(w, h) ? 1 : 0;
}

#ifdef _WIN32
// ── WebRTC 推拉流 (RtcPlayerBridge, 对应 UE AvoxRtcPlayerComponent / godot RtcPlayer) ──
// Android 不编译 (核心 WEBRTC=OFF); C# DllImport 惰性解析, 不调用不报错

AVOX_UNITY_API avox_player_t avoxRtcCreate(void) {
  // id段错开(player1+/source5001+/rtc10001+): 三级查找共用id空间, 重号会抢答纹理回调
  static std::atomic<uint32_t> nextRtcId{10001};
  return new RtcPlayerBridge(nextRtcId.fetch_add(1));
}

AVOX_UNITY_API void avoxRtcDestroy(avox_player_t player) {
  delete (RtcPlayerBridge*)player;
}

AVOX_UNITY_API uint32_t avoxRtcGetId(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->id() : 0;
}

// ── 配置 (open 前调用) ──

AVOX_UNITY_API void avoxRtcSetRollType(avox_player_t player, int32_t type) {
  if (player) ((RtcPlayerBridge*)player)->setRollType(type);
}

AVOX_UNITY_API void avoxRtcSetVideoDirection(avox_player_t player, int32_t dir) {
  if (player) ((RtcPlayerBridge*)player)->setVideoDirection(dir);
}

AVOX_UNITY_API void avoxRtcSetAudioDirection(avox_player_t player, int32_t dir) {
  if (player) ((RtcPlayerBridge*)player)->setAudioDirection(dir);
}

AVOX_UNITY_API void avoxRtcAddIceServer(avox_player_t player, const char* url,
                                        const char* user, const char* pwd) {
  if (player) ((RtcPlayerBridge*)player)->addIceServer(url, user, pwd);
}

AVOX_UNITY_API void avoxRtcSetAutoReconnect(avox_player_t player, int32_t bEnable,
                                            int32_t retries) {
  if (player) ((RtcPlayerBridge*)player)->setAutoReconnect(bEnable != 0, retries);
}

AVOX_UNITY_API void avoxRtcSetSendVideoBitrate(avox_player_t player, int32_t kbps) {
  if (player) ((RtcPlayerBridge*)player)->setSendVideoBitrate(kbps);
}

AVOX_UNITY_API void avoxRtcSetSendVideoFps(avox_player_t player, int32_t fps) {
  if (player) ((RtcPlayerBridge*)player)->setSendVideoFps(fps);
}

AVOX_UNITY_API void avoxRtcSetPreferredVideoCodec(avox_player_t player, const char* codec) {
  if (player) ((RtcPlayerBridge*)player)->setPreferredVideoCodec(codec);
}

AVOX_UNITY_API void avoxRtcSetEnableDataChannel(avox_player_t player, int32_t bEnable) {
  if (player) ((RtcPlayerBridge*)player)->setEnableDataChannel(bEnable != 0);
}

AVOX_UNITY_API void avoxRtcSetVolume(avox_player_t player, float volume) {
  if (player) ((RtcPlayerBridge*)player)->setVolume(volume);
}

// ── 连接控制 ──

// HTTP 信令一键连接 (ZLM/WHEP, Offer 拉流主路径)
AVOX_UNITY_API int32_t avoxRtcConnect(avox_player_t player, const char* url) {
  return player && ((RtcPlayerBridge*)player)->connectSignaling(url) ? 1 : 0;
}

// 自定义信令模式: 先监听 localSdp/iceCandidate 事件再调
AVOX_UNITY_API void avoxRtcOpen(avox_player_t player) {
  if (player) ((RtcPlayerBridge*)player)->openRtc();
}

AVOX_UNITY_API void avoxRtcClose(avox_player_t player) {
  if (player) ((RtcPlayerBridge*)player)->close();
}

AVOX_UNITY_API void avoxRtcReconnect(avox_player_t player) {
  if (player) ((RtcPlayerBridge*)player)->reconnect();
}

// ── 信令回填 (自定义信令) ──

AVOX_UNITY_API void avoxRtcSetRemoteSdp(avox_player_t player, const char* sdp) {
  if (player) ((RtcPlayerBridge*)player)->setRemoteSdp(sdp);
}

AVOX_UNITY_API void avoxRtcAddIceCandidate(avox_player_t player, const char* candidate,
                                           const char* mid, int32_t mline) {
  if (player) ((RtcPlayerBridge*)player)->addIceCandidate(candidate, mid, mline);
}

// ── DataChannel ──

AVOX_UNITY_API int32_t avoxRtcSendDataChannel(avox_player_t player, const uint8_t* data,
                                              int32_t size) {
  return player && ((RtcPlayerBridge*)player)->sendDataChannel(data, size) ? 1 : 0;
}

// 弹出一条 DataChannel 消息, 返回字节数 (0=无)
AVOX_UNITY_API int32_t avoxRtcPollDataChannel(avox_player_t player, uint8_t* buf,
                                              int32_t bufSize) {
  return player ? ((RtcPlayerBridge*)player)->pollDataChannel(buf, bufSize) : 0;
}

// ── 查询 ──

AVOX_UNITY_API int32_t avoxRtcGetState(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->state() : 0;
}

AVOX_UNITY_API int32_t avoxRtcGetConnectionState(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->connectionState() : 5;
}

AVOX_UNITY_API double avoxRtcGetFps(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->fps() : 0.0;
}

AVOX_UNITY_API float avoxRtcGetLossRate(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->lossRate() : 0.0f;
}

AVOX_UNITY_API int32_t avoxRtcGetRttMs(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->rttMs() : -1;
}

AVOX_UNITY_API int32_t avoxRtcPollEvent(avox_player_t player, void* out) {
  if (!player || !out) return 0;
  return ((RtcPlayerBridge*)player)->pollEvent((AvoxRtcEvent*)out) ? 1 : 0;
}

// 本地 SDP (localSdp 事件后拉取, 返回长度; -1=无)
AVOX_UNITY_API int32_t avoxRtcGetLocalSdp(avox_player_t player, char* buf, int32_t bufSize) {
  return player ? ((RtcPlayerBridge*)player)->localSdp(buf, bufSize) : -1;
}

AVOX_UNITY_API int32_t avoxRtcGetColorSpace(avox_player_t player) {
  return player ? ((RtcPlayerBridge*)player)->colorSpaceCode() : -1;
}

AVOX_UNITY_API int32_t avoxRtcGetFrameInfo(avox_player_t player, int32_t* w, int32_t* h) {
  if (!player) return 0;
  return ((RtcPlayerBridge*)player)->frameInfo(w, h) ? 1 : 0;
}

AVOX_UNITY_API int32_t avoxRtcIsGpuMode(avox_player_t player) {
  return player ? (((RtcPlayerBridge*)player)->gpuMode() ? 1 : 0) : 0;
}
#endif  // _WIN32 (WebRTC 导出段)
