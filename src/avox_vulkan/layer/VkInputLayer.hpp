#pragma once

#include "avox/layer/VInputLayer.hpp"
#ifdef WIN32
#include "../windows/VkWinImage.hpp"
#elif __ANDROID__
#include "../android/VkAndImage.hpp"
#elif __APPLE__
#include "../ios/VkIosImage.hpp"
#endif
#include "../share/VkSharedImage.hpp"
#include "VkLayer.hpp"

namespace avox {

// 把各种VideoFormat转化成ImageFormat,主要二种,R8/RGBA8
// VkInputLayer不支持CPU/GPU同时输入,否则会忽视CPU输入
class VkInputLayer : public VInputLayer, public VkLayer {
  AVOX_LAYER_GETNAME(VkInputLayer)

 private:
  std::unique_ptr<VkWrapBuffer> inBuffer = nullptr;
  // 如果需要GPU计算,需要先把inBuffer copy 到 gpu local
  std::unique_ptr<VkWrapBuffer> inBufferX = nullptr;
  // 是否需要GPU计算
  bool bUsePipe = false;
  // 如windows设定只支持RGBA
  bool bGPUFormat = false;
#ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<VkAndImage> vkAndImage = nullptr;
  uint32_t textureId = 0;
  bool bAndInterop = false;
#elif __APPLE__
  std::unique_ptr<VkIosImage> vkIosImage = nullptr;
  bool bIosInterop = false;
#endif
  // VkDevice-VkDevice 交互
  std::unique_ptr<VkSharedImage> sharedImage = nullptr;
  bool bVkInterop = false;
  bool bPendingRelease = false;

 public:
  VkInputLayer(/* args */);
  virtual ~VkInputLayer();

  // VkDevice-VkDevice 交互访问
 public:
  VkSharedImage* getSharedImage() const { return sharedImage.get(); }
  void setVkInterop(bool bEnable) {
    if (bVkInterop != bEnable) {
      bVkInterop = bEnable;
      // 标志变化需重录 command buffer,使 interop 拷贝路径生效/失效
      resetGraph();
    }
  }
  void requestRelease() { bPendingRelease = true; }

  // InputLayer
 public:
  virtual void inputGpuData(IRenderContext* context) override;

  // VInputLayer
 protected:
  virtual void onFormatChange() override;

  // VkLayer
 protected:
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
  virtual void onInitPipe() override;
  virtual void onCommand() override;
  virtual bool onFrame() override;

  virtual void onUnInit() override;
};

}