#include "PgsDecoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <cstring>

namespace avox {

namespace {

// AVPALETTE 项 → 预乘 RGBA(浮点 0..255, r*a 已除 255): 缩放重采样的插值空间
// 必须预乘, 否则半透明边缘会被当成暗色糊边。AVPALETTE 为 0xAARRGGBB
// (pgssubdec 的 RGBA 宏把 R 放第 16 位), 低字节是 B —— 与 ASS 路径(libass
// color 高字节 R)同族, 都填进 RGBA 画布
inline void palPremul(const uint32_t* pal, uint8_t idx, float out[4]) {
  const uint32_t entry = pal[idx];
  const float a = (float)(uint8_t)(entry >> 24);
  out[0] = (float)(uint8_t)(entry >> 16) * a / 255.f;
  out[1] = (float)(uint8_t)(entry >> 8) * a / 255.f;
  out[2] = (float)(uint8_t)(entry >> 0) * a / 255.f;
  out[3] = a;
}

inline uint8_t clamp255(float v) {
  return (uint8_t)((v < 0.f ? 0.f : (v > 255.f ? 255.f : v)) + 0.5f);
}

// 预乘 src-over 合成单像素(与 blitRect 同语义, 浮点插值结果版)
inline void blendPremul(uint8_t* px, float sr, float sg, float sb, float sa) {
  const float inv = (255.f - sa) / 255.f;
  px[0] = clamp255(sr + px[0] * inv);
  px[1] = clamp255(sg + px[1] * inv);
  px[2] = clamp255(sb + px[2] * inv);
  px[3] = clamp255(sa + px[3] * inv);
}

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
      // FFmpeg 字幕调色板: AVPALETTE 0xAARRGGBB(pgssubdec RGBA 宏 R 在第 16 位,
      // 低字节 B) —— 与 libass color(高字节 R)同族, 按 RGBA 画布取值
      const uint8_t r = (uint8_t)(entry >> 16);
      const uint8_t g = (uint8_t)(entry >> 8);
      const uint8_t b = (uint8_t)(entry >> 0);
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

// 图形平面 → 视频帧的缩放版 blit: 目标像素中心反查平面坐标(边界钳进 rect,
// 按边缘像素延展), 四邻域双线性插值后 src-over。PGS 位图是 1/8 位调色板索引,
// 直接最近邻放大 2 倍会把字边缘放大成硬块, 双线性与 GPU 采样画布同观感。
void blitRectScaled(const AVSubtitleRect* rect, std::vector<uint8_t>& dst,
                    int32_t frameW, int32_t frameH, float scaleX,
                    float scaleY) {
  int32_t x0 = (int32_t)((float)rect->x * scaleX + 0.5f);
  int32_t y0 = (int32_t)((float)rect->y * scaleY + 0.5f);
  int32_t x1 = (int32_t)((float)(rect->x + rect->w) * scaleX + 0.5f);
  int32_t y1 = (int32_t)((float)(rect->y + rect->h) * scaleY + 0.5f);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > frameW) x1 = frameW;
  if (y1 > frameH) y1 = frameH;
  if (x1 <= x0 || y1 <= y0 || !rect->data[0] || !rect->data[1]) {
    return;
  }
  const auto* pal = reinterpret_cast<const uint32_t*>(rect->data[1]);
  const uint8_t* src = rect->data[0];
  const int32_t stride = rect->linesize[0];
  const float maxSx = (float)(rect->w - 1);
  const float maxSy = (float)(rect->h - 1);
  for (int32_t y = y0; y < y1; ++y) {
    float fy = ((float)y + 0.5f) / scaleY - 0.5f - (float)rect->y;
    if (fy < 0.f) fy = 0.f;
    if (fy > maxSy) fy = maxSy;
    const int32_t iy = (int32_t)fy;
    const int32_t iy1 = iy < rect->h - 1 ? iy + 1 : iy;
    const float wy = fy - (float)iy;
    const uint8_t* row0 = src + (size_t)iy * stride;
    const uint8_t* row1 = src + (size_t)iy1 * stride;
    uint8_t* row = dst.data() + (size_t)y * frameW * 4;
    for (int32_t x = x0; x < x1; ++x) {
      float fx = ((float)x + 0.5f) / scaleX - 0.5f - (float)rect->x;
      if (fx < 0.f) fx = 0.f;
      if (fx > maxSx) fx = maxSx;
      const int32_t ix = (int32_t)fx;
      const int32_t ix1 = ix < rect->w - 1 ? ix + 1 : ix;
      const float wx = fx - (float)ix;
      float p00[4], p01[4], p10[4], p11[4];
      palPremul(pal, row0[ix], p00);
      palPremul(pal, row0[ix1], p01);
      palPremul(pal, row1[ix], p10);
      palPremul(pal, row1[ix1], p11);
      const float w00 = (1.f - wx) * (1.f - wy), w01 = wx * (1.f - wy);
      const float w10 = (1.f - wx) * wy, w11 = wx * wy;
      blendPremul(row + (size_t)x * 4,
                  p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11,
                  p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11,
                  p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11,
                  p00[3] * w00 + p01[3] * w01 + p10[3] * w10 + p11[3] * w11);
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

void PgsDecoder::setCanvasSize(int32_t width, int32_t height) {
  canvasWidth = width > 0 ? width : 0;
  canvasHeight = height > 0 ? height : 0;
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
  // 坐标系: 组成矩形坐标在图形平面上, 平面尺寸由 PCS 视频描述符给出(pgssubdec
  // 据此回填 avctx->width/height, 容器字幕轨多为 0); 目标为字幕合成画布
  // (subtitleCanvasSize: >1080p 恒 1920x1080, UHD BD 的 1080p 平面恰 1:1 直入,
  // 铺满全帧由画布层 sampler 拉伸); 平面!=画布时按画布重采样铺满
  const int32_t planeW = ctx_->width > 0 ? ctx_->width : 0;
  const int32_t planeH = ctx_->height > 0 ? ctx_->height : 0;
  int32_t canvasW = planeW, canvasH = planeH;
  float scaleX = 1.f, scaleY = 1.f;
  if (planeW > 0 && planeH > 0 && canvasWidth > 0 && canvasHeight > 0 &&
      (canvasWidth != planeW || canvasHeight != planeH)) {
    canvasW = canvasWidth;
    canvasH = canvasHeight;
    scaleX = (float)canvasWidth / (float)planeW;
    scaleY = (float)canvasHeight / (float)planeH;
  }
  int32_t rects = 0;
  for (unsigned i = 0; i < sub.num_rects; ++i) {
    auto* r = sub.rects[i];
    if (r && r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0) {
      ++rects;
    }
  }
  AssCanvas& out = canvas_[back];
  if (!rects || canvasW <= 0 || canvasH <= 0) {
    out = AssCanvas{};
    out.ptsMs = ptsMs;
    out.seq = ++seq_;
    front_ = back;
    have_[front_] = true;
    result = rects ? FeedResult::none : FeedResult::cleared;
  } else {
    buf_[back].assign((size_t)canvasW * canvasH * 4, 0);
    const bool sameSize = scaleX == 1.f && scaleY == 1.f;
    for (unsigned i = 0; i < sub.num_rects; ++i) {
      auto* r = sub.rects[i];
      if (r && r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0) {
        if (sameSize) {
          blitRect(r, buf_[back], canvasW, canvasH);
        } else {
          blitRectScaled(r, buf_[back], canvasW, canvasH, scaleX, scaleY);
        }
      }
    }
    out = AssCanvas{};
    out.rgba = buf_[back].data();
    out.width = canvasW;
    out.height = canvasH;
    out.stride = canvasW * 4;
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
