#pragma once

#include "../AvoxAvatar.h"
#include "../AvoxDef.h"
#include "../module/Observer.hpp"

namespace avox {

// 视频→身体姿态 33 点 内部基类: 对称 VideoFace (src/avox/avatar/VideoFace.hpp)。
// IBody 对外接口 + Observer<IBodyOb> 回调分发;
// 推理逻辑纯虚留给 MediaPipeBody 等子类 (avox_avatar 插件)。
// 方向: RGBA 进 (feed) → MediaPipe Pose 33 点身体 landmark 出 (经 IBodyOb)。
class AVOX_EXPORT BodyImpl : public IBody, public Observer<IBodyOb> {
 public:
  BodyImpl() = default;
  virtual ~BodyImpl() = default;

 public:
  // 推理相关纯虚, 子类实现
  void start() override = 0;
  void feed(IImageBuffer* img, int64_t pts) override = 0;
  void stop() override = 0;
  bool loading() override = 0;
};

}
