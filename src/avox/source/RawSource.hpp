#pragma once

#include "../video/VideoRender.hpp"
#include "BaseSource.hpp"

namespace avox {

struct RawSourceDesc {
  // 方案名
  std::string name = "";
};

// 有原始PCM/YUV/GPUTexture数据就可当做一个源
// 可以用来转发处理后的PCM/YUV/GPUTexture到编码器
class AVOX_EXPORT RawSource : public BaseSource,
                             public IRawSource,
                             public Observer<IRawSourceOb> {
 public:
  RawSource();
  virtual ~RawSource();

 protected:
  // 音频与视频的回调可能在不同线程
  // 二边线程访问同一资源同步锁
  std::mutex avMtx;
  // 确认音频与视频都准备好了
  bool bReady = false;
  // setVideoDesc 建轨时并入的容器级字段(display matrix 旋转)
  int32_t pendingRotation = 0;

 public:
  // 在open之后调用
  void setVideoDesc(const VideoDesc& desc);
  // 容器级字段(display matrix 旋转)只补这一项: 整条 VTrackDesc 下传会把源编码
  // 带进 muxer(RawMuxer 拿 desc.codecId 当输出编码)。须在 setVideoDesc 之前调,
  // 建轨时一并落值(ready 回调在 setVideoDesc 内同步派发)。
  void setVideoRotation(int32_t rotation);
  void setAudioDesc(const AudioDesc& desc);
  void checkTrackReady();
  // 在音频与视频都设置完成后，为true
  bool readying() { return bReady; }
  // 打开后按协商结果修正期望媒体(如webrtc对端只发视频), 绕过bOpen守卫, 内部重查ready
  void updateExpectVideo(bool bExpect);
  void updateExpectAudio(bool bExpect);

 protected:
  // 如果开了音频与视频，需要等待音频与视频都ready
  virtual void onTrackOpen() override;

 public:
  virtual bool open() override;
  virtual void close() override;
  // 查询是否打开
  virtual bool bOpening() override;
  // 在调用open，回调onOpen后能获取到正确信息
  virtual ISourceInfo* getSourceInfo() override;

 public:
  void pushFrame(const YUVFrame& frame);
  void pushFrame(const GpuFrame& frame);
  void pushFrame(const AvoxAFrame& frame);
};

}