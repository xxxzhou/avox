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
    // 忽略无效数据错误
    if (ret != AVERROR_INVALIDDATA) {
      AVOX_FFMEPG_LOG_RETURN(ret, DecodeResult::dataNoReady,
                            "avcodec_send_packet failed");
    }
  }
  while (true) {
    ret = avcodec_receive_frame(codecCtx.get(), avFrame.get());
    if (ret < 0) {
      // 没有更多帧可接收，循环结束
      if (ret == AVERROR_EOF) {
        return DecodeResult::complete;
      }
      if (ret == AVERROR(EAGAIN)) {
        return DecodeResult::dataNoReady;
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
  }
  return DecodeResult::success;
}

void FFDecoder::flushContext() {
  if (!codecCtx) {
    return;
  }
  avcodec_flush_buffers(codecCtx.get());
}

}
