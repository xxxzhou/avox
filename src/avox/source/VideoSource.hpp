#pragma once

#include "../module/HighClock.hpp"
#include "DeviceManager.hpp"

namespace avox {

// 任何能产生视频帧的源
// 回调不要直接dispatch(&IVideoSourceOb),需要从onFrame走
// 因为onFrame会管理数据大小变化
class AVOX_EXPORT VideoSource : public TDeviceSource<IVideoSource, IVideoSourceOb> {
 public:
  VideoSource() = default;
  virtual ~VideoSource() = default;

 protected:
  VideoDesc curDesc = {};
  // 设备本身支持的视频格式列表
  std::vector<VideoDesc> descList = {};
  bool bSizeChanged = false;
  //
  bool bOpen = false;
  bool bBack = false;
  VDeviceKind deviceKind = VDeviceKind::none;

 public:
  // 设备来源类别
  virtual VDeviceKind getDeviceKind() override { return deviceKind; }
  // 设置到curDesc中，由支持的格式列表中选择设定最相近的
  void setVideoDesc(int32_t width, int32_t height, int32_t fps = 20);

 public:
  void onFrame(const YUVFrame& frame);
  void onFrame(const GpuFrame& frame);
  bool checkSizeChanged(const YUVFormat& format);
};

// 视频源占用登记(进程级, 跨模块): 采集会话 add 时打标/stop 时清除,
// 平台设备管理器刷新时对占用中的源对象 pin 住不替换
// (同一源被两个会话同时 open 会互相覆盖内部采集资源, 必须拒绝)
AVOX_EXPORT bool markVideoSourceBusy(void* source);
AVOX_EXPORT void unmarkVideoSourceBusy(void* source);
AVOX_EXPORT bool isVideoSourceBusy(void* source);

}
