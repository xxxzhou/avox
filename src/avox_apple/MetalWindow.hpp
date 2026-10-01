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
  // 直通实态: Metal 侧翻转在渲染器, 意愿位即实态(经 EDR 探测后置位)。
  // ⚠️ metalHdrPassthrough 是 namespace 级全局量, 多窗口/多实例会串
  // (方案按单窗口假设, 多窗口需求出现时改实例成员+原子)
  virtual bool hdrPassthroughActive() const override {
    return metalHdrPassthrough.load();
  }

protected:
  virtual void onChangeSize() override;

  // 宿主原始挂入对象(NSView 或层): 层本身无屏信息, 探 EDR 走它所在屏
  void* hostObject = nullptr;
};

}
