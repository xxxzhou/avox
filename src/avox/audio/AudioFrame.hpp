#pragma once

#include <cassert>
#include <memory>

#include "../AvoxPlayer.h"
#include "../module/AvBuffer.hpp"

namespace avox {

using AudioFramePtr = std::shared_ptr<class AudioFrame>;

class AudioFrame {
 public:
  AudioFrame() = default;
  ~AudioFrame() = default;

 public:
  // 显示时间
  int64_t pts = 0;

 protected:
  std::vector<uint8_t> buffer;
  // 已经写入数据
  int32_t wcount = 0;

 public:
  void setPts(int64_t pts_) { pts = pts_; }
  int64_t getPts() const { return pts; }

  void setSize(int32_t size) {
    wcount = 0;
    buffer.resize(size);
  }
  int32_t getSize() const { return buffer.size(); }
  uint8_t* point() { return buffer.data(); }
  const uint8_t* point() const { return buffer.data(); }
  int32_t spaceLeft() { return buffer.size() - wcount; }
  void clear() { wcount = 0; }
  void writeBytes(const uint8_t* data, int32_t size) {
    if (!data || size <= 0) return;
    int32_t space = spaceLeft();
    assert(size <= space);  // 调试期暴露越界调用
    // Release 下显式钳位: 只写剩余空间, 不再裸 memcpy 越界
    int32_t n = size < space ? size : space;
    memcpy(buffer.data() + wcount, data, n);
    wcount += n;
  }
  void form(const AudioFrame& frame) {
    pts = frame.pts;
    wcount = frame.wcount;
    buffer.resize(frame.getSize());
    memcpy(buffer.data(), frame.point(), frame.getSize());
  }
};

inline void copyAudioFrame(AudioFramePtr& frame, const AudioFrame& curframe) {
  // 槽内帧可能正被消费线程读取(队列 dequeue 是锁外处理), 独占才原地复用,
  // 否则换新对象; 边读边写会让解码帧被下一帧覆写成断音/爆音
  if (!frame || frame.use_count() > 1) {
    frame = std::make_shared<AudioFrame>();
  }
  // form resize大部分情况没有变化，因为解码后帧大小一般不会改动
  frame->form(curframe);
}

inline void copyAudioBuf(AudioFramePtr& frame, const AvoxAFrame& curframe) {
  // 同上: TranscodeRecorder 的帧队列也是锁外消费, 复用前必须确认独占
  if (!frame || frame.use_count() > 1) {
    frame = std::make_shared<AudioFrame>();
  }
  frame->setSize(curframe.buffer.size);
  frame->writeBytes(curframe.buffer.data, curframe.buffer.size);
  frame->setPts(curframe.pts);
}

}