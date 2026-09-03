#include "VkSharedRender.hpp"

namespace avox {

VkImage VkSharedRender::getTexture() {
  if (sharedImage) {
    return sharedImage->getImage();
  }
  return VK_NULL_HANDLE;
}

ImageFormat VkSharedRender::getImageFormat() {
  ImageFormat format = {};
  if (sharedImage) {
    const auto& desc = sharedImage->getDesc();
    format.width = desc.width;
    format.height = desc.height;
    // VkFormat → ImageType 的简单映射
    if (desc.format == VK_FORMAT_R8G8B8A8_UNORM) {
      format.imageType = ImageType::rgba8;
    } else if (desc.format == VK_FORMAT_B8G8R8A8_UNORM) {
      format.imageType = ImageType::bgra8;
    } else if (desc.format == VK_FORMAT_R8_UNORM) {
      format.imageType = ImageType::r8;
    }
  }
  return format;
}

}
