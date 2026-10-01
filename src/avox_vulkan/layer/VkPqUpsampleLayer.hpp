#pragma once

#include "VkLayer.hpp"

namespace avox {

// 链F 升样层(§6.3 G10): 硬解 HDR 片经平台渲染器原样写下的 rgba8 PQ 码,
// 在此做 PQ EOTF 解码 -> rgba16f 扩展线性域, 供后续合成/字幕/图像处理在
// 线性域进行, 最终交 FP16 交换链 HDR 呈现。
// 输入恒 rgba8(全平台统一的 VkInputLayer 对接面), 输出 16F。
class VkPqUpsampleLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkPqUpsampleLayer)

 public:
  VkPqUpsampleLayer(/* args */);
  virtual ~VkPqUpsampleLayer();

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

}
