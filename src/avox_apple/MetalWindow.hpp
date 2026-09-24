#pragma once

#include "avox/video/Window.hpp"
#include <QuartzCore/QuartzCore.h>
#include <atomic>

namespace avox {

// forceHDR 直通态(块3 Apple 腿): setHdrPassthrough 过 EDR 探测后置位,
// MetalRender 建管线/配宿主层时读取对齐
extern std::atomic<bool> metalHdrPassthrough;

class MetalWindow : public Window {
public:
  MetalWindow();
  virtual ~MetalWindow();

  // EDR 屏受理并返回 true; SDR 屏恒 no-op 返回 false。关断恒受理
  virtual bool setHdrPassthrough(bool bPassthrough) override;
  virtual void initSurface(void* surface) override;

protected:
  virtual void onChangeSize() override;

  // 宿主原始挂入对象(NSView 或层): 层本身无屏信息, 探 EDR 走它所在屏
  void* hostObject = nullptr;
};

}
