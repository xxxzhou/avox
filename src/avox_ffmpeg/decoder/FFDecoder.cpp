#include "FFDecoder.hpp"

namespace avox {

FFDecoder::FFDecoder() {
  avPacket = getUniquePtr(av_packet_alloc());
  avFrame = getUniquePtr(av_frame_alloc());
}

FFDecoder::~FFDecoder() {}

DecodeResult FFDecoder::decodePacket(const AvoxPacket &packet) {
  if (!codecCtx) {
    return DecodeResult::openFailed;
  }
  avPacket->data = packet.data.data;
  avPacket->size = packet.data.size;
  avPacket->pts = packet.pts;
  avPacket->dts = packet.dts;
  if (packet.frameType == 1) {
    avPacket->flags |= AV_PKT_FLAG_KEY;
  }
  int32_t ret = avcodec_send_packet(codecCtx.get(), avPacket.get());
  if (ret < 0) {
    // 忽略无效数据错误(数据坏但车道是活的)
    if (ret != AVERROR_INVALIDDATA) {
      sendFailStreak++;
      // 未出过帧的连续喂包失败=车道打不开(实测vulkan帧池VK_ERROR_UNKNOWN时
      // send_packet恒ENOMEM), 返回openFailed让上层立即降级, 不吃满首帧看门狗
      if (!bDecodedEver && sendFailStreak >= kSendFailOpenFailLimit) {
        AVOX_FFMEPG_LOG_RETURN(ret, DecodeResult::openFailed,
                               "avcodec_send_packet failed, lane open fail");
      }
      AVOX_FFMEPG_LOG_RETURN(ret, DecodeResult::dataNoReady,
                            "avcodec_send_packet failed");
    }
  }
  sendFailStreak = 0;
  // 本次调用是否解出过帧。EAGAIN只代表"当前没有更多帧", 上层(VDecoderTask
  // 的bOpenDecode看门狗)以success作为"解码器已打开"的唯一凭证, 吐过帧却返回
  // dataNoReady 会让看门狗在 delayMs(5s) 后误杀正常播放中的解码器。
  bool gotFrame = false;
  while (true) {
    ret = avcodec_receive_frame(codecCtx.get(), avFrame.get());
    if (ret < 0) {
      // 没有更多帧可接收，循环结束
      if (ret == AVERROR_EOF) {
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        return gotFrame ? DecodeResult::success : DecodeResult::dataNoReady;
      } else {
        AVOX_FFMEPG_LOG(ret, "avcodec_receive_frame failed");
        break;
      }
    }
    // 注意,现在PacketQueue的锁下
    onFrame(avFrame.get(), false);
    // log(LogLevel::info, "packet pts:", avFrame->pts);
    // if (preVPts > avFrame->pts) {
    //   log(LogLevel::warn, "---preVPts:", preVPts);
    // }
    preVPts = avFrame->pts;
    // 释放 AVFrame 引用
    av_frame_unref(avFrame.get());
    gotFrame = true;
    bDecodedEver = true;
  }
  return gotFrame ? DecodeResult::success : DecodeResult::dataNoReady;
}

void FFDecoder::flushContext() {
  // 未open的ctx(alloc到open2之间/open2失败残留)internal为空, FFmpeg8的
  // avcodec_flush_buffers无守卫直接解引用必崩(9/25 16:15真机crash)
  if (!codecCtx || !bCtxOpened) {
    return;
  }
  avcodec_flush_buffers(codecCtx.get());
}

}
