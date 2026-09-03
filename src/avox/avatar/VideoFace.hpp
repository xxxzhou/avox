#pragma once

#include "../AvoxAvatar.h"
#include "../AvoxDef.h"
#include "../module/Observer.hpp"

namespace avox {

// 视频→blendshape+landmarks 内部基类: 对称 AudioFace (src/avox/audio/AudioFace.hpp)。
// IVideoFace 对外接口 + Observer<IVideoFaceOb> 回调分发;
// 通用配置 (modelLevel) 在基类, 推理逻辑纯虚留给 MediaPipeFace 等子类 (avox_avatar 插件)。
// 方向: RGBA 进 (feed) → ARKit52 blendshape + 478 landmarks 出 (经 IVideoFaceOb)。
class AVOX_EXPORT VideoFace : public IVideoFace, public Observer<IVideoFaceOb> {
 public:
  VideoFace() = default;
  virtual ~VideoFace() = default;

 protected:
  ModelLevel modelLevel = ModelLevel::base;

 public:
  // 通用配置在基类实现 (仅赋值)
  void setModelLevel(ModelLevel level) override;
  // 推理相关纯虚, 子类实现
  void start() override = 0;
  void feed(IImageBuffer* img, int64_t pts) override = 0;
  void stop() override = 0;
  bool loading() override = 0;
};

}
