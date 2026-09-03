#include "VkWindow.hpp"

#include <array>

#include "avox/module/AvoxManager.hpp"

namespace avox {

#ifdef WIN32
LRESULT handleMessage(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  VkWindow* window =
      reinterpret_cast<VkWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  if (!window) {
    return DefWindowProc(hWnd, msg, wparam, lparam);
  }
  // switch (msg) {
  //   case WM_SIZE: {
  //     // 父窗口大小变化，更新子窗口大小
  //     if (window->hwnd && window->surface) {
  //       int newWidth = LOWORD(lparam);
  //       int newHeight = HIWORD(lparam);
  //       SetWindowPos((HWND)window->surface, NULL, 0, 0, newWidth, newHeight,
  //                    SWP_NOZORDER | SWP_NOACTIVATE);
  //     }
  //     break;
  //   }
  //   default:
  //     break;
  // }
  return DefWindowProc(hWnd, msg, wparam, lparam);
}
#endif

#ifdef __ANDROID__
void handleAppCommand(android_app* app, int32_t cmd) {
  assert(app->userData != NULL);
  VkWindow* window = reinterpret_cast<VkWindow*>(app->userData);
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      // The window is being shown, get it ready.
      log(LogLevel::info, "APP_CMD_INIT_WINDOW");
      window->initSurface(app->window);
      break;
    case APP_CMD_TERM_WINDOW:
      // The window is being hidden or closed, clean it up.
      break;
    case APP_CMD_WINDOW_RESIZED:
      log(LogLevel::info, "APP_CMD_WINDOW_RESIZED");
      window->onChangeSize();
      break;
    case APP_CMD_LOST_FOCUS:
      log(LogLevel::info, "APP_CMD_LOST_FOCUS");
      window->focused = false;
      break;
    case APP_CMD_GAINED_FOCUS:
      log(LogLevel::info, "APP_CMD_GAINED_FOCUS");
      window->focused = true;
      break;
    case APP_CMD_STOP:
      ANativeActivity_finish(app->activity);
      break;
    default:
      log(LogLevel::info, "event not handled: ", cmd);
  }
}
#endif

VkWindow::VkWindow() {
  setVkContext(VkContext::Shared());
  renderType = RenderType::Vulkan;
}

VkWindow::~VkWindow() { onCloseWin(); }

IRenderContext* VkWindow::getRenderContext() { return &renderContext; }

void VkWindow::onInitWin() {
// 创建窗口
#if _WIN32
  log(LogLevel::info, "create win32 window,width:", wdWidth,
      " height:", wdHeight);
  HMODULE wdInstance = GetModuleHandle(nullptr);
  surface = createWin32Window((HINSTANCE)wdInstance, hwnd, wdWidth, wdHeight,
                              wdTitle.c_str(), handleMessage, this);
  if (!surface) {
    log(LogLevel::warn, "win32 vk init surface is null");
    return;
  }
  // 得到合适的queueIndex
  initVkSurface((HINSTANCE)wdInstance, surface);
#endif
#ifdef __ANDROID__
  // 原生窗口,initSurface的时机需要在APP_CMD_INIT_WINDOW中
  if (AvoxManager::Get().getApp()) {
    AvoxManager::Get().getApp()->userData = this;
    AvoxManager::Get().getApp()->onAppCmd = handleAppCommand;
  } else {
    if (!surface) {
      log(LogLevel::warn, "android vk init surface is null");
      return;
    }
    // Android里Surface
    initVkSurface((ANativeWindow*)surface);
  }
#endif
#ifdef __APPLE__
  if (!surface) {
    log(LogLevel::warn, "ios vk init surface is null");
    return;
  }
  initVkSurface((CAMetalLayer*)surface);
#endif
#ifdef __ONLY_LINUX__
  if (!surface) {
    log(LogLevel::warn, "linux vk init surface is null");
    return;
  }
  initVkSurface((ILinuxSurface*)surface);
#endif
}

bool VkWindow::onValidWin() {
#ifdef __APPLE__
  if (AvoxManager::Get().getBackground()) {
    return false;
  }
#endif
  return vkSurface != nullptr;
}

