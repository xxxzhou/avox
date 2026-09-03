#pragma once

#include "../module/HighClock.hpp"
#include "DeviceManager.hpp"

namespace avox {

// 任何能产生视频帧的源
// 回调不要直接dispatch(&IVideoSourceOb),需要从onFrame走
// 因为onFrame会管理数据大小变化
class VideoSource : public TDeviceSource<IVideoSource, IVideoSourceOb> {
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

}