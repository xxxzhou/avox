#pragma once

#include <memory>
#include <mutex>

#include "../VkCommon.hpp"
#include "VkTexture.hpp"
#include "avox/video/Window.hpp"

#ifdef WIN32
#include "avox_windows/WinCommon.hpp"
#elif __ANDROID__
#include <android/native_activity.h>
#include <android_native_app_glue.h>
struct android_app;
#elif defined(__APPLE__)
#include "vulkan/vulkan_metal.h"
// #import <QuartzCore/CAMetalLayer.h>
// #import <Metal/Metal.h>
#elif defined(__ONLY_LINUX__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif


#include <functional>

namespace avox {

class VkWindow : public VkContextRef, public Window {
public:
  // preFram运行前确定,否则需要在运行后确定
  VkWindow();
  virtual ~VkWindow();

private:
  VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
  VkSwapchainKHR swapChain = VK_NULL_HANDLE;
  VkCommandPool cmdPool = VK_NULL_HANDLE;

  VkPipelineStageFlags submitPipelineStages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  // VkQueue graphicsQueue;
  VkQueue presentQueue = VK_NULL_HANDLE;
  VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
  VkRenderPassBeginInfo renderPassBeginInfo = {};
  VkViewport viewport = {};
  VkRect2D scissor = {};
  VkClearValue clearValues[2];
  bool focused = false;
  bool vsync = false;

public:
  // int32_t graphicsQueueIndex = UINT32_MAX;
  int32_t presentQueueIndex = UINT32_MAX;
  // VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  VkSurfaceFormatKHR format = {};
  VkFormat depthFormat = VK_FORMAT_D16_UNORM;
  VkRenderPass renderPass;

  uint32_t imageCount;
  uint32_t currentImage;
  std::vector<VkImage> images;
  std::unique_ptr<VkTexture> depthTex;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> frameBuffers;
  VkRenderContext renderContext = {};

  const int32_t frameCount = 2;
  int32_t currentFrame = 0;
  std::vector<VkCommandBuffer> cmdBuffers;
  // 用于动态添加执行列表
  std::vector<VkFence> addCmdFences;
  // 图像被获取,可以开始渲染
  std::vector<VkSemaphore> presentCompletes;
  // 图像已经渲染,可以呈现
  std::vector<VkSemaphore> renderCompletes;

protected:
  virtual void onInitWin() override;
  virtual bool onValidWin() override;
  virtual void onChangeSize() override;
  // virtual void onRunWin() override;
  virtual bool onPreTick() override;
  // 没有调用initWindow,直接用已有窗口initSurface,请在窗口的frame事件时调用
  virtual void onTickWin() override;
  virtual void onCloseWin() override;
  virtual IRenderContext *getRenderContext() override;

public:
  // 没有外部窗口,自己创建
#if _WIN32
  friend LRESULT handleMessage(HWND hWnd, UINT msg, WPARAM wparam,
                               LPARAM lparam);
  // 根据窗口创建surface,并返回使用的queueIndex.
  void initVkSurface(HINSTANCE inst, HWND windowHandle);
#endif
#ifdef __ANDROID__
  friend void handleAppCommand(android_app *app, int32_t cmd);
  void initVkSurface(ANativeWindow *window);
#endif
#ifdef __APPLE__
  void initVkSurface(CAMetalLayer *metalLayer);
#endif
#ifdef __ONLY_LINUX__
  void initVkSurface(ILinuxSurface* x11Surface);
#endif

private:
  void createSwipChain();

  void reSwapChainBefore();

  void reSwapChainAfter();

  void createRenderPass();
};
}
