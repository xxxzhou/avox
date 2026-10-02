#pragma once

#include <cstring>
#include <mutex>
#include <vector>

#include "SubtitleCanvas.hpp"

namespace avox {

// 稳定的 ICanvasLayer 前端(字幕画布多后端渲染计划 P0, 自 VkCanvasLayer.hpp 下沉):
// 外部(字幕视图)持它, 图重建时后端层指针会换, 由它转发 + 暂存建图期间的来件。
// 全局变换与内容丢失信号也存这里(权威副本), setCanvasLayer 时补发/置位。
// 后端(VkCanvasLayer/平台腿)只实现 ICanvasLayer, 不感知本类。
class CanvasRender : public ICanvasLayer {
 public:
  ~CanvasRender() override { setCanvasLayer(nullptr); }

  // ICanvasLayer: 转发到当前层; 未绑定层时暂存最新一帧(建图后补发)
  virtual void updateCanvas(const AssCanvas& canvas) override {
    std::lock_guard<std::mutex> lock(mtx);
    if (layer) {
      layer->updateCanvas(canvas);
      hasStash = false;
      return;
    }
    if (!canvas.rgba || canvas.width <= 0 || canvas.height <= 0) {
      return;
    }
    stash.resize((size_t)canvas.height * canvas.stride);
    memcpy(stash.data(), canvas.rgba, (size_t)canvas.height * canvas.stride);
    sw = canvas.width; sh = canvas.height; sstride = canvas.stride;
    sx = canvas.x; sy = canvas.y;
    hasStash = true;
  }
  virtual void clearCanvas() override {
    std::lock_guard<std::mutex> lock(mtx);
    hasStash = false;
    if (layer) {
      layer->clearCanvas();
    }
  }

  // 全局变换: 存权威副本(层随图重建换指针, 由 setCanvasLayer 补发)并转发当前层
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity) override {
    std::lock_guard<std::mutex> lock(mtx);
    tScale = scale;
    tOffsetX = offsetX;
    tOffsetY = offsetY;
    tOpacity = opacity;
    hasTransform = true;
    if (layer) {
      layer->setCanvasTransform(scale, offsetX, offsetY, opacity);
    }
  }

  // 内容丢失信号(换层/脱钩且无暂存可恢复时置位), 取走即清;
  // 消费方(字幕视图)据此强制重传, 否则字幕消失到下次内容变化
  bool takeContentStale() override {
    std::lock_guard<std::mutex> lock(mtx);
    const bool stale = contentStale;
    contentStale = false;
    return stale;
  }

  // 图构建/销毁时由后端渲染器调用(同渲染线程); l 为后端层实例(可为空)
  void setCanvasLayer(ICanvasLayer* l) {
    std::lock_guard<std::mutex> lock(mtx);
    layer = l;
    if (layer) {
      if (hasStash) {
        AssCanvas c;
        c.rgba = stash.data();
        c.width = sw;
        c.height = sh;
        c.stride = sstride;
        c.x = sx;
        c.y = sy;
        layer->updateCanvas(c);
        hasStash = false;
        contentStale = false;  // 内容已随暂存迁到新层
      } else {
        contentStale = true;   // 新层为空且无可恢复内容
      }
      if (hasTransform) {
        layer->setCanvasTransform(tScale, tOffsetX, tOffsetY, tOpacity);
      }
    } else {
      contentStale = true;     // 脱钩: 下次挂层必为空层
    }
  }

 private:
  std::mutex mtx;
  ICanvasLayer* layer = nullptr;
  std::vector<uint8_t> stash;
  int32_t sw = 0, sh = 0, sstride = 0, sx = 0, sy = 0;
  bool hasStash = false;
  // 全局变换权威副本
  float tScale = 1.f;
  float tOffsetX = 0.f;
  float tOffsetY = 0.f;
  float tOpacity = 1.f;
  bool hasTransform = false;
  bool contentStale = false;
};

}
