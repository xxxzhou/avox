#pragma once

#include <memory>
#include <vector>

#include "../AvoxBuffer.h"
#include "../AvoxCodec.h"
#include "../AvoxPlayer.h"

namespace avox {

using PacketBufPtr = std::shared_ptr<class PacketBuf>;

// 检查是否avcc的包,注意看起来annexb三字节头可能是avcc的
AVOX_EXPORT bool checkAvccPacket(const uint8_t* data, int32_t size);

// AvoxPacket放入队列的数据，每个包尽量不重新分配
// 主要就是分配的空间如果大于现在要用，就只复制
// 这样会导致包的大小越到后面就越大，分配的次数就变少
// 一般来说，音频包大小差异不大，所以这个设计没问题
// 而视频因为I帧和P/B帧空间差异大，后面的空间全是I帧空间,有些浪费，后面优化吧
class AVOX_EXPORT PacketBuf {
 public:
  PacketBuf() = default;
  ~PacketBuf() {
    pts = 0;
    buff.clear();
  }
  PacketBuf(const AvoxPacket& packet) { form(packet); };
  PacketBuf(const PacketBuf& packet) { form(packet); }
  PacketBuf(const std::vector<uint8_t>& data) {
    buff = data;
    size = buff.size();
  }

  void form(const AvoxPacket& packet);
  void form(const PacketBuf& packet);
  // packet是视频包
  void append(const AvoxPacket& packet);
  void append(const PacketBuf& packet);
  int32_t getNaluType(VCodecId codeId);

  bool configType() {
    PackType packType = (PackType)packtype;
    return packType == PackType::vconfig || packType == PackType::aconfig;
  }

 public:
  // 视频可能是关键帧 0普通非I帧,1关键帧,2-n 特殊意义,配置变化提示帧
  int32_t frameType = 0;
  // 视频/音频/视频配置/音频配置/字幕 1-5
  int32_t packtype = 0;
  // h264/h265(前3/4或是无意义的,aac前七可能是adts)
  int32_t prefixSize = 0;
  // 渲染时间
  int64_t pts = 0;
  // 解码时间
  int64_t dts = 0;
  // 这个size是buff有效长度,这个长度一定少于buff.size()
  int32_t size = 0;
  // 尽量减少分配次数
  std::vector<uint8_t> buff;
};

ConfigAddType addConfigPacket(std::vector<PacketBuf>& configPackets,
                              const PacketBuf& data, VCodecId codecId);
AvoxPacket getPacket(PacketBufPtr packet); 
AvoxPacket getPacket(PacketBuf& packet);
void copyBuf(PacketBufPtr& pack, const AvoxPacket& data);

}
