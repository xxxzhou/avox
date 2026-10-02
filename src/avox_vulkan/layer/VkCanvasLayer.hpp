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
// ≤1080p 与帧 1:1, 见 subtitleCanvasSize), 铺满全帧的拉伸由 sampler 归一化
// 映射完成。画布内容按 bbox 裁剪上传, 静止段零上传; 无字幕帧 opacity=0 走
// shader 直通。数据面(bbox/变换/门控矩形合成)在 CanvasState(多后端共享),
// 本层只做 VK 上传与 dispatch。
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
  // 已来不及(in/out rgba16f + canvasBlendHDR)
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
