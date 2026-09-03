#include "AudioEncoder.hpp"

#include "../player/Player.hpp"

namespace avox {

AudioEncoder::AudioEncoder() {}

void AudioEncoder::setOutDesc(const AudioDesc& desc) { outDesc = desc; }

ATrackDesc AudioEncoder::setDesc(const ATrackDesc& desc_) {
  desc = desc_;
  enDesc = desc;
  if (outDesc.channels == 0) {
    outDesc.channels = desc.desc.channels;
  }
  if (outDesc.sampleRate == 0) {
    outDesc.sampleRate = desc.desc.sampleRate;
  }
  if (outDesc.format == AudioFormat::other) {
    outDesc.format = desc.desc.format;
  }
  enDesc.desc = getSupportDesc(outDesc);
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样
  resample = std::make_unique<FFResample>();
  resample->init(desc.desc, enDesc.desc);
#endif
  return enDesc;
}

AudioDesc AudioEncoder::getSupportDesc(const AudioDesc& desc) { return desc; }

DecodeResult AudioEncoder::fillFrame(const AvoxAFrame& frame) {
  uint8_t* data = frame.buffer.data;
  int32_t size = frame.buffer.size;
#ifdef AVOX_ENABLE_FFMPEG
  AvoxData inData = {data, size, true};
  int ret = resample->resample(inData);
  if (ret < 0) {
    return DecodeResult::dataError;
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
      // 因为如mp4/rtsp都要求pts是递增的,所以要修正这种情况
      // 原因主要是录制时，100ms的数据，可能因为buffer，直接10/200ms来的
      // 这样实际间隔与数据间隔不一致，spts计算就不匹配，android录音频繁
      if (curFrame.getPts() <= lastPts) {
        // 修正这种情况
        spts = lastPts + 1;
        curFrame.setPts(spts);
      }
      lastPts = curFrame.getPts();
      // 填充了一帧数据，开始解码
      DecodeResult dr = encode();
      if (dr == DecodeResult::complete) {
        return dr;
      }
      // 开始新的curFrame
      curFrame.clear();
      // 下一帧的开始时间
      spts += getAudioFrameMs(enDesc.desc, spaceleft);
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
  return DecodeResult::success;
}

}
