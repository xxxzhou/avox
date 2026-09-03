#pragma once

#include "../VkContext.hpp"

namespace avox {
// gpu buffer主要用于保存GPU数据,几种主要场景
enum class BufferUsage {
  other,
  // 1 CPU-GPU频繁交互,如UBO，Tick更新数据，需要手动刷新
  store,
  // 2 用于GPU/GPU计算中的缓存数据,并不需要和CPU交互
  program,
  // 3 不频繁更新,如纹理,模型VBO，自动更新
  onestore,
};

// (CPU可写)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
// (unmap后GPU最新)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
// (CPU不可写,Computer shader)(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
// memory与view的关系应该是一对多,本类用于特殊的一对一情况
// 后期专门设计一个memory对应多个view的类.(可以一个mesh的IBO,VBO放一起,也可多Mesh放一块)
class VkWrapBuffer : public VkContextRef {
public:
  VkWrapBuffer();
  virtual ~VkWrapBuffer();

private:
  VkBufferView view = VK_NULL_HANDLE;
  // VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;  // VK_PIPELINE_STAGE_HOST_BIT;
  VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_HOST_BIT;
  VkAccessFlags accessFlags = VK_ACCESS_HOST_WRITE_BIT;
  int32_t bufferSize = 0;
  uint8_t *pData = nullptr;
  VkMemoryRequirements memoryRequires = {};
  BufferUsage usage = BufferUsage::other;

public:
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDescriptorBufferInfo descInfo = {};

public:
  void initResoure(BufferUsage usage, uint32_t dataSize,
                   VkBufferUsageFlags usageFlag, uint8_t *cpuData = nullptr);
  void initView(VkFormat viewFormat);
  void release();
  // 默认是满足整个bufferSize的数据
  void upload(const uint8_t *cpuData);
  void upload(const uint8_t *cpuData,int32_t size);
  // 复制pData到cpuData
  void download(uint8_t *cpuData);
  // 提交
  void submit();
  // 如果是store,使用HOST_CACHED,需要手动调用刷新
  // bRead表示CPU是读还是写
  void flush(bool bRead);

  // 直接返回buffer关联pdata
  inline uint8_t *getCpuData() { return pData; };

  inline int32_t getBufferSize() { return bufferSize; };

  // Computer shader后,插入相关barrier,由读变写,由写变读,确保前面操作完成
  // 后续添加渲染管线与计算管线添加barrier的逻辑
  void addBarrier(VkCommandBuffer command, VkPipelineStageFlags newStageFlags,
                  VkAccessFlags newAccessFlags);
};
}
