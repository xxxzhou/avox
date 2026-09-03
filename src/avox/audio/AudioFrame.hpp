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
    assert(size + wcount <= buffer.size());
    memcpy(buffer.data() + wcount, data, size);
    wcount += size;
  }
  void form(const AudioFrame& frame) {
    pts = frame.pts;
    wcount = frame.wcount;
    buffer.resize(frame.getSize());
    memcpy(buffer.data(), frame.point(), frame.getSize());
  }
};

inline void copyAudioFrame(AudioFramePtr& frame, const AudioFrame& curframe) {
  if (!frame) {
    frame = std::make_shared<AudioFrame>();
  }
  // form resize大部分情况没有变化，因为解码后帧大小一般不会改动
  frame->form(curframe);
}

inline void copyAudioBuf(AudioFramePtr& frame, const AvoxAFrame& curframe) {
  if (!frame) {
    frame = std::make_shared<AudioFrame>();
  }
  frame->setSize(curframe.buffer.size);
  frame->writeBytes(curframe.buffer.data, curframe.buffer.size);
  frame->setPts(curframe.pts);
}

}