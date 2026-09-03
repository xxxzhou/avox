#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "../layer/VkResizeLayer.hpp"
#include "../extra/VkConvertImageLayer.hpp"
#include "../extra/VkLinearFilterLayer.hpp"
#include "../extra/VkSeparableLinearLayer.hpp"

namespace avox {

// 导向滤波自引导去块 - 准备逐通道平方 c²
class VkToMatDeblockLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkToMatDeblockLayer)
 public:
  VkToMatDeblockLayer();
  virtual ~VkToMatDeblockLayer();

 protected:
  virtual void onInitGraph() override;
};

// 导向滤波自引导去块 - 逐通道求解 a (每通道独立 1D guided filter)
// 与 VkGuidedSolveLayer(3D guidance+标量p, 抠图用) 区别: 逐通道独立, 保色
class VkGuidedSolveDeblockLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkGuidedSolveDeblockLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkGuidedSolveDeblockLayer();
  virtual ~VkGuidedSolveDeblockLayer();

 protected:
  virtual void onInitGraph() override;
};

// 导向滤波自引导去块层 (VkGroupLayer, 自身持输出shader)
// 论文: http://kaiminghe.com/publications/pami12guidedfilter.pdf
// 逐通道自引导: guidance I = input p = 各通道自身
//   q = mean_c + a·(I - mean_c), a = var_c/(var_c+eps) ∈ [0,1]
//   平坦区 a≈0 -> q≈mean_c(平滑去块, 逐通道保色); 边缘 a≈1 -> q≈I(保留)
//   a∈[0,1] => q 是 I 与 mean_c 的凸组合, 值域有界, 不会过曝/变黑
// group 自身持 guidedDeblock.comp 作为输出节点:
//   in[0]=原始I(rgba32f, from convert rgba8→rgba32f), in[1]=mean_c(rgba32f), in[2]=a(rgba32f)
//   out=rgba8 (sRGB 空间, 去块在 sRGB 做, eps 匹配 sRGB 值域)
// 输入: rgba8 (sRGB 空间)  输出: rgba8 (sRGB 空间)
//   线性空间暗部值极小(0.01级), eps=0.01 远大于暗部方差→a≈0→全局blur
//   sRGB 空间值域均匀(0~1), eps=0.001~0.05 匹配块边界方差量级
// zoom=1 全分辨率优化: 去掉 resize 层(恒等拷贝)和 box5 层(a 不需要再平滑)
//   子链: convert(rgba8→rgba32f) → [box1=mean_c, toMat→box2=mean_cc] → solve(a) → output
//   box1 直接送 output 作为 mean_c (不上采样), solve 直接送 output 作为 a
// zoom>1 时保留完整子链: convert→resize→[box1,box2]→solve→box5→resize1/resize1m→output
// boxSize 由 FSR 层设为 5 (zoom=1 下覆盖 ~4px, 匹配块边界)
class VkGuidedDeblockLayer : public VkGroupLayer, public IParamet<GuidedParamet> {
  AVOX_LAYER_GETNAME(VkGuidedDeblockLayer)

 private:
  VKTNodePtr<VkConvertImageLayer> convertLayer = nullptr;
  VKTNodePtr<VkSizeScaleLayer> resizeLayer = nullptr;   // 下采样 (相对 fx=1/zoom)
  VKTNodePtr<VkToMatDeblockLayer> toMatLayer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box1Layer = nullptr;  // mean_c
  VKTNodePtr<VkBoxBlurSLayer> box2Layer = nullptr;  // mean_cc
  VKTNodePtr<VkGuidedSolveDeblockLayer> solveDeblockLayer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box5Layer = nullptr;  // 平滑 a
  VKTNodePtr<VkSizeScaleLayer> resize1Layer = nullptr;   // 上采样 a (相对 fx=zoom)
  VKTNodePtr<VkSizeScaleLayer> resize1mLayer = nullptr;  // 上采样 mean_c (相对 fx=zoom)

  // 全分辨率算 a (zoom=1): boxSize=5 覆盖 ~4px 邻域, 匹配 H.264 块边界宽度(2-4px)。
  //   不用 boxSize=9: 9×9=81px 均值窗口远超块边界宽度, 把真实细节也抹进 mean_c → 整体 blur。
  //   zoom=2 下采样会把细线/边缘平均稀释 → 方差低估 → a 偏小 → 细线变暗; zoom=1 不会。
  //   zoom=1 时 resize 层为恒等拷贝; 未来可优化: 去掉 resize 层省开销。
  const int32_t zoom = 1;

 public:
  VkGuidedDeblockLayer();
  virtual ~VkGuidedDeblockLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

}
