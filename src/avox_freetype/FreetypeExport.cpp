#include "FreetypeExport.h"

#include "avox/video/WindowRender.hpp"
#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#endif

namespace avox {

IFontLayer* enableRenderFont(ISurfaceRender* render) {
  VkVideoRender* vkVideoRender = getVkVideoRender(render);
  if (!vkVideoRender) {
    return nullptr;
  }
  return vkVideoRender->enableRenderFont();
}

void disableRenderFont(ISurfaceRender* render) {
  VkVideoRender* vkVideoRender = getVkVideoRender(render);
  if (!vkVideoRender) {
    return;
  }
  vkVideoRender->disableRenderFont();
}

}
