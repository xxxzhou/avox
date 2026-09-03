#include "VkWrapBuffer.hpp"

namespace avox {
VkWrapBuffer::VkWrapBuffer() {}

VkWrapBuffer::~VkWrapBuffer() { release(); }

void VkWrapBuffer::initResoure(BufferUsage usage_, uint32_t dataSize,
                               VkBufferUsageFlags usageFlag, uint8_t *cpuData) {
  // 先释放可能已经在的资源
  release();
  // 重新生成
  bufferSize = dataSize;
  usage = usage_;
  // 生成buffer
  VkBufferCreateInfo bufInfo = {};
  bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufInfo.pNext = nullptr;
  bufInfo.usage = usageFlag; // 用来指定用于VBO/IBO/UBO等
  bufInfo.size = dataSize;
  bufInfo.queueFamilyIndexCount = 0;
  bufInfo.pQueueFamilyIndices = nullptr;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  bufInfo.flags = 0;
  // 生成一个buffer标识(可以多个标识到同一个显存空间DeviceMemory里,标识用来指定VBO/IBO/UBO等)
  AVOX_VULKAN_LOG(vkCreateBuffer(vkDevice, &bufInfo, nullptr, &buffer),
                 "create buffer failed");
  // Buffer存入位置
  VkMemoryPropertyFlags memoryFlag = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  // 相对HOST_COHERENT,HOST_CACHED 不能自动，但是性能更高？
  // 但是如果本来每帧我就要刷新一次用vkInvalidateMappedMemoryRanges
  // 那对于我每次tick都要调用vkInvalidateMappedMemoryRanges来说
  // 使用HOST_CACHED相对HOST_COHERENT提高的性能在那？
  // 一句话总结：
  // 即使每帧都 invalidate，HOST_CACHED 依然能让 CPU 读 Vulkan buffer
  // 时享受缓存带宽和低延迟，性能远优于 uncached。
  if (usage == BufferUsage::onestore) {
    memoryFlag = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  } else if (usage == BufferUsage::store) {
    memoryFlag = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }
  vkGetBufferMemoryRequirements(vkDevice, buffer, &memoryRequires);
  uint32_t memoryTypeIndex = 0;
  bool getIndex = wphyDevcie->getMemoryTypeIndex(memoryRequires.memoryTypeBits,
                                                 memoryFlag, memoryTypeIndex);
  assert(getIndex == true);
  VkMemoryAllocateInfo memoryInfo = {};
  memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryInfo.pNext = nullptr;
  memoryInfo.memoryTypeIndex = memoryTypeIndex;
  memoryInfo.allocationSize = memoryRequires.size;
  // 生成设备显存空间
  AVOX_VULKAN_LOG(vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &memory),
                 "allocate memory failed");
  if (cpuData) {
    AVOX_VULKAN_LOG(
        vkMapMemory(vkDevice, memory, 0, dataSize, 0, (void **)&pData),
        "map memory failed");
    memcpy(pData, cpuData, dataSize);
    // 这段逻辑需要再考虑下
    if ((memoryFlag & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
      VkMappedMemoryRange memRange;
      memRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      memRange.pNext = nullptr;
      memRange.memory = memory;
      memRange.offset = 0;
      memRange.size = dataSize;
      AVOX_VULKAN_LOG(vkFlushMappedMemoryRanges(vkDevice, 1, &memRange),
                     "flush memory failed");
    }
    vkUnmapMemory(vkDevice, memory);
    pData = nullptr;
  }
  AVOX_VULKAN_LOG(vkBindBufferMemory(vkDevice, buffer, memory, 0),
                 "bind buffer memory failed");

  descInfo.buffer = buffer;
  descInfo.offset = 0;
  descInfo.range = dataSize;

  if (usage == BufferUsage::store || usage == BufferUsage::onestore) {
    AVOX_VULKAN_LOG(
        vkMapMemory(vkDevice, memory, 0, dataSize, 0, (void **)&pData),
        "map memory failed");
  }
}

void VkWrapBuffer::initView(VkFormat viewFormat) {
  VkBufferViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
  viewInfo.pNext = nullptr;
  viewInfo.buffer = buffer;
  viewInfo.format = viewFormat;
  viewInfo.offset = 0;
  viewInfo.range = memoryRequires.size;
  AVOX_VULKAN_LOG(vkCreateBufferView(vkDevice, &viewInfo, nullptr, &view),
                 "create buffer view failed");
}

void VkWrapBuffer::upload(const uint8_t *cpuData) {
  upload(cpuData, bufferSize);
}

void VkWrapBuffer::upload(const uint8_t *cpuData, int32_t size) {
  if (pData == nullptr) {
    AVOX_VULKAN_LOG(
        vkMapMemory(vkDevice, memory, 0, bufferSize, 0, (void **)&pData),
        "map memory failed");
  }
  memcpy(pData, cpuData, size);
  // 告诉GPU，需要更新数据缓存 (如果是HOST_COHERENT则不需要)
  flush(false);
}

void VkWrapBuffer::download(uint8_t *cpuData) {
  if (pData == nullptr) {
    AVOX_VULKAN_LOG(
        vkMapMemory(vkDevice, memory, 0, bufferSize, 0, (void **)&pData),
        "map memory failed");
  }
  memcpy(cpuData, pData, bufferSize);
}

void VkWrapBuffer::submit() {
  if (pData) {
    vkUnmapMemory(vkDevice, memory);
    pData = nullptr;
  }
}

void VkWrapBuffer::flush(bool bRead) {
  // 在map中，并且只有usage是store才需要手动flush
  // 这种更新方式，虽然不是自动的，但是利用缓存能提高性能
  if (!pData || usage != BufferUsage::store) {
    return;
  }
  VkMappedMemoryRange memRange = {};
  memRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  memRange.pNext = nullptr;
  memRange.memory = memory;
  memRange.offset = 0;
  memRange.size = bufferSize;
  if (bRead) {
    AVOX_VULKAN_LOG(vkInvalidateMappedMemoryRanges(vkDevice, 1, &memRange),
                   "flush memory failed");
  } else {
    AVOX_VULKAN_LOG(vkFlushMappedMemoryRanges(vkDevice, 1, &memRange),
                   "flush memory failed");
  }
}

void VkWrapBuffer::release() {
  if (pData != nullptr) {
    vkUnmapMemory(vkDevice, memory);
    pData = nullptr;
  }
  if (view) {
    vkDestroyBufferView(vkDevice, view, nullptr);
    view = VK_NULL_HANDLE;
  }
  if (buffer) {
    vkDestroyBuffer(vkDevice, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
  }
  if (memory) {
    vkFreeMemory(vkDevice, memory, nullptr);
    buffer = VK_NULL_HANDLE;
  }
}

void VkWrapBuffer::addBarrier(VkCommandBuffer command,
                              VkPipelineStageFlags newStageFlags,
                              VkAccessFlags newAccessFlags) {
  VkPipelineStageFlags oldStageFlags = stageFlags;
  VkAccessFlags oldAccessFlags = accessFlags;

  VkBufferMemoryBarrier bufBarrier = {};
  bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

  bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bufBarrier.buffer = buffer;
  bufBarrier.offset = 0;
  bufBarrier.size = VK_WHOLE_SIZE;
  bufBarrier.srcAccessMask = oldAccessFlags;
  bufBarrier.dstAccessMask = newAccessFlags;
  vkCmdPipelineBarrier(command, oldStageFlags, newStageFlags, 0, 0, nullptr, 1,
                       &bufBarrier, 0, nullptr);
  stageFlags = newStageFlags;
  accessFlags = newAccessFlags;
}

}