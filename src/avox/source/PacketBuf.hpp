#pragma once

#include <memory>
#include <vector>

#include "../AvoxBuffer.h"
#include "../AvoxCodec.h"
#include "../AvoxPlayer.h"

namespace avox {

class PacketBufPool;

using PacketBufPtr = std::shared_ptr<class PacketBuf>;

// 检查是否avcc的包,注意看起来annexb三字节头可能是avcc的
AVOX_EXPORT bool checkAvccPacket(const uint8_t* data, int32_t size);

// AvoxPacket放入队列的数据, 每包一个 shared_ptr
// 走池时按本次尺寸取档(小包永不被大包撑大), 走栈/拷贝时按贴合复用(不超 4 倍)
// 队列槽对象复用见 copyBuf: 仅当本线程是唯一持有者时原地复用
class AVOX_EXPORT PacketBuf {
 public:
  PacketBuf() = default;
  ~PacketBuf();
  PacketBuf(const AvoxPacket& packet) { form(packet); };
  PacketBuf(const PacketBuf& packet);
  PacketBuf& operator=(const PacketBuf& packet);
  PacketBuf(PacketBuf&& packet) noexcept;
  PacketBuf& operator=(PacketBuf&& packet) noexcept;
  PacketBuf(const std::vector<uint8_t>& data) {
    buff = data;
    size = buff.size();
  }

  void form(const AvoxPacket& packet);
  void form(const PacketBuf& packet);
  // 走缓冲池(playback 包队列用): 池须活到本对象析构, 传空等价于不带池
  void form(const AvoxPacket& packet, const std::shared_ptr<PacketBufPool>& pool);
  void form(const PacketBuf& packet, const std::shared_ptr<PacketBufPool>& pool);
  // 池化回收: 交还块+清零字段(等价析构), 对象本体由 PacketBufPool 复用
  void recycle();
  // packet是视频包
  void append(const AvoxPacket& packet);
  void append(const PacketBuf& packet);
  int32_t getNaluType(VCodecId codeId) const;

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
  // 包时长(ms): 音频NOPTS合成推进用(录制链)
  int32_t duration = 0;
  // 这个size是buff有效长度,这个长度一定少于buff.size()
  int32_t size = 0;
  // 实际数据, size 为有效长度, 容量按贴合复用/池档位维护
  std::vector<uint8_t> buff;

 private:
  // 按 need 备好 buff: 池模式同档只改逻辑长度, 换档/退出池先把老块还回去
  void fitBuffer(int32_t need, const std::shared_ptr<PacketBufPool>& want);
  // 摘掉池关系(归还老块), 拷贝/移动语义都不共享块
  void leavePool();

  // 所属池与档位(-1=普通分配); 池用 shared_ptr, 包比池活得久也不会悬空
  std::shared_ptr<PacketBufPool> pool;
  int32_t poolClass = -1;
};

ConfigAddType addConfigPacket(std::vector<PacketBuf>& configPackets,
                              const PacketBuf& data, VCodecId codecId);
AvoxPacket getPacket(PacketBufPtr packet); 
AvoxPacket getPacket(PacketBuf& packet);
// 入队拷贝: 槽对象在本线程独占时原地复用, 否则换新对象
void copyBuf(PacketBufPtr& pack, const AvoxPacket& data,
             const std::shared_ptr<PacketBufPool>& pool = nullptr);

}
