#include "VkContext.hpp"

#include <chrono>
#include <thread>

#include "avox/module/AvoxManager.hpp"

namespace avox {

// 设备丢失恢复状态(进程内共享一台VkDevice, 见Shared())
static std::atomic<VkContext::VkDevState> sVkDevState{VkContext::VkDevState::Ok};
static std::atomic<uint32_t> sVkDevEpoch{0};
static std::atomic<int32_t> sVkRecoverAttempts{0};

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

VkContext::VkDevState VkContext::devState() { return sVkDevState.load(); }

uint32_t VkContext::devEpoch() { return sVkDevEpoch.load(); }

void VkContext::markLost() {
  VkDevState expect = VkDevState::Ok;
  // 只有Ok态才触发一次恢复; Recovering=已在恢复, Dead=已放弃等上层降级
  if (!sVkDevState.compare_exchange_strong(expect, VkDevState::Recovering)) {
    return;
  }
  LOGFLF(LogLevel::warn, "vk device lost, start recovery, attempt:",
         sVkRecoverAttempts.load() + 1);
  std::thread([] {
    while (true) {
      int32_t attempt = sVkRecoverAttempts.fetch_add(1);
      if (attempt >= 6) {
        sVkDevState.store(VkDevState::Dead);
        LOGFLF(LogLevel::error,
               "vk device recovery failed, render degraded, restart client to "
               "recover");
        return;
      }
      // 驱动重置需要时间, 退避后重试: 1s,2s,4s,8s,16s,16s
      int64_t waitMs = 500ll << std::min(attempt + 1, 5);
      std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
      bool ok = false;
      {
        std::lock_guard<std::mutex> lock(gVkContextMutex);
        if (gVkContext && !gVkContext->bShardConext) {
          // 旧设备已lost: 跳过waitIdle(会返回DEVICE_LOST), 直接销毁重建
          gVkContext->createDevice();
          gVkContext->createCommandPool();
          ok = gVkContext->vkDevice != VK_NULL_HANDLE;
        }
      }
      if (ok) {
        sVkDevEpoch.fetch_add(1);
        sVkDevState.store(VkDevState::Ok);
        sVkRecoverAttempts.store(0);
        LOGFLF(LogLevel::warn, "vk device recovered, epoch:",
               sVkDevEpoch.load());
        return;
      }
      LOGFLF(LogLevel::warn, "vk device recovery failed, attempt:", attempt + 1);
    }
  }).detach();
}

VkContext::VkRecoverGate VkContext::recoverGate(uint32_t& myEpoch) {
  VkDevState st = sVkDevState.load();
  if (st != VkDevState::Ok) {
    return VkRecoverGate::Skip;
  }
  uint32_t cur = sVkDevEpoch.load();
  if (myEpoch == cur) {
    return VkRecoverGate::Ok;
  }
  myEpoch = cur;
  return VkRecoverGate::Rebuild;
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
  // 丢失恢复不变式: 刻意不销毁旧vkDevice, 丢失期间所有旧句柄调用仍合法; 勿补destroy否则恢复门变UAF, 旧device每episode泄漏一个属取舍
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
