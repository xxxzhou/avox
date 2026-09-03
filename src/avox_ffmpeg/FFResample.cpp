#include "FFResample.hpp"

namespace avox {

bool FFResample::init(const AudioDesc& sDesc, const AudioDesc& dDesc) {
  // 如果相同,不需要重采样
  bSameDesc = sDesc == dDesc;
  if (bSameDesc) {
    return true;
  }
  // 如果已经用相同参数初始化过
  if (srcDesc == sDesc && dstDesc == dDesc && swrCtx) {
    return true;
  }
  srcDesc = sDesc;
  dstDesc = dDesc;
  SwrContext* swr = swr_alloc();
  // log(LogLevel::info, "swr:", swr);
  AVChannelLayout in_ch_layout = {};
  av_channel_layout_default(&in_ch_layout, srcDesc.channels);
  AVChannelLayout out_ch_layout = {};
  av_channel_layout_default(&out_ch_layout, dstDesc.channels);
  AVSampleFormat inSampleFormat = getFFAudioFormat(srcDesc.format);
  AVSampleFormat outSampleFormat = getFFAudioFormat(dstDesc.format);
  int ret = 0;
  ret = swr_alloc_set_opts2(&swr, &out_ch_layout, outSampleFormat,
                            dstDesc.sampleRate, &in_ch_layout, inSampleFormat,
                            srcDesc.sampleRate, 0, nullptr);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "swr_alloc_set_opts2 failed");
    swr_free(&swr);
    return false;
  }
  ret = swr_init(swr);
  av_channel_layout_uninit(&in_ch_layout);
  av_channel_layout_uninit(&out_ch_layout);
  if (ret < 0) {
    AVOX_FFMEPG_LOG(ret, "swr_alloc_set_opts2 failed");
    swr_free(&swr);
    return false;
  }
  swrCtx = getUniquePtr(swr);
  // 每秒输入与输出的采样字节数
  int32_t inSizeMS = getAudioFrameSize(srcDesc, 10);
  int32_t outSizeMS = getAudioFrameSize(dstDesc, 10);
  // 输出与输入的倍率
  scale = (double)outSizeMS / (double)inSizeMS;
  return true;
}

int32_t FFResample::resample(AvoxData& src) {
  if (bSameDesc) {
    return src.size;
  }
  if (!swrCtx) {
    return -1;
  }
  AVSampleFormat inFormat = getFFAudioFormat(srcDesc.format);
  AVSampleFormat outFormat = getFFAudioFormat(dstDesc.format);
  // 输入采样数
  int32_t inSamples = src.size / (srcDesc.channels * audioFormatSize(srcDesc.format));
  if (inSamples <= 0) {
    return 0;
  }
  // 获取重采样器内部延迟
  int64_t delay = swr_get_delay(swrCtx.get(), dstDesc.sampleRate);
  int64_t outSamplesMax64 = delay + av_rescale_rnd(inSamples, dstDesc.sampleRate,
                                                  srcDesc.sampleRate, AV_ROUND_UP) + 1;
  int outSamplesMax = (int)outSamplesMax64;
  // 计算输出缓冲区大小
  int32_t outSizeNeed = outSamplesMax * dstDesc.channels * audioFormatSize(dstDesc.format);
  if (reBuffer.size() < (size_t)outSizeNeed) {
    reBuffer.resize(outSizeNeed);
  }
  // 使用 av_samples_fill_arrays 填充指针数组
  uint8_t* inData[8] = {nullptr};
  uint8_t* outData[8] = {nullptr};
  av_samples_fill_arrays(inData, nullptr, (const uint8_t*)src.data,
                         srcDesc.channels, inSamples, inFormat, 1);
  av_samples_fill_arrays(outData, nullptr, reBuffer.data(),
                         dstDesc.channels, outSamplesMax, outFormat, 1);
  // 执行重采样
  int outSamples = swr_convert(swrCtx.get(), outData, outSamplesMax,
                               (const uint8_t**)inData, inSamples);
  if (outSamples < 0) {
    AVOX_FFMEPG_LOG(outSamples, "swr_convert failed");
    return outSamples;
  }
  // 更新输出
  src.data = reBuffer.data();
  src.size = outSamples * dstDesc.channels * audioFormatSize(dstDesc.format);
  return src.size;
}

int32_t FFResample::flush(AvoxData& out) {
  out.data = nullptr;
  out.size = 0;
  if (bSameDesc || !swrCtx) {
    return 0;
  }
  // swr 因 filter delay 缓存的残余样本: NULL 输入排干
  int64_t delay = swr_get_delay(swrCtx.get(), dstDesc.sampleRate);
  if (delay <= 0) {
    return 0;
  }
  AVSampleFormat outFormat = getFFAudioFormat(dstDesc.format);
  int32_t outSizeNeed = delay * dstDesc.channels * audioFormatSize(dstDesc.format);
  if (reBuffer.size() < (size_t)outSizeNeed) {
    reBuffer.resize(outSizeNeed);
  }
  uint8_t* outData[8] = {nullptr};
  av_samples_fill_arrays(outData, nullptr, reBuffer.data(), dstDesc.channels,
                         delay, outFormat, 1);
  int outSamples = swr_convert(swrCtx.get(), outData, delay, nullptr, 0);
  if (outSamples <= 0) {
    return 0;
  }
  out.data = reBuffer.data();
  out.size = outSamples * dstDesc.channels * audioFormatSize(dstDesc.format);
  return out.size;
}

}
