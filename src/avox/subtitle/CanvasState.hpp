#pragma once

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "SubtitleCanvas.hpp"

namespace avox {

// 画布混合参数(与 canvasBlend.comp / canvasBlendHDR.comp 的 UBO 布局严格同序,
// std140 全 float)
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

// 字幕画布 CPU 侧状态机(字幕画布多后端渲染计划 §二.4 逻辑单点): bbox 清旧
// blit 新/建图前来件暂存/全局变换权威副本/每帧门控矩形与采样反算合成。
// 各后端(VK/Metal/DX11/GLES)只做各自的上传与混合。锁内自洽, 内容可跨线程推。
class CanvasState {
 public:
  // 建画布(后端建图时按合成画布尺寸调): 尺寸变化即重置; 返回 false 尺寸非法
  bool reset(int32_t w, int32_t h) {
    std::lock_guard<std::mutex> lock(mtx);
    if (w <= 0 || h <= 0) {
      return false;
    }
    if (w == canvasW && h == canvasH) {
      return true;
    }
    canvasData.assign((size_t)w * h * 4, 0);
    canvasW = w;
    canvasH = h;
    rectX = rectY = rectW = rectH = 0;
    bHasContent = false;
    bNeedUpdate = false;
    return true;
  }

  // 上传画布(bbox 裁剪, 画布坐标): 画布未建时暂存, 建图后由 takePending 应用
  void updateCanvas(const AssCanvas& canvas) {
    const uint8_t* rgba = canvas.rgba;
    const int32_t w = canvas.width;
    const int32_t h = canvas.height;
    const int32_t stride = canvas.stride;
    const int32_t x = canvas.x;
    const int32_t y = canvas.y;
    if (!rgba || w <= 0 || h <= 0) {
      return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    if (canvasW <= 0 || canvasH <= 0) {
      pendingCanvas.assign((size_t)h * stride, 0);
      memcpy(pendingCanvas.data(), rgba, (size_t)h * stride);
      pendingW = w;
      pendingH = h;
      pendingStride = stride;
      pendingX = x;
      pendingY = y;
      bHasPending = true;
      return;
    }
    if (bHasContent) {
      // 清上一帧 bbox(CPU 画布侧); GPU 侧旧区域在本帧已不在采样矩形内
      for (int32_t row = 0; row < rectH; ++row) {
        memset(canvasData.data() + (size_t)(rectY + row) * canvasW * 4 +
                   (size_t)rectX * 4,
               0, (size_t)rectW * 4);
      }
    }
    // blit 新 bbox(裁到画布内)
    const int32_t cx0 = x < 0 ? 0 : x;
    const int32_t cy0 = y < 0 ? 0 : y;
    int32_t cx1 = x + w, cy1 = y + h;
    if (cx1 > canvasW) cx1 = canvasW;
    if (cy1 > canvasH) cy1 = canvasH;
    for (int32_t row = cy0; row < cy1; ++row) {
      const uint8_t* src =
          rgba + (size_t)(row - y) * stride + (size_t)(cx0 - x) * 4;
      uint8_t* dst =
          canvasData.data() + (size_t)row * canvasW * 4 + (size_t)cx0 * 4;
      memcpy(dst, src, (size_t)(cx1 - cx0) * 4);
    }
    rectX = cx0;
    rectY = cy0;
    rectW = cx1 - cx0;
    rectH = cy1 - cy0;
    bHasContent = rectW > 0 && rectH > 0;
    bNeedUpdate = true;
  }

  // 清空(无字幕帧): 层走直通/跳画
  void clearCanvas() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!bHasContent) {
      return;
    }
    bHasContent = false;
    bNeedUpdate = true;
  }

  // 全局变换(字幕轨槽): 存权威副本, 静止帧改值也即时生效
  void setCanvasTransform(float scale, float offsetX, float offsetY,
                          float opacity) {
    std::lock_guard<std::mutex> lock(mtx);
    userScale = scale > 0.f ? scale : 1.f;
    userOffsetX = offsetX;
    userOffsetY = offsetY;
    userOpacity = (std::min)((std::max)(opacity, 0.f), 1.f);
  }

