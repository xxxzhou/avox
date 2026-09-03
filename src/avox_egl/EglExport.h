#pragma once

#include "avox/AvoxLayer.h"

namespace avox {

extern "C" {
// 使用调用者的EGL上下文
AVOX_EXPORT IRenderContext* createGLESContext(int64_t sharedCtx);
// 更新纹理,对应于Android的SurfaceTexture的updateTexImage方法
AVOX_EXPORT void updateGLESContextTexture(IRenderContext* context, int32_t image,
  ImageFormat format);
// electron中,抓到当前EGL对应线程上下文绑定的纹理
// 然后把ISurfaceRender的NT SharedTexture传给这个纹理
// AVOX_EXPORT int32_t renderEglTexture(ISurfaceRender* render);
}

}