bool VkWindow::onPreTick() {
  bool quit = false;
#ifdef WIN32
  MSG msg;
  // log(LogLevel::info, "msg.message begin");
  while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      quit = true;
      break;
    }
    // 当WM_NCLBUTTONDOWN 属于系统级消息,这个会卡住
    if (msg.message == WM_NCLBUTTONDOWN) {
      log(LogLevel::info, "msg.message:", "WM_NCLBUTTONDOWN");
      // 1. 检查 wParam 决定是否拦截
      if (msg.wParam == HTCAPTION) {
        // 自定义拖动逻辑
        SendMessage(surface, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        return 0;
      }
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
#endif
  if (wdWidth <= 0 || wdHeight <= 0) {
    return false;
  }
  return !quit;
}

void VkWindow::onTickWin() {
  if (!vkSurface) {
    return;
  }
  // HighClock clock = {};

  // 非阻塞检查 fence 状态，如果 GPU 还没完成，跳过这一帧
  VkResult result = vkGetFenceStatus(vkDevice, addCmdFences[currentFrame]);
  if (result == VK_NOT_READY) {
    // GPU 还在执行，跳过这一帧（不阻塞）
    return;
  }
  if (result != VK_SUCCESS) {
    AVOX_VULKAN_LOG(result, "vkGetFenceStatus");
    return;
  }

  vkResetFences(vkDevice, 1, &addCmdFences[currentFrame]);
  vkAcquireNextImageKHR(vkDevice, swapChain, UINT64_MAX,
                        presentCompletes[currentFrame], VK_NULL_HANDLE,
                        &currentImage);
  // log(LogLevel::info, "vk windows cost 1:", clock.recordLast());
  vkResetCommandBuffer(cmdBuffers[currentFrame], 0);
  vkBeginCommandBuffer(cmdBuffers[currentFrame], &cmdBufferBeginInfo);
  vkCmdSetViewport(cmdBuffers[currentFrame], 0, 1, &viewport);
  vkCmdSetScissor(cmdBuffers[currentFrame], 0, 1, &scissor);
  // renderPassBeginInfo.framebuffer = frameBuffers[currentFrame];
  // renderpass关联渲染目标 BlitImage不能包含在RenderPass里面
  // vkCmdBeginRenderPass(cmdBuffers[currentFrame],
  // &renderPassBeginInfo,
  //                      VK_SUBPASS_CONTENTS_INLINE);
  renderContext.setCommandBuffer(cmdBuffers[currentFrame]);
  renderContext.setTexture(images[currentImage]);
  renderContext.setImageFormat({wdWidth, wdHeight, 0, ImageType::argb8});

  // 如果没有观察者处理渲染，做默认布局转换
  if (empty()) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = images[currentImage];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmdBuffers[currentFrame],
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
  } else {
    dispatch(&IWindowOb::onRenderWindow);
  }

  // vkCmdEndRenderPass(cmdBuffers[currentFrame]);
  vkEndCommandBuffer(cmdBuffers[currentFrame]);
  // log(LogLevel::info, "vk windows cost 2:", clock.recordLast());
  VkSemaphore waitSemaphores[] = {presentCompletes[currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSubmitInfo submitInfo{};
  // 用于渲染队列里确定等待与发送信号
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.pWaitDstStageMask = &submitPipelineStages;
  submitInfo.waitSemaphoreCount = 1;
  // 用于指定队列开始执行前需要等待的信号量,以及需要等待的管线阶段
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.signalSemaphoreCount = 1;
  // 用于缓冲命令执行结束后发出信号的信号量对象
  VkSemaphore signalSemaphores[] = {renderCompletes[currentFrame]};
  submitInfo.pSignalSemaphores = signalSemaphores;
  // 提交缓冲区命令,执行完后发送信号给renderComplete
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmdBuffers[currentFrame];
  // 等presentComplete收到信号后执行,执行完成后发送信号renderComplete
  // 集显或是手机GPU，其vkComputeQueue/presentQueue可能是同一个
  // 不同线程对同一队列vkQueueSubmit/vkQueuePresentKHR,需要同步
  lockCommand();
  result =
      vkQueueSubmit(presentQueue, 1, &submitInfo, addCmdFences[currentFrame]);
  AVOX_VULKAN_LOG(result, "submit compute queue failed");
  // log(LogLevel::info, "vk windows cost 3:", clock.recordLast());
  //  提交渲染呈现到屏幕
  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.pNext = nullptr;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapChain;
  presentInfo.pImageIndices = &currentImage;
  // 等待renderComplete
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.waitSemaphoreCount = 1;
  // 提交渲染呈现到屏幕
  result = vkQueuePresentKHR(presentQueue, &presentInfo);
  unLockCommand();
  // log(LogLevel::info, "vk windows cost 4:", clock.recordLast());
  AVOX_VULKAN_LOG(result, "vkQueuePresentKHR");
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    // onChangeSize();
  }
  currentFrame = (currentFrame + 1) % frameCount;
}

void VkWindow::onCloseWin() {
  if (vkDevice == VK_NULL_HANDLE) {
    return;
  }
  // 等待设备空闲
  vkDeviceWaitIdle(vkDevice);
  // 销毁交换链相关资源
  if (swapChain) {
    for (uint32_t i = 0; i < imageCount; i++) {
      vkDestroyImageView(vkDevice, views[i], nullptr);
      vkDestroyFramebuffer(vkDevice, frameBuffers[i], nullptr);
    }
    vkDestroySwapchainKHR(vkDevice, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }
  // 销毁命令池
  if (cmdPool) {
    vkDestroyCommandPool(vkDevice, cmdPool, nullptr);
    cmdPool = VK_NULL_HANDLE;
  }
  if (presentCompletes.size() == frameCount) {
    // 销毁同步对象
    for (int32_t i = 0; i < frameCount; i++) {
      vkDestroySemaphore(vkDevice, presentCompletes[i], nullptr);
      vkDestroySemaphore(vkDevice, renderCompletes[i], nullptr);
      vkDestroyFence(vkDevice, addCmdFences[i], nullptr);
    }
  }
  if (vkSurface) {
    vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
    vkSurface = VK_NULL_HANDLE;
  }
}

void VkWindow::onChangeSize() {
  if (wdHeight == 0 || wdWidth == 0) {
    return;
  }
  VkResult res = vkDeviceWaitIdle(vkDevice);
  if (res == VK_ERROR_DEVICE_LOST) {
    return;
  }
  createSwipChain();
  vkDeviceWaitIdle(vkDevice);
}

#if defined(_WIN32)
void VkWindow::initVkSurface(HINSTANCE inst, HWND windowHandle)
#elif defined(__ANDROID__)
void VkWindow::initVkSurface(ANativeWindow* window)
#elif defined(__APPLE__)
void VkWindow::initVkSurface(CAMetalLayer* layer)
#elif defined(__ONLY_LINUX__)
void VkWindow::initVkSurface(ILinuxSurface* x11Surface)
#endif
{
  VkResult ret = VK_SUCCESS;
  LOGFLF(LogLevel::info, "vk init surface");
#if defined(_WIN32)
  VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
  surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceCreateInfo.hinstance = inst;
  surfaceCreateInfo.hwnd = (HWND)windowHandle;
  AVOX_VULKAN_LOG(vkCreateWin32SurfaceKHR(vkInstance, &surfaceCreateInfo,
                                         nullptr, &vkSurface),
                 "create win32 surface failed");
#elif defined(__ANDROID__)
  VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
  surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  surfaceCreateInfo.window = window;
  surfaceCreateInfo.flags = 0;
  surfaceCreateInfo.pNext = nullptr;
  ret = vkCreateAndroidSurfaceKHR(vkInstance, &surfaceCreateInfo, NULL,
                                  &vkSurface);
#elif defined(__APPLE__)
  VkMetalSurfaceCreateInfoEXT surfaceCreateInfo = {};
  surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  surfaceCreateInfo.pNext = nullptr;
  surfaceCreateInfo.pLayer = layer;
  ret = vkCreateMetalSurfaceEXT(vkInstance, &surfaceCreateInfo, nullptr,
                                &vkSurface);
#elif defined(__ONLY_LINUX__)
  VkXlibSurfaceCreateInfoKHR x11SurfaceCreateInfo = {};
  x11SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  x11SurfaceCreateInfo.dpy = (Display*)x11Surface->getDisplay();
  x11SurfaceCreateInfo.window = (::Window)(uintptr_t)x11Surface->getWindow();
  ret = vkCreateXlibSurfaceKHR(vkInstance, &x11SurfaceCreateInfo, nullptr,
                               &vkSurface);
#endif
  if (ret != VK_SUCCESS) {
    LOGFLF(LogLevel::error, "failed to create surface");
    return;
  }
  bool bfind = wphyDevcie->findSurfaceQueue(
      vkSurface, getDeviceArgs()->graphicsIndex, presentQueueIndex);
  assert(presentQueueIndex >= 0);
  if (!bfind) {
    LOGFLF(LogLevel::warn, "presentIndex not equal graphicsIndex");
  }
  // 查找surf支持的显示格式
  uint32_t formatCount;
  AVOX_VULKAN_LOG(vkGetPhysicalDeviceSurfaceFormatsKHR(vkPhyDevice, vkSurface,
                                                      &formatCount, nullptr),
                 "get surface format failed");
  std::vector<VkSurfaceFormatKHR> surfFormats(formatCount);
  AVOX_VULKAN_LOG(vkGetPhysicalDeviceSurfaceFormatsKHR(
                     vkPhyDevice, vkSurface, &formatCount, surfFormats.data()),
                 "get surface format failed");
  if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
    format = surfFormats[0];
    format.format = VK_FORMAT_B8G8R8A8_UNORM;
  } else {
    assert(formatCount >= 1);
    bool bfind = false;
    for (auto& surf : surfFormats) {
      if (surf.format == VK_FORMAT_B8G8R8A8_UNORM ||
          surf.format == VK_FORMAT_R8G8B8A8_UNORM) {
        format = surf;
        bfind = true;
        break;
      }
    }
    if (!bfind) {
      format = surfFormats[0];
    }
  }
  // 得到当前使用的queue
  vkGetDeviceQueue(vkDevice, presentQueueIndex, 0, &presentQueue);
  // cmdPool
  VkCommandPoolCreateInfo cmdPoolInfo = {};
  cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolInfo.queueFamilyIndex = presentQueueIndex;
  cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  AVOX_VULKAN_LOG(vkCreateCommandPool(vkDevice, &cmdPoolInfo, nullptr, &cmdPool),
                 "create cmd pool failed");
  // command buffer
  cmdBuffers.resize(frameCount);
  VkCommandBufferAllocateInfo cmdBufInfo = {};
  cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufInfo.commandPool = cmdPool;
  cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufInfo.commandBufferCount = (uint32_t)cmdBuffers.size();
  AVOX_VULKAN_LOG(
      vkAllocateCommandBuffers(vkDevice, &cmdBufInfo, cmdBuffers.data()),
      "allocate cmd buffer failed");
  // sync objects
  addCmdFences.resize(frameCount);
  presentCompletes.resize(frameCount);
  renderCompletes.resize(frameCount);
  for (uint32_t i = 0; i < frameCount; i++) {
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // 默认是有信号
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(vkDevice, &fenceInfo, nullptr, &addCmdFences[i]);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &presentCompletes[i]);
    vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &renderCompletes[i]);
  }
  // 创建窗口用的renderpass
  createRenderPass();
  createSwipChain();
}

