#include "AudioReshaper.hpp"

#include "../player/Player.hpp"

namespace avox {

bool AudioReshaper::initConfig(const AudioDesc& src, AudioDesc& out) {
  srcDesc = src;
  outDesc = out;
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  resample = std::make_unique<FFResample>();
  if (!resample->init(srcDesc, outDesc)) {
    return false;
  }
#endif
  frameSize = getAudioFrameSize(outDesc, frameMs);
  // 默认10ms的buffer
  curFrame.setSize(frameSize);
  curFrame.setPts(AVOX_NOVALID_PTS);
  pbuffer.resize(frameSize);
  return true;
}

void AudioReshaper::process(const AvoxAFrame& frame) {
  uint8_t* data = frame.buffer.data;
  int32_t size = frame.buffer.size;
#ifdef AVOX_ENABLE_FFMPEG
  if (!resample) {
    LOGFLF(LogLevel::warn, "no ffmpeg resample");
    return;
  }
  AvoxData inData = {data, size, true};
  int ret = resample->resample(inData);
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "resample failed");
    return;
  }
  data = inData.data;
  size = ret;
#endif
  // frame的开始pts
  int64_t spts = frame.pts;
  // 第一次进来，初始化当前包的时间戳
  if (curFrame.getPts() == AVOX_NOVALID_PTS) {
    curFrame.setPts(spts);
  }
  // 写入数据,数据每次组成固定bufferMs的长度bufferSize
  while (size > 0) {
    // 当前BUFFER剩余空间
    int32_t spaceleft = curFrame.spaceLeft();
    if (size >= spaceleft) {
      // 写入当前BUFFER并写满
      curFrame.writeBytes(data, spaceleft);
      // 通知子类处理
      onProcess();
      // 开始新的curFrame
      curFrame.clear();
      // 下一帧的开始时间
      spts += getAudioFrameMs(outDesc, spaceleft);
      curFrame.setPts(spts);
      // 剩余数据继续
      data += spaceleft;
      size -= spaceleft;
    } else {
      // 写入当前BUFFER未满
      curFrame.writeBytes(data, size);
      size = 0;
    }
  }
}

void AudioReshaper::flush() {
  // 排干 swr 内部残余(filter delay 缓存的样本), 复用 process 的写帧逻辑
#ifdef AVOX_ENABLE_FFMPEG
  if (resample) {
    AvoxData drainOut = {};
    int32_t n = resample->flush(drainOut);
    if (n > 0 && drainOut.data) {
      const uint8_t* data = drainOut.data;
      int32_t size = n;
      while (size > 0) {
        int32_t spaceleft = curFrame.spaceLeft();
        if (size >= spaceleft) {
          curFrame.writeBytes(data, spaceleft);
          onProcess();
          curFrame.clear();
          data += spaceleft;
          size -= spaceleft;
        } else {
          curFrame.writeBytes(data, size);
          size = 0;
        }
      }
    }
  }
#endif
  // curFrame 里没满的残余作为最后一帧吐出 (onProcess 只读 point/getSize, 不要求满)
  if (curFrame.spaceLeft() < frameSize) {
    onProcess();
    curFrame.clear();
  }
}

}