#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "GLESContext.hpp"
#include "avox/video/Window.hpp"

namespace avox {

// 用于Android硬解直接渲染到gl窗口
// 需要用到SurfaceTexture本身的Surface得到队列
// 需要对应的渲染纹理用于具体渲染
// 需要相应的SurfaceTexture通知渲染时机
class EglWindow : public Window {
public:
  EglWindow();
  virtual ~EglWindow() {};

protected:
  virtual void onChangeSize() override;
};

}