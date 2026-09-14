#pragma once

#include <cstdint>
#include <vector>

struct AVCodecParameters;
struct AVCodecContext;

#include "avox/subtitle/IAssOverlay.hpp"

namespace avox {

// PGS(HDMV 蓝光位图字幕)解码器(计划 ASS字幕渲染计划.md §3.6):
// FFmpeg pgssub 解码器 → 位图矩形 + 调色板查表 → RGBA(premultiplied)联合
// bbox 画布 → 复用 IAssOverlay 的通用 canvas 通道上屏(不经 libass, 无文字
// 可排)。解码只在选中该轨时由 IO 线程喂包, 成本近零、零新增三方库。
class PgsDecoder {
 public:
  PgsDecoder() = default;
  ~PgsDecoder();

  // 用流参数初始化 pgssub 解码器(codecpar 提供宽高与 extradata)
  bool open(const AVCodecParameters* par);

  // seek 后重置解码状态与画布
  void flush();

  enum class FeedResult {
    none,     // 本包未产生呈现变化
    frame,    // 有位图呈现: 用 canvas() 取画布
    cleared,  // 空呈现(清屏): canvas() 的 rgba == nullptr
  };

  // 喂一个字幕包(拷贝内部持有)。ptsMs 为呈现时间戳(毫秒, 供画布标注)。
  FeedResult feed(const uint8_t* data, int32_t size, int64_t ptsMs);

  // 最近一次呈现的画布(bbox 裁剪, premultiplied RGBA; 内存归本对象,
  // 下一次 feed 前有效)。cleared 时 rgba == nullptr 且 seq 递增。
  const AssCanvas& canvas() const { return canvas_[front_]; }

 private:
  AVCodecContext* ctx_ = nullptr;
  // 双缓冲画布(整帧坐标系)
  std::vector<uint8_t> buf_[2];
  AssCanvas canvas_[2] = {};
  int32_t front_ = 0;
  int32_t seq_ = 0;
  bool have_[2] = {false, false};

  PgsDecoder(const PgsDecoder&) = delete;
  PgsDecoder& operator=(const PgsDecoder&) = delete;
};

}
