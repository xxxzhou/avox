#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "VkLayer.hpp"
#include "avox/subtitle/CanvasRender.hpp"
#include "avox/subtitle/CanvasState.hpp"
#include "avox/subtitle/SubtitleCanvas.hpp"
#include "avox_vulkan/vulkan/VkTexture.hpp"

namespace avox {

// ASS/PGS 字幕画布层(计划 ASS字幕渲染计划.md §3.4):
// 视频 rgba8/16f 帧 + 内部 rgba8 canvas 经 canvasBlend(.comp/HDR.comp) 做
// premultiplied source-over。canvas = 合成画布尺寸(>1080p 恒 1920x1080 基准,
class VkCanvasLayer : public VkLayer, public ICanvasLayer {
  AVOX_LAYER_GETNAME(VkCanvasLayer)

 private:
  CanvasBlendParamet vkParamet = {};

  // 线性域变体开关(构造参数进入, onInitGraph 消费)
  bool bLinearDomain = false;

  // 画布 CPU 侧状态机(多后端共享, 字幕画布多后端渲染计划 §二.4)
  CanvasState state;

  // staging 与采样纹理
  std::unique_ptr<VkWrapBuffer> cpuBuffer;
  VulkanTexturePtr canvasImage;

 public:
  VkCanvasLayer();
// 线性域变体构造(直通拓扑, 字幕画布多后端渲染计划 §5.4): addNode 同步走
// attach→onInitGraph 声明格式/shader, 域开关必须经构造参数进入, 后置设置
  explicit VkCanvasLayer(bool bLinearDomain);
  virtual ~VkCanvasLayer();

 public:
  // ICanvasLayer(数据面全委托 CanvasState)
  virtual void updateCanvas(const AssCanvas& canvas) override;
  virtual void clearCanvas() override;
  // 全局变换(字幕轨槽路径): 委托 CanvasState, 下个 onPreFrame 合成进 UBO
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

}
