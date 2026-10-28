#include "AudioEncoder.hpp"

#include <algorithm>
#include <cstring>

#include "../player/Player.hpp"

namespace avox {

AudioEncoder::AudioEncoder() {}

void AudioEncoder::setOutDesc(const AudioDesc& desc) { outDesc = desc; }

int32_t AudioEncoder::getTargetBitrate() const {
  // 用编码器实际协商后的输出格式(下混/重采样之后)算自动档
  return resolveAudioBitrate(encodeParam, enDesc.desc);
}

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
  bPlane = bAPlaneFormat(enDesc.desc.format);
  planeFilled = 0;
  clockPts = 0;
  clockSamples = 0;
  bClockAnchored = false;
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
  // frame的开始pts: 时间轴按输出样本累计, 输入块 pts 只用于首帧锚定。
  // EAC3(1536)与 AAC(1024)不整除, 逐块沿用输入 pts 会抖成 21/32/11ms
  int64_t spts = frame.pts;
  // 源时间轴大跳变(缺包/拼接): 重新锚定样本时钟, 否则缺口会被压平
  if (bClockAnchored && spts != AVOX_NOVALID_PTS) {
    const int32_t rate =
        enDesc.desc.sampleRate > 0 ? enDesc.desc.sampleRate : 48000;
    const int32_t partialSamples =
        (bPlane && enDesc.desc.channels > 1)
            ? planeFilled
            : getSamples(curFrame.getSize() - curFrame.spaceLeft(),
                         enDesc.desc);
    const int64_t expected =
        framePts() + (int64_t)partialSamples * 1000 / rate;
    if (spts > expected + 500) {
      clockPts = spts;
      clockSamples = 0;
      planeFilled = 0;
      curFrame.clear();
      curFrame.setPts(spts);
    }
  }
  if (!bClockAnchored && spts != AVOX_NOVALID_PTS) {
    clockPts = spts;
    bClockAnchored = true;
  }
  // 第一次进来，初始化当前包的时间戳
  if (curFrame.getPts() == AVOX_NOVALID_PTS) {
    curFrame.setPts(bClockAnchored ? clockPts : spts);
  }
  // 平面格式必须逐声道攒帧: 输入是 [ch][samples], 帧缓冲是 [ch][frameSamples],
  // 两者不整除时按字节切片会把一个声道的数据摊到别的声道上
  if (bPlane && enDesc.desc.channels > 1) {
    return fillFramePlanar(data, size, spts);
  }
  // 写入数据,数据每次组成固定bufferMs的长度bufferSize
  while (size > 0) {
    // 当前BUFFER剩余空间
    int32_t spaceleft = curFrame.spaceLeft();
    if (size >= spaceleft) {
      // 写入当前BUFFER并写满
      curFrame.writeBytes(data, spaceleft);
      // mp4/rtsp要求pts递增: 录制buffer抖动使实际间隔与数据间隔不一致, 需修正(android录音常见)
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
      clockSamples += getSamples(curFrame.getSize(), enDesc.desc);
      // 下一帧的开始时间
      curFrame.setPts(framePts());
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

DecodeResult AudioEncoder::fillFramePlanar(uint8_t* data, int32_t size,
                                           int64_t pts) {
  const int32_t channels = enDesc.desc.channels;
  const int32_t bytesPerSample = audioFormatSize(enDesc.desc.format);
  const int32_t frameBytes = curFrame.getSize();
  if (bytesPerSample <= 0 || frameBytes <= 0) {
    return DecodeResult::dataError;
  }
  const int32_t frameSamples = frameBytes / (channels * bytesPerSample);
  const int32_t samples = size / (channels * bytesPerSample);
  if (frameSamples <= 0 || samples <= 0) {
    return DecodeResult::success;
  }
  int32_t pos = 0;
  while (pos < samples) {
    const int32_t take = std::min(frameSamples - planeFilled, samples - pos);
    for (int32_t ch = 0; ch < channels; ++ch) {
      std::memcpy(curFrame.point() +
                      ((size_t)ch * frameSamples + planeFilled) * bytesPerSample,
                  data + ((size_t)ch * samples + pos) * bytesPerSample,
                  (size_t)take * bytesPerSample);
    }
    planeFilled += take;
    pos += take;
    if (planeFilled < frameSamples) {
      break;
    }
    // mp4/rtsp要求pts递增: 与 fillFrame 的修正口径保持一致
    if (curFrame.getPts() <= lastPts) {
      pts = lastPts + 1;
      curFrame.setPts(pts);
    }
    lastPts = curFrame.getPts();
    planeFilled = 0;
    DecodeResult dr = encode();
    if (dr == DecodeResult::complete) {
      return dr;
    }
    curFrame.clear();
    clockSamples += frameSamples;
    curFrame.setPts(framePts());
  }
  return DecodeResult::success;
}

int64_t AudioEncoder::framePts() const {
  if (!bClockAnchored) {
    return lastPts + 1;
  }
  const int32_t rate =
      enDesc.desc.sampleRate > 0 ? enDesc.desc.sampleRate : 48000;
  return clockPts + clockSamples * 1000 / rate;
}

}
