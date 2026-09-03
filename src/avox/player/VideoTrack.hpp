#pragma once

#include "../module/AvBuffer.hpp"
#include "../video/VDecoderTask.hpp"
#include "../video/VideoDecoder.hpp"
#include "../video/VideoFrame.hpp"
#include "../video/VideoRender.hpp"
#include "../video/Window.hpp"
#include "../video/WindowRender.hpp"
#include "AVTrack.hpp"

namespace avox {

using VideoTrackPtr = std::shared_ptr<class VideoTrack>;

// window窗口不强求与MediaPlayer同一线程处理，相反还复杂
// window直接在Track中同步渲染对象
class VideoTrack : public TAVTrack<VideoFramePtr>,
                   public IVideoDecoderOb,
                   public FrameSourceOb {
 public:
  VideoTrack();
  virtual ~VideoTrack();

 protected:
  // 解码器线程
  std::unique_ptr<VDecoderTask> decodeTask = nullptr;
  // 视频+窗口渲染
  std::unique_ptr<WindowRender> windowRender = nullptr;

  VCodecId codecId = VCodecId::none;
  VideoDesc srcDesc = {};

  // 解码类型
  VCodecTh codecTh = VCodecTh::cpu;
  // 是否是硬解码
  bool bHardDecode = false;

  int32_t maxDropCount = 5;
  int64_t dropDuration = 100;

 public:
  RingBuffer<VideoFramePtr>& getFrameQueue() { return frameQueue; }
  VCodecId getCodecId() { return codecId; }
  VideoDesc getDesc() { return srcDesc; }

  // IVideoDecoderOb
 public:
  // 解码器成功构造后回调
  virtual void onVideoDesc() override;
  // 解码器开始解码前的第一个包
  virtual void onPacket(PacketBufPtr packet) override;
  virtual void onDecode(const YUVFrame& frame) override;
  virtual void onDecodeGpu(const GpuFrame& frame) override;
  virtual void onVideoComplete() override;

 public:
  double getFps();

  // FrameSourceOb
 public:
  virtual void onWinUpdate() override;

 public:
  WindowRender* getSurfaceRender();
  // 告知解码器准备重置，真实重置需要等到I帧包前
  void setResetDecoderFlag();
  // 解码器因重置flag关闭后，回调给Track
  void onResetDecoder();

 public:
  void setTrackDesc(const VTrackDesc& trackDesc);
  void start();
  // 计算需要的延迟
  int64_t computeDelay(int64_t delay);
  // 同步视频 -1 视频帧慢了,丢弃,无效帧 0 同步帧 1 视频帧快了
  SyncResult syncVideo();
  // 渲染与时钟暂停
  void pauseRender(bool pause);
  // 解码暂停
  void pauseDecoder(bool pause);
  // 当队列数据无效时，需要清除队列中的数据
  void flush();
  void updateSeekTime(int64_t seekTime);
  // 关闭解码与渲染对象
  void close();
};

}