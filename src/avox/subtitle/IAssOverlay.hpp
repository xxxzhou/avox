#pragma once

#include <cstdint>

namespace avox {

// ============== ASS/PGS 字幕 overlay(纯接口, plugins/avox_ass 实现) ==============
//
// 数据流: MKV ASS 轨(packet/extradata)或外挂 .ass/.srt → 插件内 libass 光栅化
// → RGBA canvas(联合 bbox 裁剪, 双缓冲) → 核心 VK PipeGraph 混合层(sourceOver)
// → 随输出帧直达消费端。PGS 走 FFmpeg pgssub 解码产出同一种 canvas, 复用同一通道。
//
// 跨 DLL 规范(plugins/机制文档 §6): 接口只传原始类型/const char*/C 函数指针,
// 不传 STL;canvas 内存归插件所有, render() 返回后到下一次 render() 前有效, 消费方只读。
// 未安装插件 → AvoxManager::assOverlayHub.create("libass") 返回 nullptr,
// 调用方降级为不渲染内封特效字幕轨(不崩)。

// ASS/PGS overlay 画布: RGBA8 位图裁剪到联合 bounding box
struct AssCanvas {
  const uint8_t* rgba = nullptr;  // RGBA8 行主序; rgba[0] 对应视频帧 (x, y); 无字幕为 nullptr
  int32_t width = 0;              // 像素
  int32_t height = 0;             // 像素
  int32_t stride = 0;             // 每行字节数
  int32_t x = 0;                  // canvas 左上角相对视频帧的偏移
  int32_t y = 0;
  int64_t ptsMs = 0;              // 本画布内容对应的播放时间
  int32_t seq = 0;                // 递增序号: 消费方据此判变更(seq 变 = 重新上传)
};

// 字体来源说明(libass 公开 API 无按需回调, 见 ass_set_fonts_dir/ass_add_font):
// Windows/macOS/Linux 用 ASS_FONTPROVIDER_AUTODETECT(DirectWrite/CoreText/fontconfig);
// Android 无系统字体提供器 → setFontsDir 指定字体目录 + setDefaultFont 兜底。

class IAssOverlay {
 public:
  virtual ~IAssOverlay() = default;

  // 能力探测 + 初始化。storage 分辨率 = 视频帧分辨率(libass 排版坐标系, \pos 等按此换算)。
  // 返回 false = libass 不可用/内部失败, 调用方降级(等价无插件)。
  virtual bool init(int32_t storageWidth, int32_t storageHeight) = 0;
  virtual void shutdown() = 0;

  // 字体目录(可选): 无系统字体提供器的平台(Android)扫描此目录取字体;
  // 需在 init 前调用
  virtual void setFontsDir(const char* dir) = 0;
  // 缺省字体(完整路径 + family 名), 系统提供器不可用时的兜底; 需在 init 前调用
  virtual void setDefaultFont(const char* fontPath, const char* family) = 0;

  // 内封轨: 先喂 MKV ASS extradata(剧本头 [Script Info]/[V4+ Styles] 段), 再 processChunk
  virtual bool loadTrack(const char* extradata, int32_t size) = 0;
  // 内封轨流: 一个 MKV ASS packet = 一个 chunk(MKV 的 ASS packet 天然是 libass chunk)
  virtual void processChunk(const char* data, int32_t size, int64_t ptsMs,
                            int64_t durationMs) = 0;
  // 外挂文件: .ass 直接加载; .srt 先转 ASS 再加载
  virtual bool loadFile(const char* path) = 0;

  // 渲染当前帧字幕: 返回画布指针(内部双缓冲)。
  // 内容未变 → 返回同一 seq(消费方跳过重新上传); 无字幕 → rgba=nullptr 且 seq 递增(消费方清层)。
  virtual const AssCanvas* render(int64_t ptsMs) = 0;

  // seek 后丢弃过期状态(libass 内部事件缓冲)
  virtual void flush() = 0;
  // 卸载当前轨(切轨用), init 保持
  virtual void unload() = 0;
};

}  // namespace avox
