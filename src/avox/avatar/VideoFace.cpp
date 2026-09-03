#include "VideoFace.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/ModuleMgr.hpp"

namespace avox {

void VideoFace::setModelLevel(ModelLevel level) { modelLevel = level; }

void addVideoFaceOb(IVideoFace* face, IVideoFaceOb* ob) {
  if (face) {
    auto* videoFace = dynamic_cast<VideoFace*>(face);
    if (videoFace) {
      videoFace->addObserver(ob);
    }
  }
}

void removeVideoFaceOb(IVideoFace* face, IVideoFaceOb* ob) {
  if (face) {
    auto* videoFace = dynamic_cast<VideoFace*>(face);
    if (videoFace) {
      videoFace->removeObserver(ob);
    }
  }
}

// 通过 AvoxManager 工厂表创建视频→面部推理器 (组件 loadModule 时注册),
// none 或组件未注册返回 nullptr
IVideoFace* createVideoFace(VideoFaceType type) {
  ModuleMgr::Get().ensureStarted();
  const char* key = (type == VideoFaceType::mediapipe) ? "mediapipe" : nullptr;
  return key ? AvoxManager::Get().videoFaceHub.create(key) : nullptr;
}

// ARKit52 canonical 名表 (getArkit52NamesCsv 返回值): 与 AvoxAvatar.h 顺序注释、
// plugins/avox_avatar 两插件输出、godot arkit52.gd 严格一致
static const char* kArkit52NamesCsv =
    "_neutral,browDownLeft,browDownRight,browInnerUp,browOuterUpLeft,browOuterUpRight,"
    "cheekPuff,cheekSquintLeft,cheekSquintRight,eyeBlinkLeft,eyeBlinkRight,"
    "eyeLookDownLeft,eyeLookDownRight,eyeLookInLeft,eyeLookInRight,eyeLookOutLeft,"
    "eyeLookOutRight,eyeLookUpLeft,eyeLookUpRight,eyeSquintLeft,eyeSquintRight,"
    "eyeWideLeft,eyeWideRight,jawForward,jawLeft,jawOpen,jawRight,mouthClose,"
    "mouthDimpleLeft,mouthDimpleRight,mouthFrownLeft,mouthFrownRight,mouthFunnel,"
    "mouthLeft,mouthLowerDownLeft,mouthLowerDownRight,mouthPressLeft,mouthPressRight,"
    "mouthPucker,mouthRight,mouthRollLower,mouthRollUpper,mouthShrugLower,mouthShrugUpper,"
    "mouthSmileLeft,mouthSmileRight,mouthStretchLeft,mouthStretchRight,mouthUpperUpLeft,"
    "mouthUpperUpRight,noseSneerLeft,noseSneerRight";

const char* getArkit52NamesCsv() { return kArkit52NamesCsv; }

}
