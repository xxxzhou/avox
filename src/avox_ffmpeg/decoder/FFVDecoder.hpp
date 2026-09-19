#pragma once

#include "FFDecoder.hpp"
#include "avox/video/VideoDecoder.hpp"

namespace avox {

class FFVDecoder : public VideoDecoder, public FFDecoder {
public:
  FFVDecoder();
  virtual ~FFVDecoder();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket & packet) override;
  // flush
  virtual void flush() override;
  virtual void onClose() override;
  // IOptionOb(OptionLink): 选项推送缓存(含linkOption时存量重放)
  virtual void onOptionChange(const char* key, ArgType option) override;

  // FFDecoder
protected:
  // 解码完成，子类具体实现
  virtual void onFrame(AVFrame *avFrame, bool bDrop) override;
  virtual void onError(int32_t error) override;

protected:
  virtual void onAttachContext();
  virtual void onDetachContext();
  // SEI 裸流兜底: ffmpeg>=9 不再从带内 SEI 导出 MDCV/CLL(三处 side data
  // 均空), 从包内 prefix SEI NAL 自提 137/144 固定宽载荷
  void scanHdrSei(const AvoxPacket &packet);
  // 元数据变化才下发 (帧级 side data 与裸流 SEI 两条路共用)
  void updateHdrMeta(const HdrMeta &meta);
  // HDR 静态元数据缓存(变化才下发)
  HdrMeta hdrMeta = {};
  // SEI RBSP 反仿真复用缓冲
  std::vector<uint8_t> seiRbsp;
  // 故障注入解码器名(mp.decoder.failinject), onOptionChange缓存, open时比对命中即失败
  std::string failInjectName;
};

}