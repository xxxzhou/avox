#pragma once

// IMediaReader 的 FFmpeg 实现(v1): 同步执行, 回调在调用线程派发。
// open/buildIndex/grabFrame/close 须同线程串行; cancel() 可跨线程打断。
// P1 落地 open/probe + buildIndex; P2 grabFrame 走 VK 离屏 RGBA 出图

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "MediaSourceInfo.hpp"
#include "avox/AvoxMedia.h"

struct AVFormatContext;

namespace avox {

class MediaThumbRenderer;

class MediaReaderFF : public IMediaReader {
 public:
  MediaReaderFF();
  virtual ~MediaReaderFF();

  // IMediaReader
 public:
  virtual void setObserver(IMediaReaderOb* ob) override;
  virtual bool open(const char* uri) override;
  virtual bool buildIndex(const MediaIndexOptions& options) override;
  virtual bool grabFrame(const MediaFrameRequest& request) override;
  virtual void cancel() override;
  virtual void close() override;
  virtual ISourceInfo* getSourceInfo() override;
  virtual MediaError lastError() override;
  virtual const char* lastErrorMessage() override;

 private:
  // avio interrupt_callback: cancel 标志置位即打断 in-flight 读
  static int interruptCb(void* opaque);
  bool interruptIo() const { return bCancel.load(); }
  // 状态迁移(onStateChange 同步派发); 迁移表见 MediaReaderUtil
  void notifyState(MediaReaderState from, MediaReaderState to);
  // 请求被接受后的失败收尾: 记 lastError + onError
  void reportError(MediaOp op, MediaError code, const std::string& message);
  // 请求未被接受: 只记 lastError, 不迁移状态不回调
  bool reject(MediaError code, const std::string& message);
  // open 主体: 建上下文(装 interrupt)→open_input→find_stream_info→填轨道
  bool openInput(const char* uri);
  // 逐 stream 填 ISourceInfo(probe 列全口径, 仅跳过 attached_pic)
  void fillSourceInfo();
  // 索引主体: 一趟 av_read_frame, 音频解码算峰值 + 关键帧收集 + 时长回退
  bool buildIndexImpl(const MediaIndexOptions& options);
  // 抓帧主体: 选轨→seek→解码到目标→VK 离屏出 RGBA→onFrame
  bool grabFrameImpl(const MediaFrameRequest& request);
  void closeInput();

 private:
  IMediaReaderOb* observer = nullptr;
  MediaReaderState state = MediaReaderState::idle;
  MediaError errorCode = MediaError::none;
  std::string errorMessage;
  std::atomic<bool> bCancel{false};
  AVFormatContext* fmtCtx = nullptr;
  std::unique_ptr<MediaSourceInfo> sourceInfo;
  // onIndexDone 结果存储(peaks/keyframesMs 指针只在回调内有效)
  std::vector<float> indexPeaks;
  std::vector<int64_t> indexKeyframes;
  // 抓帧渲染器(懒创建, 同一 reader 复用以省 VK 图重建)
  std::unique_ptr<MediaThumbRenderer> thumbRenderer;
};

}  // namespace avox
