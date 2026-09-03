#pragma once

#include "../VkContext.hpp"
namespace avox {

#define AVOX_MAP_VK_YCBCR_FORMAT(XX) \
  XX(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_FORMAT_R8_UNORM, YCBCRA_8BPP)      \
  XX(h265, 2, "h265")


}
