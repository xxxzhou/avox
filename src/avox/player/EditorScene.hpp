#pragma once

#include <mutex>

#include "../AvoxCapture.h"
#include "../video/ImageBuffer.hpp"
#include "../../avox_vulkan/layer/VkSceneLayer.hpp"

namespace avox {

// IEditorScene 实现: set* 即拷贝快照+升代数(控制线程), 渲染线程整读。
// 图集在 setCursorAtlas 时拷贝一份(宿主栈/临时缓冲都安全)
class AVOX_EXPORT EditorScene : public IEditorScene, public SceneSource {
 public:
  EditorScene() = default;
  virtual ~EditorScene() override = default;

 public:
  // IEditorScene
  virtual void setStyle(const SceneStyle& style) override;
  virtual void setCanvas(int32_t width, int32_t height) override;
  virtual void setSegments(const SceneCameraSegment* segs,
                           int32_t count) override;
  virtual void setCursorTrack(const SceneCursorSample* samples,
                              int32_t count) override;
  virtual void setCursorAtlas(IImageBuffer* atlas, int32_t cols,
                              int32_t rows) override;
  virtual void setClicks(const SceneClick* clicks, int32_t count) override;

  // SceneSource(渲染线程)
  virtual uint64_t generation() override;
  virtual bool loadSnapshot(SceneSnapshot& out) override;
  virtual void canvas(int32_t& w, int32_t& h) override;
  virtual IImageBuffer* getAtlas() override;

 private:
  std::mutex mtx;
  SceneSnapshot snap = {};
  // 图集副本(空格子=未设, 渲染侧给 1x1 透明兜底)
  ImageBuffer atlasBuffer;
  int32_t atlasCols = 0;
  int32_t atlasRows = 0;
  bool bAtlasSet = false;
};

}
