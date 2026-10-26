// 音频编码旋钮纯函数单测: 自动码率分档与解析(显式/-1 自动/0 保持缺省)
#include "avox/audio/AudioEncodeParams.hpp"
#include "doctest.h"

using namespace avox;

TEST_CASE("getAutoAudioBitrate: 按声道分档") {
  AudioDesc desc = {};
  desc.channels = 1;
  CHECK(getAutoAudioBitrate(desc) == 96000);
  desc.channels = 2;
  CHECK(getAutoAudioBitrate(desc) == 160000);
  desc.channels = 3;
  CHECK(getAutoAudioBitrate(desc) == 256000);
  desc.channels = 4;
  CHECK(getAutoAudioBitrate(desc) == 256000);
  desc.channels = 6;
  CHECK(getAutoAudioBitrate(desc) == 384000);
  desc.channels = 8;
  CHECK(getAutoAudioBitrate(desc) == 512000);
  // 未给声道按双声道
  desc.channels = 0;
  CHECK(getAutoAudioBitrate(desc) == 160000);
}

TEST_CASE("resolveAudioBitrate: 0 保持缺省 / -1 自动 / 显式钳位") {
  AudioDesc desc = {};
  desc.channels = 6;
  AudioEncodeParam param = {};
  // 0 = 不设(存量行为不变): 编码器各自现状缺省
  CHECK(resolveAudioBitrate(param, desc) == 0);
  // -1 = 按声道自动
  param.bitrateKbps = -1;
  CHECK(resolveAudioBitrate(param, desc) == 384000);
  // 显式 kbps → bps
  param.bitrateKbps = 128;
  CHECK(resolveAudioBitrate(param, desc) == 128000);
  // 钳到 8k..1024k
  param.bitrateKbps = 1;
  CHECK(resolveAudioBitrate(param, desc) == 8000);
  param.bitrateKbps = 4096;
  CHECK(resolveAudioBitrate(param, desc) == 1024000);
}

TEST_CASE("resolveAudioProfile: inherit → LC") {
  CHECK(resolveAudioProfile(AudioEncodeProfile::inherit) ==
        AudioEncodeProfile::lc);
  CHECK(resolveAudioProfile(AudioEncodeProfile::lc) == AudioEncodeProfile::lc);
  CHECK(resolveAudioProfile(AudioEncodeProfile::he) == AudioEncodeProfile::he);
}