  // 每帧参数合成(门控矩形与采样反算, 式同旧 VkCanvasLayer::onPreFrame):
  // 无内容 opacity=0 强制直通; 轴心=帧中心, 正向 screen = P + (c-P)*s + o
  CanvasBlendParamet computeParamet() {
    std::lock_guard<std::mutex> lock(mtx);
    CanvasBlendParamet p;
    if (!bHasContent) {
      return p;  // opacity=0
    }
    const float s = userScale;
    const float inv = 1.f / s;
    const float qx = 0.5f + userOffsetX;
    const float qy = 0.5f + userOffsetY;
    const float baseCX = (float)(rectX + rectW / 2) / canvasW;
    const float baseCY = (float)(rectY + rectH / 2) / canvasH;
    p.centerX = qx + (baseCX - 0.5f) * s;
    p.centerY = qy + (baseCY - 0.5f) * s;
    p.width = (float)rectW / canvasW * s;
    p.height = (float)rectH / canvasH * s;
    p.originX = 0.5f - qx * inv;
    p.originY = 0.5f - qy * inv;
    p.invScale = inv;
    p.opacity = userOpacity;
    return p;
  }

  // 上传判定(取走即清): 仅内容更新时 true —— 清空不传, GPU 侧残留被
  // opacity=0 门控/跳画, 永不可见
  bool takeUploadDue() {
    std::lock_guard<std::mutex> lock(mtx);
    const bool due = bNeedUpdate && bHasContent;
    bNeedUpdate = false;
    return due;
  }

  // CPU 画布与当前 bbox(上传方自取; 与 uploadDue 判定同渲染线程使用)
  const uint8_t* data() const { return canvasData.empty() ? nullptr : canvasData.data(); }
  int32_t width() const { return canvasW; }
  int32_t height() const { return canvasH; }
  int32_t bboxX() const { return rectX; }
  int32_t bboxY() const { return rectY; }
  int32_t bboxW() const { return rectW; }
  int32_t bboxH() const { return rectH; }
  bool hasContent() const {
    std::lock_guard<std::mutex> lock(mtx);
    return bHasContent;
  }

  // 建图前来件暂存(取走即清): 后端建图后以 updateCanvas 回灌
  bool hasPending() const {
    std::lock_guard<std::mutex> lock(mtx);
    return bHasPending;
  }
  void takePending(std::vector<uint8_t>& buf, int32_t& w, int32_t& h,
                   int32_t& stride, int32_t& x, int32_t& y) {
    std::lock_guard<std::mutex> lock(mtx);
    buf.swap(pendingCanvas);
    w = pendingW;
    h = pendingH;
    stride = pendingStride;
    x = pendingX;
    y = pendingY;
    pendingCanvas.clear();
    pendingW = pendingH = pendingStride = pendingX = pendingY = 0;
    bHasPending = false;
  }

 private:
  mutable std::mutex mtx;
  std::vector<uint8_t> canvasData;
  int32_t canvasW = 0;
  int32_t canvasH = 0;
  // 当前内容 bbox(画布坐标), hasContent=false 时层直通/跳画
  int32_t rectX = 0, rectY = 0, rectW = 0, rectH = 0;
  bool bHasContent = false;
  bool bNeedUpdate = false;
  // 全局变换权威副本
  float userScale = 1.f;
  float userOffsetX = 0.f;
  float userOffsetY = 0.f;
  float userOpacity = 1.f;
  // 画布未建时的来件暂存(建图后应用, 避免丢第一屏字幕)
  std::vector<uint8_t> pendingCanvas;
  int32_t pendingW = 0, pendingH = 0, pendingStride = 0;
  int32_t pendingX = 0, pendingY = 0;
  bool bHasPending = false;
};

}
