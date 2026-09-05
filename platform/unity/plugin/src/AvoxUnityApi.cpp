#include "AvoxUnityApi.h"
#include "PlayerBridge.h"
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

AVOX_UNITY_API int32_t avoxPlayerIsGpuMode(avox_player_t player) {
  return player ? (((PlayerBridge*)player)->gpuMode() ? 1 : 0) : 0;
}

AVOX_UNITY_API uint64_t avoxPlayerGetExternalTexture(avox_player_t player) {
  if (!player) return 0;
  auto* bridge = (PlayerBridge*)player;
  // D3D11 拷贝模式: 返回插件自建的目标纹理 (格式一致, CreateExternalTexture 包裹)
  if (unityGpuImportFlavor() == 2) return bridge->dx11NativeTex();
  return bridge->gpuImage();
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
