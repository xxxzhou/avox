#pragma once

#include "../VkContext.hpp"
#include "VkSharedImage.hpp"

namespace avox {

// 跨 VkDevice 渲染上下文
// 用于 VkInputLayer/VkOutputLayer 识别跨 Device 的 Vulkan 输入/输出
class VkSharedRender : public IVkRenderContext {
 public:
  VkSharedRender() = default;
  virtual ~VkSharedRender() = default;

  // IVkRenderContext
  VkCommandBuffer getCommandBuffer() override { return VK_NULL_HANDLE; }
  VkImage getTexture() override;
  ImageFormat getImageFormat() override;
  bool bSharedImage() const override { return true; }

  // 设置共享图像
  void setSharedImage(VkSharedImage* image) { sharedImage = image; }
  VkSharedImage* getSharedImage() const { return sharedImage; }

 private:
  VkSharedImage* sharedImage = nullptr;
};

}
