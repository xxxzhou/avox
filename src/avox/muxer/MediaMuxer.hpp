#pragma once

#include "../source/AVSource.hpp"
#include "IOMuxer.hpp"

namespace avox {

using RDOB = Observer<IRecorderOb>;

// 媒体封装器,用于封装已经编码后的acc/h264/h265数据
// 创建MediaMuxer对象的类需要实现IRecorderOb接口
// 在IRecorderOb::onStateChange回调中获取状态
// 原本是把AVSource和设置AvSource,由AvSource来驱动
// 但是上层需要同时知道AVSource和MediaMuxer状态并控制
class MediaMuxer : public IMediaMuxer, public Observer<IRecorderOb> {
 public:
  MediaMuxer();
  virtual ~MediaMuxer();

 protected:
  // 把编码后的数据写入到网络流或是文件中
  std::unique_ptr<IOMuxer> ioMuxer = nullptr;
  MuxerType muxerType = MuxerType::none;
  RecorderState state = RecorderState::none;
  // 进度
  RecorderProgress progress = {};
  // 上次派发进度的时间,节流避免高频回调
  int64_t lastProgressTimeMs = -100;
  // 打开通知上层对象,由上层对象设置
  IMuxerOb* muxerOb = nullptr;
  // 媒体复用的地址
  std::string url = "";
  // 是否有视频
  bool bHaveAudio = false;
  // 是否有音频
  bool bHaveVideo = false;
  // 有视频，但是不写入视频流，只能在open前设置
  bool bDisableVideo = false;
  // 有音频，但是不写入音频流，只能在open前设置
  bool bDisableAudio = false;
  // 开/关同步
  std::mutex ocMtx;
  // 检查是否annexb/avcc
  bool bCheckAcc = false;
  // 是否是avcc/hvcc包
  bool bvcc = false;
  VCodecId vcodecId = VCodecId::none;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  // I帧/P帧合并包
  std::vector<AvoxPacket> combineBufs;
  // 配置帧之前都不要
  bool bStartPacket = false;

 public:
  void setRecState(RecorderState newState);
  virtual void setMuxerType(MuxerType type) override;
  // 设视频编码,none=丢弃视频轨(disable)
  virtual void setVideoCodec(VCodecId codecId) override;
  // 设音频编码,none=丢弃音频轨(disable)
  virtual void setAudioCodec(ACodecId codecId) override;
  // 可能是附加在mediaplayer/sourceplayer上
  // 由player提供相应的音频/视频信息
  void setContext(IMuxerOb* context) { muxerOb = context; }
  virtual RecorderState getState() override { return state; }
  virtual bool open(const char* url) override;
  virtual void onOpen() {};
  virtual void close() override;
  virtual void onClose() {};
  // 在IMuxerOb中的onMuxerOpen回调设置
  virtual void setInVideoDesc(const VTrackDesc& desc);
  virtual void setInAudioDesc(const ATrackDesc& desc);
  void ready();
  void pushPacket(const AvoxPacket& packet);
  // 设置总时长(ms),用于onProgress的totalTimeMs
  void setDuration(int64_t durationMs) { progress.totalTimeMs = durationMs; }
  // IOMuxer处理包时回调当前pts(ms),用于派发onProgress
  void onProgress(int64_t ptsMs);
  void singleVideo(AvoxPacket& packet);
};

}
