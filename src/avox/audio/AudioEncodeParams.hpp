#pragma once

#include "../AvoxAudio.h"
#include "../AvoxMuxer.h"

namespace avox {

// 自动码率(bps): 按声道数分档(1ch 96k / 2ch 160k / 3-4ch 256k /
// 5-6ch 384k / 7-8ch 512k); channels<=0 按双声道
int32_t getAutoAudioBitrate(const AudioDesc& desc);

// 已解析码率(bps): >0 显式(kbps→bps 并钳 8k..1024k); -1 按声道自动;
// 0 = 不设(保持编码器现状缺省, 存量行为不变)
int32_t resolveAudioBitrate(const AudioEncodeParam& param,
                            const AudioDesc& desc);

// profile 解析: inherit → LC(投屏与录制都锁 LC)
AudioEncodeProfile resolveAudioProfile(AudioEncodeProfile profile);

}
