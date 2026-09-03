#pragma once

#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"
#include "avox_egl/EglExport.h"
namespace avox {

extern "C" {
// format指的是ANDROID_BITMAP_FORMAT
ImageType getBitmapType(int32_t format);
// 使用VkVideoRender/EglVideoRender渲染
void renderContext(ISurfaceRender* wrender, IRenderContext* context);
}
}