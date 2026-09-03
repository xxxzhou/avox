#include "VkContext.hpp"

#include "avox/module/AvoxManager.hpp"

namespace avox {

struct VkReg {
  VkReg() {
    if (volkInitialize() != VK_SUCCESS) {
      LOGFLF(LogLevel::warn, "volkInitialize failed");
    }
  }
};

// 模块初始化时调用volkInitialize
static VkReg vkRegStatic = {};

void VkContextRef::setVkContext(VkContextRef* ctx) {
  vkCtx = ctx;
  vkInstance = ctx->vkInstance;
  vkPhyDevice = ctx->vkPhyDevice;
  wphyDevcie = ctx->wphyDevcie;
  vkDevice = ctx->vkDevice;
  vkGraphicsQueue = ctx->vkGraphicsQueue;
  vkComputeQueue = ctx->vkComputeQueue;
  vkTransferQueue = ctx->vkTransferQueue;
  vkDecodeQueue = ctx->vkDecodeQueue;
  cmdPool = ctx->cmdPool;
  submitMtxPtr = ctx->submitMtxPtr;

  onSetVkContext();
}

VkDeviceArgs* VkContextRef::getDeviceArgs() {
  // 如果有引用
  if (vkCtx) {
    return vkCtx->getDeviceArgs();
  }
  return onGetDeviceArgs();
}

void VkContextRef::lockCommand() {
  if (submitMtxPtr) {
    submitMtxPtr->lock();
  }
}

void VkContextRef::unLockCommand() {
  if (submitMtxPtr) {
    submitMtxPtr->unlock();
  }
}

VkContext* VkContext::gVkContext = nullptr;
static std::mutex gVkContextMutex;

VkContext::VkContext(/* args */) {}

VkContext::~VkContext() {
  if (cmdPool) {
    vkDestroyCommandPool(vkDevice, cmdPool, 0);
    cmdPool = VK_NULL_HANDLE;
  }
  // 负责device/instance
  if (!bShardConext) {
    if (vkDevice) {
      vkDestroyDevice(vkDevice, nullptr);
      vkDevice = VK_NULL_HANDLE;
    }
    if (vkDebugMsg) {
      DestroyDebugUtilsMessengerEXT(vkInstance, vkDebugMsg, nullptr);
      vkDebugMsg = VK_NULL_HANDLE;
    }
    if (vkInstance) {
      vkDestroyInstance(vkInstance, nullptr);
      vkInstance = VK_NULL_HANDLE;
    }
  }
}

VkContext* VkContext::Shared() {
  std::lock_guard<std::mutex> lock(gVkContextMutex);
  if (gVkContext == nullptr) {
    gVkContext = new VkContext();
    gVkContext->initContext();
  }
  return gVkContext;
}

void VkContext::onDeviceComplete() {}

void VkContext::createCommandPool() {
  // 在vkDevice已经后
  if (devArgs.graphicsIndex >= 0) {
    vkGetDeviceQueue(vkDevice, devArgs.graphicsIndex, 0, &vkGraphicsQueue);
  }
  if (devArgs.computeIndex >= 0) {
    vkGetDeviceQueue(vkDevice, devArgs.computeIndex, 0, &vkComputeQueue);
    // context和呈现渲染相应command分开
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = devArgs.computeIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    AVOX_VULKAN_LOG(
        vkCreateCommandPool(vkDevice, &cmdPoolInfo, nullptr, &cmdPool),
        "create cmd pool failed");
  }
  if (devArgs.transferIndex >= 0) {
    vkGetDeviceQueue(vkDevice, devArgs.transferIndex, 0, &vkTransferQueue);
  }
  if (devArgs.decodeIndex >= 0) {
    vkGetDeviceQueue(vkDevice, devArgs.decodeIndex, 0, &vkDecodeQueue);
  }
  onDeviceComplete();
}

bool VkContext::createInstance(bool bDebug) {
  // 创建一个实例
  instArgs = VkInstanceArgs::defArgs(bDebug);
  vkInstance = instArgs.crateInstace();
  volkLoadInstance(vkInstance);
  if (bDebug) {
    createVkDebug(vkInstance, vkDebugMsg);
  }
  std::vector<VKPhysDevWrapper> physicalDevices;
  enumerateDevice(vkInstance, physicalDevices);
  if (physicalDevices.empty()) {
    return false;
  }
  bool find = false;
  // 首选独立显卡
  for (auto& pdevice : physicalDevices) {
    if (pdevice.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      phDevice = pdevice;
      find = true;
      break;
    }
  }
  if (!find) {
    int32_t index = std::min(gpuIndex, (int32_t)physicalDevices.size() - 1);
    phDevice = physicalDevices[index];
  }
  // 输出选择的GPU设备
  log(LogLevel::info, "vk instance select gpu:", phDevice.properties.deviceName,
      " device luid:", phDevice.luid);
  wphyDevcie = &phDevice;
  vkPhyDevice = phDevice.physicalDevice;
  return true;
}

bool VkContext::createDevice() {
  devArgs = VkDeviceArgs::defArgs(phDevice);
  vkDevice = devArgs.crateDevice(phDevice);
  if (vkDevice != VK_NULL_HANDLE) {
    volkLoadDevice(vkDevice);
  }
  return vkDevice != VK_NULL_HANDLE;
}

VkDeviceArgs* VkContext::onGetDeviceArgs() { return &devArgs; }

void VkContext::initContext() {
#if AVOX_DEBUG
  bVkDebug = true;
#endif
  createInstance(bVkDebug);
  createDevice();
  createCommandPool();
  submitMtxPtr = &submitMtx;
  bShardConext = false;
}

void VkContext::initContext(VkInstance instace_, VkPhysicalDevice phDevice_,
                            VkDevice device_) {
  vkInstance = instace_;
  vkPhyDevice = phDevice_;
  phDevice.form(phDevice_);
  wphyDevcie = &phDevice;
  devArgs = VkDeviceArgs::defArgs(phDevice);
  vkDevice = device_;
  if (vkDevice != VK_NULL_HANDLE) {
    volkLoadDevice(vkDevice);
  }
  submitMtxPtr = &submitMtx;
  createCommandPool();
  bShardConext = true;
}

bool canVulkan() {
#if AVOX_ENABLE_VULKAN
  // 结果在进程生命周期内不会变,缓存避免重复创建/销毁VkInstance/VkDevice
  static bool checked = false;
  static bool result = false;
  if (checked) return result;
  checked = true;
  // 1. 检查vulkan loader是否可用(VkReg静态初始化时已调用volkInitialize)
  // volkGetInstanceVersion返回0表示vulkan不可用,非0则loader已就绪
  if (volkGetInstanceVersion() == 0) {
    LOGFLF(LogLevel::warn, "canVulkan: vulkan loader not available");
    return false;
  }
  // 2. 尝试创建VkInstance
  VkInstanceArgs instArgs = VkInstanceArgs::defArgs(false);
  VkInstance instance = instArgs.crateInstace();
  if (instance == VK_NULL_HANDLE) {
    LOGFLF(LogLevel::warn, "canVulkan: create VkInstance failed");
    return false;
  }
  volkLoadInstance(instance);
  // 3. 检查是否有可用的物理设备
  std::vector<VKPhysDevWrapper> physicalDevices;
  enumerateDevice(instance, physicalDevices);
  if (physicalDevices.empty()) {
    LOGFLF(LogLevel::warn, "canVulkan: no vulkan physical device found");
    vkDestroyInstance(instance, nullptr);
    return false;
  }
  // 4. 尝试创建VkDevice(使用第一个物理设备)
  VKPhysDevWrapper& phDevice = physicalDevices[0];
  VkDeviceArgs devArgs = VkDeviceArgs::defArgs(phDevice);
  VkDevice device = devArgs.crateDevice(phDevice);
  if (device == VK_NULL_HANDLE) {
    LOGFLF(LogLevel::warn, "canVulkan: create VkDevice failed");
    vkDestroyInstance(instance, nullptr);
    return false;
  }
  // 检查通过,清理临时资源
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);
  result = true;
  return true;
#else
  return false;
#endif
}
}
