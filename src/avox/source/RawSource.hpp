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

 public:
  // 在open之后调用
  void setVideoDesc(const VideoDesc& desc);
  void setAudioDesc(const AudioDesc& desc);
  void checkTrackReady();
  // 在音频与视频都设置完成后，为true
  bool readying() { return bReady; }

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