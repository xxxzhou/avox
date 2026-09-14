#include "PgsDecoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <cstring>

namespace avox {

namespace {

// AVSubtitleRect 位图(pal8: data[0]=索引, data[1]=调色板) → premultiplied
// RGBA blit 到整帧画布。
void blitRect(const AVSubtitleRect* rect, std::vector<uint8_t>& dst,
              int32_t frameW, int32_t frameH) {
  const int32_t x0 = rect->x < 0 ? 0 : rect->x;
  const int32_t y0 = rect->y < 0 ? 0 : rect->y;
  int32_t x1 = rect->x + rect->w, y1 = rect->y + rect->h;
  if (x1 > frameW) x1 = frameW;
  if (y1 > frameH) y1 = frameH;
  if (x1 <= x0 || y1 <= y0 || !rect->data[0] || !rect->data[1]) {
    return;
  }
  const auto* pal = reinterpret_cast<const uint32_t*>(rect->data[1]);
  for (int32_t y = y0; y < y1; ++y) {
    const uint8_t* src = rect->data[0] + (size_t)(y - rect->y) * rect->linesize[0];
    uint8_t* row = dst.data() + (size_t)y * frameW * 4;
    for (int32_t x = x0; x < x1; ++x) {
      const uint32_t entry = pal[src[x - rect->x]];
      // FFmpeg 字幕调色板: 本机序 uint32, 字节序 R,G,B,A(AVPALETTE 约定,
      // 见 libavcodec/pgssubdec 的打包顺序)
      const uint8_t r = (uint8_t)(entry >> 0);
      const uint8_t g = (uint8_t)(entry >> 8);
      const uint8_t b = (uint8_t)(entry >> 16);
      const uint8_t a = (uint8_t)(entry >> 24);
      if (!a) {
        continue;
      }
      uint8_t* px = row + (size_t)x * 4;
      // premultiplied src-over 合成(与 AssOverlay 画布同语义)
      px[0] = (uint8_t)((r * a + px[0] * (255 - a)) / 255);
      px[1] = (uint8_t)((g * a + px[1] * (255 - a)) / 255);
      px[2] = (uint8_t)((b * a + px[2] * (255 - a)) / 255);
      px[3] = (uint8_t)(a + px[3] * (255 - a) / 255);
    }
  }
}

}  // namespace

PgsDecoder::~PgsDecoder() {
  if (ctx_) {
    avcodec_free_context(&ctx_);
  }
}

bool PgsDecoder::open(const AVCodecParameters* par) {
  if (!par) {
    return false;
  }
  const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_HDMV_PGS_SUBTITLE);
  if (!dec) {
    return false;
  }
  ctx_ = avcodec_alloc_context3(dec);
  if (!ctx_) {
    return false;
  }
  if (avcodec_parameters_to_context(ctx_,
                                    const_cast<AVCodecParameters*>(par)) < 0) {
    avcodec_free_context(&ctx_);
    return false;
  }
  if (avcodec_open2(ctx_, dec, nullptr) < 0) {
    avcodec_free_context(&ctx_);
    return false;
  }
  return true;
}

void PgsDecoder::flush() {
  if (ctx_) {
    avcodec_flush_buffers(ctx_);
  }
  // 画布清空: 置空内容并递增 seq, 消费方据此清层
  const int32_t back = front_ ^ 1;
  canvas_[back] = AssCanvas{};
  canvas_[back].seq = ++seq_;
  front_ = back;
  have_[front_] = true;
}

PgsDecoder::FeedResult PgsDecoder::feed(const uint8_t* data, int32_t size,
                                        int64_t ptsMs) {
  if (!ctx_ || !data || size <= 0) {
    return FeedResult::none;
  }
  AVPacket* pkt = av_packet_alloc();
  if (!pkt) {
    return FeedResult::none;
  }
  if (av_new_packet(pkt, size) == 0) {
    memcpy(pkt->data, data, (size_t)size);
    pkt->pts = ptsMs;
    pkt->dts = ptsMs;
  } else {
    av_packet_free(&pkt);
    return FeedResult::none;
  }
  AVSubtitle sub;
  memset(&sub, 0, sizeof(sub));
  int got = 0;
  const int ret =
      avcodec_decode_subtitle2(ctx_, &sub, &got, pkt);
  av_packet_free(&pkt);
  if (ret < 0 || !got) {
    return FeedResult::none;
  }
  FeedResult result = FeedResult::none;
  const int32_t back = front_ ^ 1;
  // PGS 呈现集可能只有调色板更新(无矩形) → 清屏; 多矩形全部合成
  const int32_t frameW = ctx_->width > 0 ? ctx_->width : 0;
  const int32_t frameH = ctx_->height > 0 ? ctx_->height : 0;
  int32_t rects = 0;
  for (unsigned i = 0; i < sub.num_rects; ++i) {
    auto* r = sub.rects[i];
    if (r && r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0) {
      ++rects;
    }
  }
  AssCanvas& out = canvas_[back];
  if (!rects || frameW <= 0 || frameH <= 0) {
    out = AssCanvas{};
    out.ptsMs = ptsMs;
    out.seq = ++seq_;
    front_ = back;
    have_[front_] = true;
    result = rects ? FeedResult::none : FeedResult::cleared;
  } else {
    // 画布坐标系: PGS rect 坐标即为视频分辨率坐标(解码器按流宽高换算)
    buf_[back].assign((size_t)frameW * frameH * 4, 0);
    for (unsigned i = 0; i < sub.num_rects; ++i) {
      auto* r = sub.rects[i];
      if (r && r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0) {
        blitRect(r, buf_[back], frameW, frameH);
      }
    }
    out = AssCanvas{};
    out.rgba = buf_[back].data();
    out.width = frameW;
    out.height = frameH;
    out.stride = frameW * 4;
    out.x = 0;
    out.y = 0;
    out.ptsMs = ptsMs;
    out.seq = ++seq_;
    front_ = back;
    have_[front_] = true;
    result = FeedResult::frame;
  }
  avsubtitle_free(&sub);
  return result;
}

}
