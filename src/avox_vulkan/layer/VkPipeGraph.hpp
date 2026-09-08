#pragma once

#include <memory>

#include "VkLayer.hpp"
#include "avox/layer/PipeGraph.hpp"

#ifdef WIN32
#include "../windows/VkWinImage.hpp"
#include "avox_windows/dx11/Dx11Helper.hpp"
#endif

namespace avox {

class VkFontLayer;
class VkGeometryLayer;

class VkPipeGraph : public VPipeGraph<VkLayer>, public VkContextRef {
 public:
  VkPipeGraph(VkContext* ctx = nullptr);
  virtual ~VkPipeGraph();

  friend class VkLayer;
  friend class VkInputLayer;
  friend class VkDrawPointsPreLayer;
  friend class VkLinearFilterLayer;
  friend class VkSeparableLayer;
  friend class VkFontLayer;
  friend class VkGeometryLayer;
  friend class VkQEnhanceLayer;

 private:
  // 管线缓存,加速管线创建
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  // 多缓冲命令缓冲区
  std::vector<VkCommandBuffer> computerCmds;
  int32_t currentCmdIndex = 0;
  // 多缓冲同步对象
  std::vector<VkFence> cmdFences;
  VkSampler linearSampler = VK_NULL_HANDLE;
  VkSampler nearestSampler = VK_NULL_HANDLE;
  // 输出层
  std::vector<VkLayer*> vkOutputLayers;
  // 余下层
  std::vector<VkLayer*> vkLayers;
  // 多缓冲执行
  uint32_t commandCount = 2;
  // 确定是否在重置生成资源与commandbuffer中
  VkEvent outEvent = VK_NULL_HANDLE;
  VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  // 共享VkDevice的代际, 与VkContext::devEpoch()不一致时需重拉句柄并重建
  uint32_t vkDevEpoch = 0;
  // RenderType renderType = RenderType::other;
#ifdef WIN32
  MComPtr<ID3D11Device> dxDevice = nullptr;
  MComPtr<ID3D11DeviceContext> dxCtx = nullptr;
  bool bDX11Update = false;
#endif

 public:
  // 获取当前正在使用的命令缓冲区
  VkCommandBuffer getCurrentCmdBuffer();
  VulkanTexturePtr getOutTex(NodeSlot slot);
  bool getMustSampled(NodeSlot slot);
  bool bOutLayer(int32_t node);
  bool resourceReady();

#ifdef WIN32
  ID3D11Device* getD3D11Device();
#endif

 protected:
  // 创建graph自有资源(pipelineCache/cmdPool/命令缓冲/fence/event/sampler)
  void createGraphResources(VkContext* ctx);
  // 所有layer调用initbuffer后
  virtual void onReset() override;
  virtual void onInitBuffers() override;
  virtual void onRun() override;

 public:
  // 设备丢失恢复门: 恢复中跳过本帧, 设备重建后重拉句柄+走reset全图重建
  virtual void run() override;
};

}
