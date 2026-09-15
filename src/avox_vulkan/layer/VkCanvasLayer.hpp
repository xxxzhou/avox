#pragma once

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "VkLayer.hpp"
#include "avox/subtitle/SubtitleCanvas.h"
#include "avox_vulkan/vulkan/VkTexture.hpp"

namespace avox {

// 画布混合参数(与 canvasBlend.comp 的 UBO 布局严格同序, std140 全 float)
struct CanvasBlendParamet {
  // 变换后门控矩形中心与宽高(帧归一化坐标)
  float centerX = 0.f;
  float centerY = 0.f;
  float width = 0.f;
  float height = 0.f;
  // 整层不透明度(0=直通/无字幕, 1=完全显示)
  float opacity = 0.f;
  // 采样反算仿射原点与 1/scale: 正向 screen = P + (c-P)*s + o(P=帧中心轴心,
  // o=帧归一化 offset), 反算 suv = origin + uv * invScale, origin = P - (P+o)*invScale
  float originX = 0.f;
  float originY = 0.f;
  float invScale = 1.f;
};

// ASS/PGS 字幕画布层(计划 ASS字幕渲染计划.md §3.4):
// 视频 rgba8 帧 + 内部 rgba8 canvas(=帧尺寸) 经 canvasBlend.comp 做
// premultiplied source-over。画布内容按 bbox 裁剪上传(子矩形拷贝),
// 静止段零上传; 无字幕帧 opacity=0 走 shader 直通。
class VkCanvasLayer : public VkLayer, public ICanvasLayer {
  AVOX_LAYER_GETNAME(VkCanvasLayer)

 private:
  CanvasBlendParamet vkParamet = {};

  // 整帧尺寸的 CPU 画布(rgba8): 换内容时先清上一帧 bbox 再 blit 新 bbox,
  // GPU 侧只采样当前 UBO 矩形, 矩形外残留永不可见
  std::vector<uint8_t> canvasData;
  int32_t frameW = 0;
  int32_t frameH = 0;
  // 当前内容 bbox(整帧坐标), hasContent=false 时 shader 直通
  int32_t rectX = 0, rectY = 0, rectW = 0, rectH = 0;
  bool hasContent = false;
  bool bNeedUpdate = false;
  // 用户全局变换(setCanvasTransform 写入, onPreFrame 每帧与 base bbox 合成;
  // 不做增量累加, 静止帧改值也即时生效)
  float userScale = 1.f;
  float userOffsetX = 0.f;
  float userOffsetY = 0.f;
  float userOpacity = 1.f;
  // 内容归 updateCanvas/onPreFrame 的调用线程(渲染线程)所有, 锁防御性保留
  std::mutex mtx;
  // 图未建时的来件暂存(建图后首帧应用, 避免丢第一屏字幕)
  std::vector<uint8_t> pendingCanvas;
  int32_t pendingW = 0, pendingH = 0, pendingStride = 0;
  int32_t pendingX = 0, pendingY = 0;
  bool hasPending = false;

  // staging 与采样纹理
  std::unique_ptr<VkWrapBuffer> cpuBuffer;
  VulkanTexturePtr canvasImage;

 public:
  VkCanvasLayer();
  virtual ~VkCanvasLayer();

 public:
  // ICanvasLayer
  virtual void updateCanvas(const uint8_t* rgba, int32_t w, int32_t h,
                            int32_t stride, int32_t x, int32_t y) override;
  virtual void clearCanvas() override;
  // 全局变换(字幕轨槽路径): 存成员, 下个 onPreFrame 合成进 UBO
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity);

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitPipe() override;
  virtual void onPreFrame() override;
  virtual void onCommand() override;

 private:
  void applyPending();
};

// 稳定的 ICanvasLayer 前端(归 VkVideoRender 所有): 外部(字幕视图)持它,
// 图重建时层指针会换, 由它转发 + 暂存建图期间的来件(与 FontRender 同模式)。
// 全局变换与内容丢失信号也存这里(权威副本), setCanvasLayer 时补发/置位
class CanvasRender : public ICanvasLayer {
 public:
  ~CanvasRender() override { setCanvasLayer(nullptr); }

  // ICanvasLayer: 转发到当前层; 未绑定层时暂存最新一帧(建图后补发)
  virtual void updateCanvas(const uint8_t* rgba, int32_t w, int32_t h,
                            int32_t stride, int32_t x, int32_t y) override {
    std::lock_guard<std::mutex> lock(mtx);
    if (layer) {
      layer->updateCanvas(rgba, w, h, stride, x, y);
      hasStash = false;
      return;
    }
    if (!rgba || w <= 0 || h <= 0) {
      return;
    }
    stash.resize((size_t)h * stride);
    memcpy(stash.data(), rgba, (size_t)h * stride);
    sw = w; sh = h; sstride = stride; sx = x; sy = y;
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

  // 图构建/销毁时由 VkVideoRender 调用(同渲染线程)
  void setCanvasLayer(VkCanvasLayer* l) {
    std::lock_guard<std::mutex> lock(mtx);
    layer = l;
    if (layer) {
      if (hasStash) {
        layer->updateCanvas(stash.data(), sw, sh, sstride, sx, sy);
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
  VkCanvasLayer* layer = nullptr;
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
