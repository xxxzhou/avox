#pragma once

#include "FFHelper.hpp"
#include "avox/audio/DownmixLevel.hpp"
#include "avox/source/PacketBuf.hpp"

namespace avox {

// 导出: 白盒探针/工具直驱本类做数值对照(samples/functest/downmixtest), Windows
// avox.dll 是导入库, 不导出则链接 LNK2019(同模块 FFHelper.hpp 的导出口径)
class AVOX_EXPORT FFResample {
public:
  FFResample() = default;
  virtual ~FFResample() = default;

private:
  SwrContextPtr swrCtx = nullptr;
  AudioDesc srcDesc = {};
  AudioDesc dstDesc = {};
  // 如果相同,可以直接返回
  bool bSameDesc = false;
  // 下混系数: downmixParamet=最新请求(可随时换), swrParamet=当前 swr 建库所用
  // 参数; 两者不等时在 resample 内惰性重建(热切, 见 resample)
  DownmixParamet downmixParamet = {};
  DownmixParamet swrParamet = {};
  // 重采样缓存
  std::vector<uint8_t> reBuffer;
  double scale = 1.0;

public:
  // downmix: 多声道→少声道下混系数(none=不干预, 逐位等价历史行为)
  bool init(const AudioDesc &sDesc, const AudioDesc &dDesc,
            const DownmixParamet &downmix = DownmixParamet{});
  // 更新下混系数(轻量: 只记录, 实际重建推迟到下次 resample)
  void setDownmix(const DownmixParamet &downmix) { downmixParamet = downmix; }
  // int32_t resample(const AvoxData &src, const AvoxData &dst);
  int32_t resample(AvoxData &src);
  // 排干 swr 内部缓冲(filter delay 残留样本): 用 NULL 输入 swr_convert,
  // 返回残余字节数并填 out.data/out.size; bSameDesc(不重采样)直接返回 0
  int32_t flush(AvoxData &out);
};

}
