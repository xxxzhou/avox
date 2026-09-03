#pragma once

#include <memory>

#include "../AvoxPlayer.h"
#include "../module/AvBuffer.hpp"
#include "../module/JsonOption.hpp"
#include "../source/PacketBuf.hpp"
#include "../video/VideoBuffer.hpp"

namespace avox {

// IVideoEncoder解码后的回调
// 解码可能是不同线程处理，回调是相对比较合适
class IVideoDecoderOb {
 public:
  IVideoDecoderOb() = default;
  virtual ~IVideoDecoderOb() = default;

 public:
  // 解码器生成成功后回调
  virtual void onVideoDesc() {}
  // 在解码开始的第一个包之前
  virtual void onPacket(PacketBufPtr packet) {};
  // 解码后的CPU数据
  virtual void onDecode(const YUVFrame& frame) = 0;
  // 解码后的GPU数据
  virtual void onDecodeGpu(const GpuFrame& frame) {};
  // 视频解码结束信号
  virtual void onVideoComplete() {}
};

// IAudioEncoder解码后的回调
class IAudioDecoderOb {
 public:
  IAudioDecoderOb() = default;
  virtual ~IAudioDecoderOb() = default;

 public:
  // 比如FAAD是在数据来了才初始化得到输出类型
  // 在这拿AudioDecoder.outDesc才保证正确
  virtual void onAudioDesc() {}
  // 注意onAudioDesc/onFirstPacket没有固定先后顺序
  virtual void onPacket(PacketBufPtr packet) {};
  // 解码后的数据
  virtual void onDecode(const AvoxAFrame& frame) {};
  // 音频解码结束信号
  virtual void onAudioComplete() {}
};

// 音频编解码器的描述信息,用于选择
struct ACodecDesc {
  // 解码器名称
  std::string name = "";
  // 固定标示,ffmpeg对应就是编码器的codecId
  int32_t codecId = -1;
  ACodecId acodecId = ACodecId::none;
  // 是否硬解
  int32_t bHardware = 0;
};

struct VCodecDesc {
  // 解码器名称
  std::string name = "";
  // 解码器描述
  std::string desc = "";
  // 固定标示,ffmpeg对应就是编码器的codecId
  int32_t codecId = -1;
  // 项目内识别的编码
  VCodecId vcodecId = VCodecId::none;
  // 是否硬解
  int32_t bHardware = 0;
};

// 具体到子类，都有些特定设置，因为使用JsonOption
// 单独使用一个JsonOption,需要与context同步
// 直接传入对应的JsonOption指针更好
class AVOX_EXPORT AVDecoder : public OptionLink {
 public:
  AVDecoder() = default;
  virtual ~AVDecoder() = default;

 public:
  void close() { onClose(); }

 public:
  // 子类在得到解码器描述后,可以初始化一些数据
  virtual bool onVaild() { return true; }
  // 配置解码器,需要配置帧准备就绪
  virtual DecodeResult onPreDecoder() { return DecodeResult::noConfig; }
  virtual DecodeResult decode(const AvoxPacket& packet) {
    return DecodeResult::noConfig;
  }
  virtual void flush() {}
  virtual void onClose() {};
};

}
