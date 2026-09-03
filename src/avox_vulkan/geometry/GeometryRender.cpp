#include "GeometryRender.hpp"

#include "VkGeometryLayer.hpp"

namespace avox {

GeometryRender::GeometryRender() {}

void GeometryRender::setLayer(VkGeometryLayer* layer_) {
  std::lock_guard<std::mutex> lock(mtx);
  layer = layer_;
  // 重新接入/re-enable/scale 重建后是新层：canvas 和 UBO 都要重新填充
  rasterDirty = true;
  paramDirty = true;
  // scale 影响 canvas 尺寸（resetGraph），绑定后立即下发缓存的 scale
  if (layer) {
    layer->setScale(curScale);
  }
}

bool GeometryRender::consumeRasterDirty(GeoSnapshot& out) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!rasterDirty) {
    return false;
  }
  rasterDirty = false;
  out.shapes = shapes;
  return true;
}

bool GeometryRender::consumeParamDirty(GeoSnapshot& out) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!paramDirty) {
    return false;
  }
  paramDirty = false;
  out.r = curR;
  out.g = curG;
  out.b = curB;
  out.threshold = curThreshold;
  return true;
}

void GeometryRender::setColor(float r, float g, float b) {
  std::lock_guard<std::mutex> lock(mtx);
  curR = r;
  curG = g;
  curB = b;
  paramDirty = true;
}

void GeometryRender::setScale(float scale) {
  std::lock_guard<std::mutex> lock(mtx);
  if (scale <= 0.0f) {
    scale = 1.0f;
  }
  curScale = scale;
  // canvas 尺寸变化 → 形状要按新网格重画
  rasterDirty = true;
  // 立即下发：scale 改变 canvas 尺寸，需要 resetGraph
  if (layer) {
    layer->setScale(scale);
  }
}

void GeometryRender::setThreshold(float tau) {
  std::lock_guard<std::mutex> lock(mtx);
  if (tau < 0.0f) tau = 0.0f;
  if (tau > 1.0f) tau = 1.0f;
  curThreshold = tau;
  paramDirty = true;
}

void GeometryRender::clear() {
  std::lock_guard<std::mutex> lock(mtx);
  shapes.clear();
  rasterDirty = true;
}

void GeometryRender::drawPoint(float x, float y, float radiusPx) {
  std::lock_guard<std::mutex> lock(mtx);
  GeoShape s;
  s.type = GeoType::point;
  s.a = vec2f(x, y);
  s.radius = radiusPx;
  s.fill = true;
  shapes.push_back(s);
  rasterDirty = true;
}

void GeometryRender::drawLine(float x0, float y0, float x1, float y1) {
  std::lock_guard<std::mutex> lock(mtx);
  GeoShape s;
  s.type = GeoType::line;
  s.a = vec2f(x0, y0);
  s.b = vec2f(x1, y1);
  shapes.push_back(s);
  rasterDirty = true;
}

void GeometryRender::drawRect(float x0, float y0, float x1, float y1, bool fill) {
  std::lock_guard<std::mutex> lock(mtx);
  GeoShape s;
  s.type = GeoType::rect;
  s.a = vec2f(x0, y0);
  s.b = vec2f(x1, y1);
  s.fill = fill;
  shapes.push_back(s);
  rasterDirty = true;
}

void GeometryRender::drawCircle(float cx, float cy, float radiusPx, bool fill) {
  std::lock_guard<std::mutex> lock(mtx);
  GeoShape s;
  s.type = GeoType::circle;
  s.a = vec2f(cx, cy);
  s.radius = radiusPx;
  s.fill = fill;
  shapes.push_back(s);
  rasterDirty = true;
}

}
