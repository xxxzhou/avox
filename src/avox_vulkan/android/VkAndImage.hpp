#pragma once

#define VK_ANDROID_external_memory_android_hardware_buffer 1

#include "../VkContext.hpp"
#include "../VkHelper.hpp"
#include "../vulkan/VkTexture.hpp"
#include "avox_android/SharedGpuBuffer.hpp"

namespace avox {

// https://developer.android.com/ndk/reference/group/a-hardware-buffer
// AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM VK_FORMAT_R8G8B8A8_UNORM GL_RGBA8
// AHARDWAREBUFFER_FORMAT_S8_UINT VK_FORMAT_S8_UINT GL_STENCIL_INDEX8
class VkAndImage : public SharedGpuBuffer, public VkContextRef {
public:
  VkAndImage(/* args */);
  virtual ~VkAndImage();

private:
  VkImage vkImage = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

public:
  inline VkImage getImage() { return vkImage; }

private:
  // https://android.googlesource.com/platform/cts/+/master/tests/tests/graphics/jni/VulkanTestHelpers.cpp
  // 简化与兼容，限定都用RGBA格式
  void bindVK();

public:
  virtual void onInit() override;
  virtual void onRelease() override;
};

}