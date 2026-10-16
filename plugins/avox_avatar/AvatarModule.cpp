#include "AvatarModule.hpp"

#include "MediaPipeFace.hpp"
#include "MediaPipeBody.hpp"
#include "Wav2ArkitFace.hpp"
#include "avox/audio/AudioFace.hpp"
#include "avox/avatar/VideoFace.hpp"
#include "avox/avatar/BodyImpl.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool AvatarModule::loadModule(IOption* option) {
  (void)option;
// 音频->blendshape 工厂 (wav2arkit 后端); 业务走 audioFaceHub.create("wav2arkit") 查表。
// 模型经 OnnxSessionCache 在 Wav2ArkitFace 运行期加载, 缺失/失败由调用方降级 (回退 RMS)。
  AvoxManager::Get().audioFaceHub.reg(
      "wav2arkit", []() -> AudioFace* { return new Wav2ArkitFace(); });
// 视频->blendshape+landmarks 工厂 (mediapipe 后端); 业务走 videoFaceHub.create("mediapipe") 查表。
// 5 步管线 (BlazeFace 检测 -> 眼角对齐 warp -> landmarker -> blendshape -> 反投) 见 MediaPipeFace,
  AvoxManager::Get().videoFaceHub.reg(
      "mediapipe", []() -> VideoFace* { return new MediaPipeFace(); });
  // 视频->身体姿态 33 点工厂 (mediapipe_body 后端); 业务走 bodyHub.create("mediapipe_body") 查表。
  // BlazePose 检测(2254锚) -> 人体对齐 warp -> pose_landmarker 33 点, 驱动 avatar 全身骨骼/手势。
  AvoxManager::Get().bodyHub.reg(
      "mediapipe_body", []() -> BodyImpl* { return new MediaPipeBody(); });
  return true;
}

AVOX_REGISTER_MODULE(AvatarModule, avox_avatar)

}
