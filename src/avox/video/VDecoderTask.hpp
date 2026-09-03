#pragma once

#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "VideoDecoder.hpp"

namespace avox {

// 原VideoDecoder与MediaPlayer绑定比较深
// 主要开启线程从VideoTrack读取IO队列,解码给VideoTrack
// 原来与VideoTrack绑定的部分移到这里
// 而VideoDecoder只负责输入数据，解码数据，输出数据
class VDecoderTask : public IPlayerContext, public RunTask {
public:
  VDecoderTask();
  virtual ~VDecoderTask();

protected:
  // 视频元信息
  VideoDesc srcDesc = {};
  VCodecId codecId = VCodecId::none;
  PBVideoInfo pbInfo = {};
  // 解码类型
  VCodecTh codecTh = VCodecTh::cpu;
  // 是否是硬解码
  bool bHardDecode = false;
  // 解码器允许的延迟,单位ms
  int32_t delayMs = 5000;
  // 重置标志位(切换软硬解等)
  bool bResetFlag = false;
  // 如果重置解码器，需要保持上个解码器里的配置帧
  std::vector<PacketBuf> configPackets;
  // 配置帧变化
  bool bDecodeUpdate = false;
  std::mutex configMutex;

protected:
  class VideoTrack *trackContext = nullptr;
  std::unique_ptr<VideoDecoder> decode = nullptr;

public:
  bool start(class VideoTrack *trackContext);
  void setResetFlag() { bResetFlag = true; }
  void flush();
  void close();

public:
  // 根据解码器类型选择渲染器
  RenderType selectRenderType();
  VideoDecoder *getDecoder() { return decode.get(); }
  void addConfigRecord(ConfigAddType type);
  bool hardDecode() { return bHardDecode; }

  // RunTask
protected:
  virtual void onRunTask() override;
};

}
