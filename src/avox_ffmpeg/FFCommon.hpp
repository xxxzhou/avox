#pragma once

#include "avox/AvoxCodec.h"
#include "avox/AvoxPlayer.h"
#include "avox/module/LogHelper.hpp"
extern "C" {
// 保存原始AVMediaType定义
#define AVMediaType FF_AVMediaType
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
// 恢复原始AVMediaType定义
#undef AVMediaType
}

// FFmpeg 版本守卫(a06-T4): 全部适配针对 9.0.1(avcodec major 63)。
// 换代时此断言拦下裸奔构建; 适配完成(或评估期临时放行)定义
// AVOX_FF_ALLOW_VERSION_BUMP, 并重审 FFVDecoder 弱化用法。
#if !defined(AVOX_FF_ALLOW_VERSION_BUMP)
static_assert(LIBAVCODEC_VERSION_MAJOR == 63,
              "FFmpeg major version changed beyond adapted 63 (9.0.1); "
              "re-audit decoder usage or define AVOX_FF_ALLOW_VERSION_BUMP");
#endif

namespace avox {


}