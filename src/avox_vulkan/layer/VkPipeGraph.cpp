#include "VkPipeGraph.hpp"

#include <thread>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"

namespace avox {

VkPipeGraph::VkPipeGraph(VkContext* ctx) {
  if (!ctx) {
    ctx = VkContext::Shared();
  }
  setVkContext(ctx);
  // 单还是多
  commandCount = 1;
  // 得到当前graph需要的VkCommandBuffer
  VkPipelineCacheCreateInfo pipelineCacheInfo = {};
  pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  AVOX_VULKAN_LOG(vkCreatePipelineCache(vkDevice, &pipelineCacheInfo, nullptr,
                                       &pipelineCache),
                 "create pipeline cache failed");
  // context和呈现渲染相应command分开
  VkCommandPoolCreateInfo cmdPoolInfo = {};
  cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolInfo.queueFamilyIndex = ctx->getDeviceArgs()->computeIndex;
  cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  AVOX_VULKAN_LOG(vkCreateCommandPool(vkDevice, &cmdPoolInfo, nullptr, &cmdPool),
                 "create cmd pool failed");
  // 根据commandCount分配相应数量的命令缓冲区
  computerCmds.resize(commandCount);
  VkCommandBufferAllocateInfo cmdBufInfo = {};
  cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufInfo.commandPool = cmdPool;
  cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufInfo.commandBufferCount = commandCount;
  AVOX_VULKAN_LOG(
      vkAllocateCommandBuffers(vkDevice, &cmdBufInfo, computerCmds.data()),
      "alloc cmd buffer failed");
  for (int32_t i = 0; i < commandCount; i++) {
    LOGFLF(LogLevel::info, "cmd:", computerCmds[i]);
  }
  // 根据commandCount创建相应数量的fence
  cmdFences.resize(commandCount);
  for (uint32_t i = 0; i < commandCount; i++) {
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // 默认第一帧是有信号的
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(vkDevice, &fenceInfo, nullptr, &cmdFences[i]);
  }
  // 创建一个Event
  VkEventCreateInfo eventInfo = {};
  eventInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
  eventInfo.pNext = nullptr;
  eventInfo.flags = 0;
  vkCreateEvent(vkDevice, &eventInfo, nullptr, &outEvent);
  stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
  //
  createSampler(vkDevice, true, linearSampler);
  createSampler(vkDevice, false, nearestSampler);
#ifdef WIN32
  createDevice11(&dxDevice, &dxCtx);
#endif
  gpu = GpuType::vulkan;
}

VkPipeGraph::~VkPipeGraph() {
  // 等待所有 GPU 操作完成，避免资源销毁时还在被使用
  waitIdle();
  // 销毁所有命令缓冲区（必须在 fence 之前销毁）
  if (!computerCmds.empty()) {
    vkFreeCommandBuffers(vkDevice, cmdPool, (uint32_t)computerCmds.size(),
                         computerCmds.data());
    computerCmds.clear();
  }
  // 销毁所有fence
  for (uint32_t i = 0; i < commandCount; i++) {
    if (cmdFences[i]) {
      vkDestroyFence(vkDevice, cmdFences[i], nullptr);
      cmdFences[i] = VK_NULL_HANDLE;
    }
  }
  cmdFences.clear();
  if (linearSampler) {
    vkDestroySampler(vkDevice, linearSampler, nullptr);
    linearSampler = VK_NULL_HANDLE;
  }
  if (nearestSampler) {
    vkDestroySampler(vkDevice, nearestSampler, nullptr);
    nearestSampler = VK_NULL_HANDLE;
  }
  if (pipelineCache) {
    vkDestroyPipelineCache(vkDevice, pipelineCache, nullptr);
    pipelineCache = VK_NULL_HANDLE;
  }
  if (outEvent) {
    vkDestroyEvent(vkDevice, outEvent, VK_NULL_HANDLE);
    outEvent = VK_NULL_HANDLE;
  }
}

#ifdef WIN32
ID3D11Device* VkPipeGraph::getD3D11Device() { return dxDevice.Get(); }
#endif

VkCommandBuffer VkPipeGraph::getCurrentCmdBuffer() {
  // 如果commandCount=1, currentCmdIndex永远为0,自动退化为单缓冲
  assert(currentCmdIndex < commandCount);
  assert(currentCmdIndex < computerCmds.size());
  return computerCmds[currentCmdIndex];
}

VulkanTexturePtr VkPipeGraph::getOutTex(NodeSlot slot) {
  assert(slot.index < nodes.size());
  assert(slot.slot < nodes[slot.index]->outSlotCount());
  return nodes[slot.index]->getLayer()->outTexs[slot.slot];
}

bool VkPipeGraph::getMustSampled(NodeSlot slot) {
  assert(slot.index < nodes.size());
  assert(slot.slot < nodes[slot.index]->inSlotCount());
  return nodes[slot.index]->getLayer()->getSampled(slot.slot);
}

bool VkPipeGraph::bOutLayer(int32_t node) {
  assert(node < nodes.size());
  return nodes[node]->bOutputNode();
}

bool VkPipeGraph::resourceReady() {
  if (outEvent == VK_NULL_HANDLE) {
    return false;
  }
  // 资源是否已经重新生成
  auto res = vkGetEventStatus(vkDevice, outEvent);
  return res == VK_EVENT_SET;
}

void VkPipeGraph::onReset() {
  vkLayers.clear();
  // 告诉别的线程,需要等待资源重新生成
  vkResetEvent(vkDevice, outEvent);
  // 重置所有命令缓冲区（commandCount=1时自然只重置一个）
  for (uint32_t i = 0; i < commandCount; i++) {
    vkResetCommandBuffer(computerCmds[i],
                         VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
  }
  vkQueueWaitIdle(vkComputeQueue);
  currentCmdIndex = 0;
}

void VkPipeGraph::onInitBuffers() {
  LOGFLF(LogLevel::info, "start");
  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  // 对所有命令缓冲区进行录制
  for (uint32_t i = 0; i < commandCount; i++) {
    // 使vkLayers能拿到正常的cmd
    currentCmdIndex = i;
    vkBeginCommandBuffer(computerCmds[i], &cmdBeginInfo);
    // 拿到所有层
    vkOutputLayers.clear();
    vkLayers.clear();
    for (auto index : nodeExcs) {
      VkLayer* vkLayer = nodes[index]->getLayer();
      vkLayer->onCommand();
      if (vkLayer->bOutput) {
        vkOutputLayers.push_back(vkLayer);
      } else {
        vkLayers.push_back(vkLayer);
      }
    }
    // 记录状态改为可执行状态
    vkEndCommandBuffer(computerCmds[i]);
  }
  currentCmdIndex = 0;
  // 告诉别的线程,资源已经准备好
  vkSetEvent(vkDevice, outEvent);
  LOGFLF(LogLevel::info, "end");
}

void VkPipeGraph::onRun() {
#ifdef __APPLE__
  // IOS 切到后台不能调用GPU
  if (AvoxManager::Get().getBackground()) {
    return;
  }
#endif
  // HighClock clock = {};
  // 等待上一帧完成（commandCount=1时等待当前帧）
  assert(currentCmdIndex < cmdFences.size());
  // 重置fence为无信号
  vkResetFences(vkDevice, 1, &cmdFences[currentCmdIndex]);
  for (auto* layer : vkLayers) {
    layer->onPreFrame();
  }
  // log(LogLevel::info, "vk pipe cost 1:", clock.recordLast());
  for (auto* layer : vkOutputLayers) {
    layer->onPreFrame();
  }
  // log(LogLevel::info, "vk pipe cost 2:", clock.recordLast());
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &computerCmds[currentCmdIndex];
  // 集显或是手机GPU，其vkComputeQueue/presentQueue可能是同一个
  lockCommand();
  VkResult result =
      vkQueueSubmit(vkComputeQueue, 1, &submitInfo, cmdFences[currentCmdIndex]);
  AVOX_VULKAN_LOG(result, "submit compute queue failed");
  unLockCommand();
  // log(LogLevel::info, "vk pipe cost 3:", clock.recordLast());
  // [TEST] 撕裂排查: 同步等待本次 submit 完成,确认 graph 与 window
  // 之间是否缺同步
  vkWaitForFences(vkDevice, 1, &cmdFences[currentCmdIndex], VK_TRUE,
                  UINT64_MAX);
  // log(LogLevel::info, "vk pipe cost 4:", clock.recordLast());
  // 层是否有CPU数据输出
  for (auto* layer : vkLayers) {
    layer->onFrame();
  }
  // 运行输出层,可能有CPU数据要输出
  for (auto* layer : vkOutputLayers) {
    layer->onFrame();
  }
  // log(LogLevel::info, "vk pipe cost 5:", clock.recordLast());
  // 切换到下一个命令缓冲区（commandCount=1时保持为0）
  currentCmdIndex = (currentCmdIndex + 1) % commandCount;
}

}
