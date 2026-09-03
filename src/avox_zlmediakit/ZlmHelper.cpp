#include "ZlmHelper.hpp"

#include "Extension/Frame.h"
#include "Http/HttpRequester.h"
#include "avox/module/Json.hpp"
#include "mpeg4-avc.h"

// #include "Http/HttpRequester.h"
using namespace mediakit;
using namespace toolkit;

namespace avox {

// 对应zltookit里的socket.h,不知是否正常
// Error type enumeration
// typedef enum {
//     Err_success = 0, //成功 success
//     Err_eof, //eof
//     Err_timeout, //超时 socket timeout
//     Err_refused,//连接被拒绝 socket refused
//     Err_reset,//连接被重置  socket reset
//     Err_dns,//dns解析失败 dns resolve failed
//     Err_shutdown,//主动关闭 socket shutdown
//     Err_other = 0xFF,//其他错误 other error
// } ErrCode;
AVError zlIoError(int32_t errCode) {
  switch (errCode) {
    case 0:
      return AVError::none;
    case 1:
      return AVError::endOfFile;
    case 2:
      return AVError::netTimeout;
    case 3:
      return AVError::deviceBusy;
    case 4:
      return AVError::devcieLost;
    case 5:
      return AVError::urlNoSupport;
    case 6:
      return AVError::netShutdown;
    case 0xFF:
      return AVError::other;
  }
  return AVError::other;
}

VCodecId zlVCodec(int32_t codec) {
  switch (static_cast<mediakit::CodecId>(codec)) {  // 转换为枚举类型
    case mediakit::CodecId::CodecH264:              // 使用完全限定名
      return VCodecId::h264;
    case mediakit::CodecId::CodecH265:
      return VCodecId::h265;
    default:
      return VCodecId::none;
  }
}

ACodecId zlACodec(int32_t codec) {
  switch (static_cast<mediakit::CodecId>(codec)) {  // 转换为枚举类型
    case mediakit::CodecId::CodecAAC:
      return ACodecId::aac;
    case mediakit::CodecId::CodecG711A:
      return ACodecId::g711a;
    case mediakit::CodecId::CodecG711U:
      return ACodecId::g711u;
    case mediakit::CodecId::CodecOpus:
      return ACodecId::opus;
    default:
      return ACodecId::none;
  }
}

int32_t getZlCodecId(VCodecId codec) {
  switch (codec) {
    case VCodecId::h264:
      return mediakit::CodecId::CodecH264;
    case VCodecId::h265:
      return mediakit::CodecId::CodecH265;
    default:
      return -1;
  }
}
int32_t getZlCodecId(ACodecId codec) {
  switch (codec) {
    case ACodecId::aac:
      return mediakit::CodecId::CodecAAC;
    case ACodecId::g711a:
      return mediakit::CodecId::CodecG711A;
    case ACodecId::g711u:
      return mediakit::CodecId::CodecG711U;
    case ACodecId::opus:
      return mediakit::CodecId::CodecOpus;
    default:
      return -1;
  }
}

AudioFormat zlAudioFromat(int32_t sample_bit) {
  if (sample_bit == 16) {
    return AudioFormat::AVOX_AUDIO_S16;
  } else if (sample_bit == 8) {
    return AudioFormat::AVOX_AUDIO_U8;
  }
  return AudioFormat::other;
}

PackType zlPacketType(mk_frame frame) {
  uint32_t flag = mk_frame_get_flags(frame);
  bool bConfig = (flag & MK_FRAME_FLAG_IS_CONFIG) == MK_FRAME_FLAG_IS_CONFIG;
  bool bVideo = mk_frame_is_video(frame);
  if (bVideo) {
    return bConfig ? PackType::vconfig : PackType::video;
  } else {
    return bConfig ? PackType::aconfig : PackType::audio;
  }
}

AvoxPacket zlAvoxPacket(mk_frame frame) {
  uint32_t flag = mk_frame_get_flags(frame);
  AvoxPacket packet = {};
  packet.packtype = (int32_t)zlPacketType(frame);
  packet.index = 0;
  packet.pts = mk_frame_get_pts(frame);
  packet.dts = mk_frame_get_dts(frame);
  packet.prefixSize = mk_frame_get_data_prefix_size(frame);
  packet.frameType = (flag & MK_FRAME_FLAG_IS_KEY) == MK_FRAME_FLAG_IS_KEY;
  // 数据,在进入队列前,引用都在,所以不用复制
  packet.data.bRef = true;
  packet.data.size = mk_frame_get_data_size(frame);
  packet.data.data = (uint8_t*)mk_frame_get_data(frame);
  return packet;
}

}
