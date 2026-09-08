#pragma once

#include "avox/AvoxLayer.h"
#include "VkHelper.hpp"
#include <atomic>
#include <mutex>

namespace avox {

struct VKPhysDevWrapper;
struct VkDeviceArgs;
struct VkInstanceArgs;

// 当类需要Vk上下文,免的从别的对象引入大量赋值
class VkContextRef {
 public:
  VkContextRef() {};
  virtual ~VkContextRef() {};

 protected:
  VkContextRef* vkCtx = nullptr;
  VKPhysDevWrapper* wphyDevcie = nullptr;

  // Vk实例/设备/队列
  VkInstance vkInstance = VK_NULL_HANDLE;
  VkPhysicalDevice vkPhyDevice = VK_NULL_HANDLE;
  VkDevice vkDevice = VK_NULL_HANDLE;
  // VkDevice选择计算管线
  VkQueue vkComputeQueue = VK_NULL_HANDLE;
  VkQueue vkGraphicsQueue = VK_NULL_HANDLE;
  VkQueue vkTransferQueue = VK_NULL_HANDLE;
  VkQueue vkDecodeQueue = VK_NULL_HANDLE;
  // 命令池cmdpool
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  // 多线程command执行，command需要保持同步
  std::mutex* submitMtxPtr = nullptr;

 protected:
  virtual VkDeviceArgs* onGetDeviceArgs() { return nullptr; }

 protected:
  virtual void onSetVkContext() {};

 public:
  void setVkContext(VkContextRef* ctx);
  VkDeviceArgs* getDeviceArgs();

  void lockCommand();
  void unLockCommand();
  // 等待GPU所有命令执行完,销毁与GPU相关的资源前必须先调
  void waitIdle() {
    if (vkDevice != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(vkDevice);
    }
  }
};

// 计算管线/解码管线，从这继承会帮助初始化
// 提供VkInstance/VkPhysicalDevice/VkDevice/VkQueue
// 也可使用另外的库提供的这几个对象
class VkContext : public VkContextRef {
 public:
  VkContext(/* args */);
  virtual ~VkContext();

 public:
  // 当前模块如果不想申请新的上下文，那使用共享的Vk上下文
  // 此非单例，只是默认给一个可以全局使用的
  static VkContext* gVkContext;
  static VkContext* Shared();

 public:
  // 设备丢失恢复: 共享VkDevice被驱动重置(TDR等)后, 重建device并让消费方
  // 按devEpoch发现句柄过期, 重拉句柄+走既有reset路径重建管线
  enum class VkDevState { Ok, Recovering, Dead };
  // 恢复门判定结果: Skip=恢复中/已放弃,本帧跳过; Ok=正常; Rebuild=设备已重建
  enum class VkRecoverGate { Skip, Ok, Rebuild };
  static VkDevState devState();
  static uint32_t devEpoch();
  // 任意层submit/alloc返回VK_ERROR_DEVICE_LOST时调用(幂等, 单飞恢复+退避重试)
  static void markLost();
  // 消费方每帧入口调用, myEpoch存有自己的代际:
  // 返回Rebuild时调用方需重拉句柄(如setVkContext)+重建自有资源+重建管线
  static VkRecoverGate recoverGate(uint32_t& myEpoch);

 protected:
  bool bShardConext = false;
  // 创建Vk实例参数
  VkInstanceArgs instArgs = {};
  // Inter集显有多核心，多核心在这可以指定
  int32_t gpuIndex = 0;
  // 物理显卡
  VKPhysDevWrapper phDevice = {};
  // 创建VkDevice参数
  VkDeviceArgs devArgs = {};
  // Queue提交到GPU中，Queue需要保持同步
  std::mutex submitMtx;

 protected:
  // 是否显示调试信息
  bool bVkDebug = false;
  VkDebugUtilsMessengerEXT vkDebugMsg = VK_NULL_HANDLE;

 protected:
  void onDeviceComplete();

 private:
  void createCommandPool();

 protected:
  // 创建一个实例
  bool createInstance(bool bDebug = false);
  // 创建一个VK设备
  bool createDevice();
  virtual VkDeviceArgs* onGetDeviceArgs() override;

 public:
  // 初始化管线
  void initContext();
  // 共享别的VK上下文
  void initContext(VkInstance instace, VkPhysicalDevice phDevice,
                   VkDevice device);
};

class IVkRenderContext : public IRenderContext {
 public:
  IVkRenderContext() = default;
  virtual ~IVkRenderContext() = default;

  // IRenderContext
 public:
  virtual RenderType getRenderType() override { return RenderType::Vulkan; }

 public:
  virtual VkCommandBuffer getCommandBuffer() = 0;
  virtual VkImage getTexture() = 0;
  virtual ImageFormat getImageFormat() = 0;
  // 是否跨 VkDevice 共享图像(用于区分同 Device 窗口渲染和跨 Device 输出)
  virtual bool bSharedImage() const { return false; }
};

class VkRenderContext : public IVkRenderContext {
 public:
  VkRenderContext() = default;
  virtual ~VkRenderContext() = default;

 protected:
  VkCommandBuffer vkCmd = VK_NULL_HANDLE;
  VkImage vkImage = VK_NULL_HANDLE;
  ImageFormat vkFormat = {};

 public:
  virtual VkCommandBuffer getCommandBuffer() override { return vkCmd; };
  virtual VkImage getTexture() override { return vkImage; };
  virtual ImageFormat getImageFormat() override { return vkFormat; };

 public:
  void setCommandBuffer(VkCommandBuffer cmd) { vkCmd = cmd; }
  void setTexture(VkImage image) { vkImage = image; }
  void setImageFormat(const ImageFormat& format) { vkFormat = format; }
};

}