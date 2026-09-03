#pragma once
#include <mutex>

#include "avox/layer/VOutputLayer.hpp"
#ifdef WIN32
#include "../windows/VkWinImage.hpp"
#elif __ANDROID__
#include "../android/VkAndImage.hpp"
#elif __APPLE__
#include "../ios/VkIosImage.hpp"
#endif
#include "../share/VkSharedImage.hpp"
#include "../vulkan/VkCommand.hpp"
#include "VkLayer.hpp"

namespace avox {

class VkOutputLayer : public VOutputLayer, public VkLayer {
  AVOX_LAYER_GETNAME(VkOutputLayer)
 public:
  VkOutputLayer(/* args */);
  virtual ~VkOutputLayer();

 private:
  // CPU输出使用
  std::unique_ptr<VkWrapBuffer> outBuffer = nullptr;
  // enableImage: 用户持有的外部输出 buffer (零拷贝引用 staging, 非拥有)
  IImageBuffer* userOutBuffer = nullptr;

#ifdef WIN32
  std::unique_ptr<VkWinImage> winImage = nullptr;
  bool bWinInterop = false;
#elif __ANDROID__
  std::unique_ptr<VkAndImage> vkAndImage = nullptr;
  bool bAndInterop = false;
#elif __APPLE__
  std::unique_ptr<VkIosImage> vkIosImage = nullptr;
  bool bIosInterop = true;
#endif
  // VkDevice-VkDevice 交互
  std::unique_ptr<VkSharedImage> sharedImage = nullptr;
  bool bVkInterop = false;
  bool bPendingRelease = false;
  // 用作记录GPU输出大小
  ImageFormat outFormat = {};
  // 管线重构与渲染RHI线程同步
  std::mutex mtx;
  std::unique_ptr<VkCommand> vkCommand = nullptr;
  // buffer里最小对齐,webgpu要求256
  int32_t kMinAlign = 16;
  ImageFormat patchFormat = {};
  // 默认显示帧长宽比
  float aspect = 0.0f;
  // 记录变化
  bool baspectChange = false;
  VkCommandBuffer winCmd = nullptr;
  int32_t clearCount = 3;

 protected:
  virtual void onInitGraph() override;
  virtual void onUpdateParamet() override;
  virtual void onInitVkBuffer() override;
  virtual void onInitPipe() override {};
  virtual void onCommand() override;
  virtual bool onFrame() override;
  virtual void onUnInit() override;

  // IVOutputLayer
 public:
  virtual void outputGpuData(IRenderContext* context) override;
  virtual bool fetchData(IImageBuffer* buffer) override;

 public:
  void* getOutGpuBuffer();
  // 以特定长宽比显示
  void setAspect(float aspect);
  // VkDevice-VkDevice 交互访问
  VkSharedImage* getSharedImage() const { return sharedImage.get(); }
  void setVkInterop(bool bEnable) {
    if (bVkInterop != bEnable) {
      bVkInterop = bEnable;
      // 标志变化需重录 command buffer,使 interop 拷贝路径生效/失效
      resetGraph();
    }
  }
  void requestRelease() { bPendingRelease = true; }
  const ImageFormat& getOutFormat() const { return outFormat; }
  // 设置外部输出 buffer: 每帧零拷贝引用 staging 映射内存写入它 (enableImage 用)
  // buf 由调用方持有, 生命周期需维持到 disableImage/setOutputBuffer(nullptr) 之后
  void setOutputBuffer(IImageBuffer* buf) { userOutBuffer = buf; }
};

}
