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
  // 无捕获 lambda -> C 函数指针 (跨 DLL 退出安全: AvoxManager 析构晚于 plugin 卸载不调代码)
  AvoxManager::Get().audioFaceHub.reg(
      "wav2arkit", []() -> AudioFace* { return new Wav2ArkitFace(); });
  // 视频->blendshape+landmarks 工厂 (mediapipe 后端); 业务走 videoFaceHub.create("mediapipe") 查表。
  // 5 步管线 (BlazeFace 检测 -> 眼角对齐 warp -> landmarker -> blendshape -> 反投) 见 MediaPipeFace,
  // 数学移植自 face_pipeline.py (对照官方 mediapipe 已校准)。模型同样经 OnnxSessionCache 运行期加载。
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