void VkWindow::createSwipChain() {
  // 得到imagecount
  reSwapChainBefore();
  // 创建swapchain以及对应的image
  reSwapChainAfter();
}

void VkWindow::reSwapChainBefore() {
  VkSwapchainKHR oldSwapchain = swapChain;
  // Get physical device surface properties and formats
  VkSurfaceCapabilitiesKHR surfCapabilities;
  AVOX_VULKAN_LOG(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                     vkPhyDevice, vkSurface, &surfCapabilities),
                 "get surface capabilities failed");
  // 得到交换链支持的所有Mode
  uint32_t presentModeCount;
  AVOX_VULKAN_LOG(vkGetPhysicalDeviceSurfacePresentModesKHR(
                     vkPhyDevice, vkSurface, &presentModeCount, nullptr),
                 "get present mode failed");
  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  AVOX_VULKAN_LOG(
      vkGetPhysicalDeviceSurfacePresentModesKHR(
          vkPhyDevice, vkSurface, &presentModeCount, presentModes.data()),
      "get present mode failed");

  VkExtent2D swapchainExtent = {};
  // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
  if (surfCapabilities.currentExtent.width == 0xFFFFFFFF) {
    // If the surface size is undefined, the size is set to
    // the size of the images requested.
    swapchainExtent.width = wdWidth;
    swapchainExtent.height = wdHeight;
    if (swapchainExtent.width < surfCapabilities.minImageExtent.width) {
      swapchainExtent.width = surfCapabilities.minImageExtent.width;
    } else if (swapchainExtent.width > surfCapabilities.maxImageExtent.width) {
      swapchainExtent.width = surfCapabilities.maxImageExtent.width;
    }

    if (swapchainExtent.height < surfCapabilities.minImageExtent.height) {
      swapchainExtent.height = surfCapabilities.minImageExtent.height;
    } else if (swapchainExtent.height >
               surfCapabilities.maxImageExtent.height) {
      swapchainExtent.height = surfCapabilities.maxImageExtent.height;
    }
  } else {
    // If the surface size is defined, the swap chain size must match
    swapchainExtent = surfCapabilities.currentExtent;
  }
  // 得到交换链要求的长宽,surface大小变动后,要重新获得
  wdWidth = swapchainExtent.width;
  wdHeight = swapchainExtent.height;
  // present需要支持FIFO
  VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
  if (!vsync) {
    for (size_t i = 0; i < presentModeCount; i++) {
      if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
        swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
      if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
          (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      }
    }
  }
  uint32_t desiredNumberOfSwapchainImages = surfCapabilities.minImageCount + 1;
  if ((surfCapabilities.maxImageCount > 0) &&
      (desiredNumberOfSwapchainImages > surfCapabilities.maxImageCount)) {
    desiredNumberOfSwapchainImages = surfCapabilities.maxImageCount;
  }
  // Find the transformation of the surface
  VkSurfaceTransformFlagsKHR preTransform =
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  if (surfCapabilities.supportedTransforms &
      VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    // We prefer a non-rotated transform
    preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    preTransform = surfCapabilities.currentTransform;
  }
  // Find a supported composite alpha mode - one of these is guaranteed to be
  // set
  VkCompositeAlphaFlagBitsKHR compositeAlpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[4] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (uint32_t i = 0;
       i < sizeof(compositeAlphaFlags) / sizeof(compositeAlphaFlags[0]); i++) {
    if (surfCapabilities.supportedCompositeAlpha & compositeAlphaFlags[i]) {
      compositeAlpha = compositeAlphaFlags[i];
      break;
    }
  }
  // crate swapchain
  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.pNext = nullptr;
  swapchainInfo.surface = vkSurface;
  swapchainInfo.minImageCount = desiredNumberOfSwapchainImages;
  swapchainInfo.imageFormat = format.format;
  swapchainInfo.imageColorSpace = format.colorSpace;
  swapchainInfo.imageExtent = {swapchainExtent.width, swapchainExtent.height};
  swapchainInfo.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
  swapchainInfo.compositeAlpha = compositeAlpha;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.presentMode = swapchainPresentMode;
  swapchainInfo.oldSwapchain = oldSwapchain;
  swapchainInfo.clipped = VK_FALSE;
  swapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.queueFamilyIndexCount = 0;
  swapchainInfo.pQueueFamilyIndices = nullptr;
  // 如果二个不同队列,我们需要CONCURRENT共享不同队列传输
  int graphicsQueueIndex = getDeviceArgs()->graphicsIndex;
  if (graphicsQueueIndex != presentQueueIndex) {
    uint32_t queueFamilyIndices[2] = {(uint32_t)graphicsQueueIndex,
                                      (uint32_t)presentQueueIndex};
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchainInfo.queueFamilyIndexCount = 2;
    swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
  }
  AVOX_VULKAN_LOG(
      vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &swapChain),
      "create swapchain failed");
  // 自动销毁老的资源,重新生成
  depthTex = std::make_unique<VkTexture>();
  depthTex->setVkContext(this);
  depthTex->InitResource(wdWidth, wdHeight, depthFormat,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  // 销毁老的资源
  if (oldSwapchain != VK_NULL_HANDLE) {
    for (uint32_t i = 0; i < imageCount; i++) {
      vkDestroyImageView(vkDevice, views[i], nullptr);
      vkDestroyFramebuffer(vkDevice, frameBuffers[i], nullptr);
    }
    vkDestroySwapchainKHR(vkDevice, oldSwapchain, nullptr);
    oldSwapchain = VK_NULL_HANDLE;
    views.clear();
    frameBuffers.clear();
  }
  AVOX_VULKAN_LOG(
      vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr),
      "get swapchain images failed");
  images.resize(imageCount);
  AVOX_VULKAN_LOG(
      vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, images.data()),
      "get swapchain images failed");
  views.resize(imageCount);
  frameBuffers.resize(imageCount);
}

