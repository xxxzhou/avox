#pragma once

#include "SurfaceRenderVk.hpp"

namespace avox {

// 静态图像渲染器 - 显示单张图片,不用RunTask循环
// 继承SurfaceRenderVk获得VkVideoRender + ISurfaceRender接口
// 额外持有Window用于窗口创建和呈现
// 调用render(IImageBuffer*)时触发单次渲染+窗口呈现
class AVOX_EXPORT ImageRender : public SurfaceRenderVk,
                               public IImageRender,
                               public IWindowOb {
 public:
  ImageRender();
  virtual ~ImageRender();

 private:
  // 窗口呈现: 获取swapchain, blit输出纹理, present
  void presentWindow();

 protected:
  std::unique_ptr<Window> window = nullptr;

 public:
  // IImageRender
  virtual ISurfaceRender* getSurfaceRender() override;
  virtual void render(IImageBuffer* imageBuffer) override;
  virtual void render(const YUVFrame& frame) override;

  // ISurfaceRender 覆盖(添加Window管理)
  virtual void setSurface(void* surface) override;
  virtual void* getSurface() override;
  virtual void setAutoAspect(bool bEnable) override;

  // IWindowOb
 public:
  virtual void onRenderWindow() override;
};

}
