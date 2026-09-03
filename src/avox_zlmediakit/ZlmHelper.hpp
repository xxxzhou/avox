#pragma once

#include "mk_mediakit.h"
#include "ZlmExport.h"
#include "avox/AvoxCodec.h"
#include "avox/AvoxPlayer.h"
#include "avox/module/LogHelper.hpp"

namespace avox {
// ZL里的错误码转avox的错误码 android编译不过猜测头文件冲突
AVError zlIoError(int32_t errCode);
// zl里的编码类型转avox的编码类型
VCodecId zlVCodec(int32_t codec);

ACodecId zlACodec(int32_t codec);

int32_t getZlCodecId(VCodecId codec);
int32_t getZlCodecId(ACodecId codec);

AudioFormat zlAudioFromat(int32_t sample_bit);

PackType zlPacketType(mk_frame frame);

AvoxPacket zlAvoxPacket(mk_frame frame);

}