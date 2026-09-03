#pragma once

#include "FFHelper.hpp"
#include "avox/source/PacketBuf.hpp"

namespace avox {

class FFResample {
public:
  FFResample() = default;
  virtual ~FFResample() = default;

private:
  SwrContextPtr swrCtx = nullptr;
  AudioDesc srcDesc = {};
  AudioDesc dstDesc = {};
  // 如果相同,可以直接返回
  bool bSameDesc = false;
  // 重采样缓存
  std::vector<uint8_t> reBuffer;
  double scale = 1.0;

public:
  bool init(const AudioDesc &sDesc, const AudioDesc &dDesc);
  // int32_t resample(const AvoxData &src, const AvoxData &dst);
  int32_t resample(AvoxData &src);
  // 排干 swr 内部缓冲(filter delay 残留样本): 用 NULL 输入 swr_convert,
  // 返回残余字节数并填 out.data/out.size; bSameDesc(不重采样)直接返回 0
  int32_t flush(AvoxData &out);
};

}