void VkWindow::reSwapChainAfter() {
  // 创建swap image view
  VkImageViewCreateInfo colorAttachmentView = {};
  colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  colorAttachmentView.pNext = nullptr;
  colorAttachmentView.format = format.format;
  colorAttachmentView.components = {
      VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A};
  colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  colorAttachmentView.subresourceRange.baseMipLevel = 0;
  colorAttachmentView.subresourceRange.levelCount = 1;
  colorAttachmentView.subresourceRange.baseArrayLayer = 0;
  colorAttachmentView.subresourceRange.layerCount = 1;
  colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
  colorAttachmentView.flags = 0;

  VkImageView attachments[2];
  attachments[1] = depthTex->view;
  // 创建FBO
  VkFramebufferCreateInfo frameBufferCreateInfo = {};
  frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  frameBufferCreateInfo.pNext = NULL;
  frameBufferCreateInfo.renderPass = renderPass;
  frameBufferCreateInfo.attachmentCount = 2;
  // 需要满足renderpass创建指定的VkAttachmentDescription
  frameBufferCreateInfo.pAttachments = attachments;
  frameBufferCreateInfo.width = wdWidth;
  frameBufferCreateInfo.height = wdHeight;
  frameBufferCreateInfo.layers = 1;
  for (uint32_t i = 0; i < imageCount; i++) {
    colorAttachmentView.image = images[i];
    AVOX_VULKAN_LOG(
        vkCreateImageView(vkDevice, &colorAttachmentView, nullptr, &views[i]),
        "create image view failed");
    attachments[0] = views[i];
    AVOX_VULKAN_LOG(vkCreateFramebuffer(vkDevice, &frameBufferCreateInfo,
                                       nullptr, &frameBuffers[i]),
                   "create framebuffer failed");
  }
  // 因大小变化,重新组合相应buffer
  //  创建cmdBufferBeginInfo
  cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  // 创建renderpassbeginInfo
  // VkClearValue clearValues[2];
  clearValues[0].color = {{0.0f, 1.0f, 0.0f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBeginInfo.renderPass = renderPass;
  renderPassBeginInfo.renderArea.extent = {(uint32_t)wdWidth,
                                           (uint32_t)wdHeight};
  renderPassBeginInfo.clearValueCount = 2;
  renderPassBeginInfo.pClearValues = clearValues;
  // viewport
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.width = static_cast<float>(wdWidth);
  viewport.height = static_cast<float>(wdHeight);
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;
  // scissor
  scissor.extent = {(uint32_t)wdWidth, (uint32_t)wdHeight};
  scissor.offset = {0, 0};
}

void VkWindow::createRenderPass() {
  std::array<VkAttachmentDescription, 2> attachments = {};
  // Color attachment
  attachments[0].format = format.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // VK_ATTACHMENT_LOAD_OP_CLEAR VK_ATTACHMENT_LOAD_OP_DONT_CARE
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  // Depth attachment
  attachments[1].format = depthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorReference = {};
  colorReference.attachment = 0;
  colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthReference = {};
  depthReference.attachment = 1;
  depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpassDescription = {};
  subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpassDescription.colorAttachmentCount = 1;
  subpassDescription.pColorAttachments = &colorReference;
  subpassDescription.pDepthStencilAttachment = &depthReference;
  subpassDescription.inputAttachmentCount = 0;
  subpassDescription.pInputAttachments = nullptr;
  subpassDescription.preserveAttachmentCount = 0;
  subpassDescription.pPreserveAttachments = nullptr;
  subpassDescription.pResolveAttachments = nullptr;

  // Subpass dependencies for layout transitions
  std::array<VkSubpassDependency, 2> dependencies;

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpassDescription;
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  AVOX_VULKAN_LOG(
      vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &renderPass),
      "create render pass failed");
}

}

// } else if (androidApp && androidApp->activity) {
//   // 如果使用原生窗口
//   int ident;
//   int events;
//   struct android_poll_source *source;
//   bool destroy = false;

//   focused = true;
//   while ((ident = ALooper_pollOnce(focused ? 0 : -1, NULL, &events,
//                                    (void **)&source)) >= 0) {
//     if (source != NULL) {
//       source->process(androidApp, source);
//     }
//     if (androidApp->destroyRequested != 0) {
//       log(LogLevel::info, "Android app destroy requested");
//       destroy = true;
//       break;
//     }
//   }
//   // App destruction requested
//   // Exit loop, example will be destroyed in application main
//   if (destroy) {
//     ANativeActivity_finish(androidApp->activity);
//     break;
//   }
//   if (!empty()) {
//     tick();
//   }
