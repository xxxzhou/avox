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

namespace avox {


}