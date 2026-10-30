#include "EditorScene.hpp"

#include "../module/LogHelper.hpp"

namespace avox {

void EditorScene::setStyle(const SceneStyle& style) {
  std::lock_guard<std::mutex> lock(mtx);
  snap.style = style;
  snap.gen++;
}

void EditorScene::setCanvas(int32_t width, int32_t height) {
  std::lock_guard<std::mutex> lock(mtx);
  snap.canvasW = width;
  snap.canvasH = height;
  snap.gen++;
}

void EditorScene::setSegments(const SceneCameraSegment* segs, int32_t count) {
  std::lock_guard<std::mutex> lock(mtx);
  snap.segments.clear();
  if (segs && count > 0) {
    snap.segments.assign(segs, segs + count);
  }
  snap.gen++;
}

void EditorScene::setCursorTrack(const SceneCursorSample* samples,
                                 int32_t count) {
  std::lock_guard<std::mutex> lock(mtx);
  snap.cursor.clear();
  if (samples && count > 0) {
    snap.cursor.assign(samples, samples + count);
  }
  snap.gen++;
}

void EditorScene::setCursorAtlas(IImageBuffer* atlas, int32_t cols,
                                 int32_t rows) {
  std::lock_guard<std::mutex> lock(mtx);
  bAtlasSet = atlas && cols > 0 && rows > 0;
  atlasCols = bAtlasSet ? cols : 0;
  atlasRows = bAtlasSet ? rows : 0;
  snap.atlasCols = atlasCols;
  snap.atlasRows = atlasRows;
  if (bAtlasSet) {
    atlasBuffer.copyFrom(atlas, true);
  }
  snap.gen++;
}

void EditorScene::setClicks(const SceneClick* clicks, int32_t count) {
  std::lock_guard<std::mutex> lock(mtx);
  snap.clicks.clear();
  if (clicks && count > 0) {
    snap.clicks.assign(clicks, clicks + count);
  }
  snap.gen++;
}

uint64_t EditorScene::generation() {
  std::lock_guard<std::mutex> lock(mtx);
  return snap.gen;
}

bool EditorScene::loadSnapshot(SceneSnapshot& out) {
  std::lock_guard<std::mutex> lock(mtx);
  out = snap;
  return true;
}

void EditorScene::canvas(int32_t& w, int32_t& h) {
  std::lock_guard<std::mutex> lock(mtx);
  w = snap.canvasW;
  h = snap.canvasH;
}

IImageBuffer* EditorScene::getAtlas() {
  std::lock_guard<std::mutex> lock(mtx);
  return bAtlasSet ? &atlasBuffer : nullptr;
}

IEditorScene* createEditorScene() { return new EditorScene(); }

void destroyEditorScene(IEditorScene* scene) { delete scene; }

}
