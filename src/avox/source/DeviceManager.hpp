#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../AvoxSource.h"
#include "../module/Observer.hpp"

namespace avox {

// 换Observer代码比较相似，用宏替代
#define AVOX_CHANGE_OBSERVER(oldObj, newObj) \
  if (newObj == oldObj) {                   \
    return;                                 \
  }                                         \
  if (oldObj) {                             \
    oldObj->removeObserver(this);           \
  }                                         \
  oldObj = newObj;                          \
  if (oldObj) {                             \
    oldObj->addObserver(this);              \
  }

#define AVOX_REMOVE_OBSERVER(oldObj, newObj) \
  if (newObj == oldObj) {                   \
    return;                                 \
  }                                         \
  if (oldObj) {                             \
    oldObj->removeObserver(this);           \
  }                                         \
  oldObj = newObj;

struct DeviceDesc {
  std::string name;
};

template <typename I, typename Ob>
class TDeviceSource : public I, public Observer<Ob> {
 public:
  TDeviceSource() = default;
  virtual ~TDeviceSource() = default;

 protected:
  std::string deviceName = "";
  std::string deviceId = "";
  bool bFirstFrame = false;

 public:
  // 打开
  virtual bool open() override {
    bFirstFrame = false;
    return onOpen();
  }
  // 关闭
  virtual void close() override { onClose(); }
  virtual bool bOpening() override { return false; }
  virtual const char* getDeviceId() override {
    if (!deviceId.empty()) {
      return deviceId.c_str();
    }
    return deviceName.c_str();
  }
  virtual const char* getDeviceName() override {
    if (!deviceName.empty()) {
      return deviceName.c_str();
    }
    return deviceId.c_str();
  }

 public:
  // 是否第一次弹出 onVideoDesc 回调,图像大小每次变化都会回调
  bool bFirstDesc() { return bFirstFrame; }

 protected:
  virtual bool onOpen() { return false; }
  virtual void onClose() {}
};

// I为IAudioManager/IVideoManager
// 设备管理T为IAudioSource/IVideoSource
template <typename I, typename T>
class DeviceManager : public I {
 public:
  DeviceManager() = default;
  virtual ~DeviceManager() = default;

 protected:
  typedef std::shared_ptr<T> DevicePtr;
  std::vector<DevicePtr> devices;

 public:
  T* getTDevice(int32_t index) {
    if (index < 0 || index >= devices.size()) {
      return nullptr;
    }
    return devices[index].get();
  }
  int32_t getIndex(const char* id) {
    int32_t index = -1;
    for (int32_t i = 0; i < devices.size(); i++) {
      DevicePtr device = devices[i];
      if (!device) {
        continue;
      }
      if (equalsIgnoreCase(id, device->getDeviceId())) {
        index = i;
        break;
      }
    }
    return index;
  }

 public:
  virtual int32_t getDeviceCount() override { return devices.size(); }
  virtual void refreshDevices() override { onRefreshDevices(); }

 protected:
  // SDK初始化时直接获取所有设备，由SDK内部回调维护增删
  virtual void onInitDevices() {};
  // 有些SDK源实时性要求高，在需要时手动刷新
  virtual void onRefreshDevices() {};
  // SDK退出时，释放所有设备
  virtual void onDeInitDevices() {};
};

template <typename T>
class AudioManager : public DeviceManager<IAudioManager, T> {
 public:
  AudioManager() {}
  virtual ~AudioManager() = default;

 protected:
  ADeviceSdk sdkType = ADeviceSdk::none;

 public:
  ADeviceSdk getSdkType() { return sdkType; }
  virtual IAudioSource* getDevice(int32_t index) override {
    T* device = this->getTDevice(index);
    if (!device) {
      return nullptr;
    }
    return device;
  }
  virtual IAudioSource* findDevice(const char* id) override {
    int32_t index = this->getIndex(id);
    return getDevice(index);
  }
};

template <typename T>
class VideoManager : public DeviceManager<IVideoManager, T> {
 public:
  VideoManager() {}
  virtual ~VideoManager() = default;

 protected:
  VDeviceSdk sdkType = VDeviceSdk::none;

 public:
  VDeviceSdk getSdkType() { return sdkType; }
  virtual IVideoSource* getDevice(int32_t index) override {
    T* device = this->getTDevice(index);
    if (!device) {
      return nullptr;
    }
    return device;
  }
  virtual IVideoSource* findDevice(const char* id) override {
    int32_t index = this->getIndex(id);
    return getDevice(index);
  }
};

}