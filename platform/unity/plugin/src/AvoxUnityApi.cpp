#include "AvoxUnityApi.h"
#include "PlayerBridge.h"
#include "GpuPassthrough.h"

AVOX_UNITY_API void* avoxGetTextureUpdateCallback(void) {
  return (void*)&avoxTextureUpdateCallback;
}

AVOX_UNITY_API int32_t avoxGetGpuPassthroughAvailable(void) {
  return unityGpuPassthroughAvailable() ? 1 : 0;
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
  return player ? ((PlayerBridge*)player)->gpuImage() : 0;
}

AVOX_UNITY_API void avoxPlayerUpdateGpu(avox_player_t player) {
  if (player) ((PlayerBridge*)player)->updateGpu();
}
