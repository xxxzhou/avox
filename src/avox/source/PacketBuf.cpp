#include "PacketBuf.hpp"

#include <algorithm>

#include "../module/LogHelper.hpp"

namespace avox {

bool checkAvccPacket(const uint8_t* data, int32_t size) {
  // annexb 4肯定不是avcc包，但是ann3可能是avcc包
  if (size < 4) {
    return false;
  }
  if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
    return false;
  }
  // 长度链逐环校验。游标运算必须 64 位: 垃圾包里 FC FF FF FF 的
  // nextSize+4 在 uint32 回绕成 0, 游标原地踏步死循环, IO 线程冻死
  // 整个播放器(极空间 seek 后 partial 风暴期的错位包实测踩中);
  // 每环至少前进 4 字节, 循环有界必终止
  uint32_t pos = 0;
  while (pos < (uint32_t)size) {
    if ((uint64_t)pos + 4 > (uint64_t)size) {
      return false;
    }
    const uint64_t nextSize =
        ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos + 1] << 16) |
        ((uint32_t)data[pos + 2] << 8) | (uint32_t)data[pos + 3];
    if ((uint64_t)pos + 4 + nextSize > (uint64_t)size) {
      return false;
    }
    pos = (uint32_t)(pos + 4 + nextSize);
  }
  return true;
}

void copyBuf(PacketBufPtr& pack, const AvoxPacket& data) {
  // 环形队列的槽在 dequeue 后残留 shared_ptr, 回绕复用该槽时 pack 指向的
  // 旧对象可能仍被下游持有(flattener 扣住的簇/解码侧), 就地 form 会改写
  // 它们的 pts/size/buff —— 本地文件读满队列(1000 槽≈33s)回绕恰踩扣住的
  // 首簇, WMV3 起播首 I 帧被改写成 GOP 中段 P 帧(开头花屏 3 秒的根因)。
  // 无条件换新对象, 不复用旧包(下面 form 本就白做, pack 随即被替换)。
  (void)pack;
  pack = std::make_shared<PacketBuf>(data);
}

void PacketBuf::form(const AvoxPacket& packet) {
  frameType = packet.frameType;
  packtype = packet.packtype;
  pts = packet.pts;
  dts = packet.dts;
  duration = packet.duration;
  const uint8_t* pdata = packet.data.data;
  prefixSize = packet.prefixSize;
  // annexb 3转成成annexb 4,方便后续统一处理
  int32_t bAnnexB3 = 0;
  PackType packType = (PackType)packtype;
  if (packType == PackType::video || packType == PackType::vconfig) {
    prefixSize = 4;
    bool bavcc = checkAvccPacket(pdata, packet.data.size);
    // annexb3 与 avcc可能混淆，所以这里要判断一下
    if (!bavcc) {
      if (pdata[0] == 0x00 && pdata[1] == 0x00 && pdata[2] == 0x01) {
        bAnnexB3 = 1;
      }
    }
  }
  size = packet.data.size + bAnnexB3;
  // 只有在本身少于packet.data.size才可能去调整
  if (buff.size() < size) {
    buff.resize(size);
  }
  memcpy(buff.data() + bAnnexB3, packet.data.data, packet.data.size);
  if (bAnnexB3) {
    buff[0] = 0x00;
  }
}

void PacketBuf::form(const PacketBuf& packet) {
  frameType = packet.frameType;
  packtype = packet.packtype;
  prefixSize = packet.prefixSize;
  pts = packet.pts;
  dts = packet.dts;
  duration = packet.duration;
  size = packet.size;
  // 只有在本身少于packet.data.size才可能去调整
  if (buff.size() < packet.size) {
    buff.resize(packet.size);
  }
  memcpy(buff.data(), packet.buff.data(), packet.size);
}
// packet是视频包
void PacketBuf::append(const AvoxPacket& packet) {
  if (packet.data.size < 4) {
    return;
  }
  int32_t bAnnexB3 = 0;
  const uint8_t* pdata = packet.data.data;
  bool bavcc = checkAvccPacket(pdata, packet.data.size);
  // annexb3 与 avcc很容易混淆，所以这里要判断一下
  if (!bavcc) {
    if (pdata[0] == 0x00 && pdata[1] == 0x00 && pdata[2] == 0x01) {
      bAnnexB3 = 1;
    }
  }
  // 奇怪,ffmpeg这里要把多个子I帧合并，但是又不能去掉0001
  int32_t newSize = size + packet.data.size + bAnnexB3;
  if (buff.size() < newSize) {
    buff.resize(newSize);
  }
  if (bAnnexB3) {
    buff[size] = 0x00;
  }
  memcpy(buff.data() + size + bAnnexB3, pdata, packet.data.size);
  size = newSize;
}

void PacketBuf::append(const PacketBuf& packet) {
  // 只要里面有I帧,整个包就标记为I帧
  if (packet.frameType == 1) {
    frameType = 1;
  }
  int32_t newSize = size + packet.size;
  if (buff.size() < newSize) {
    buff.resize(newSize);
  }
  memcpy(buff.data() + size, packet.buff.data(), packet.size);
  size = newSize;
}

int32_t PacketBuf::getNaluType(VCodecId codeId) const {
  if (buff.size() <= prefixSize) {
    return 0;
  }
  if (codeId == VCodecId::h264) {
    return buff[prefixSize] & 0x1f;
  }
  if (codeId == VCodecId::h265) {
    return (buff[prefixSize] >> 1) & 0x3f;
  }
  return 0;
}

// 参数集id提取: 同id才允许替换。同一条流可有多个不同id的PPS/SPS并存
// (微信导出HEVC实测: IDR切片用pps_id=0、非IDR切片用pps_id=1, 缺一个
// 解码全灭), 按类型整体替换会挤掉另一个id → extradata缺一半参数集。
// id取法: h265 VPS/SPS = nal头后u(4); h265 PPS = 头后ue(v);
//         h264 SPS = 头后跳profile/constraints/level 3字节ue(v); h264 PPS = ue(v)
static int32_t paramSetId(VCodecId codecId, const PacketBuf& buf) {
  int32_t left = buf.size - buf.prefixSize;
  const uint8_t* p = buf.buff.data() + buf.prefixSize;
  if (left < 4) {
    return -1;
  }
  if (codecId == VCodecId::h265) {
    uint8_t type = (p[0] >> 1) & 0x3F;
    if (type == 32 || type == 33) {
      return p[2] & 0x0F;
    }
    // h265 PPS: ue(v)在2字节nal头之后
    p += 2;
    left -= 2;
  } else if (codecId == VCodecId::h264) {
    // h264 nal头1字节; SPS的ue(v)还要再跳profile/constraints/level 3字节
    uint8_t type = p[0] & 0x1F;
    p += 1;
    left -= 1;
    if (type == 7) {
      p += 3;
      left -= 3;
    }
  }
  int32_t bitPos = 0;
  auto readBit = [&]() -> int32_t {
    if ((bitPos >> 3) >= left) {
      return -1;
    }
    int32_t bit = (p[bitPos >> 3] >> (7 - (bitPos & 7))) & 1;
    bitPos++;
    return bit;
  };
  int32_t zeros = 0;
  while (readBit() == 0) {
    zeros++;
    if (zeros > 16) {
      return -1;
    }
  }
  if (zeros == 0) {
    return 0;
  }
  int32_t tail = 0;
  for (int32_t i = 0; i < zeros; ++i) {
    int32_t bit = readBit();
    if (bit < 0) {
      return -1;
    }
    tail = (tail << 1) | bit;
  }
  return (1 << zeros) - 1 + tail;
}

ConfigAddType addConfigPacket(std::vector<PacketBuf>& configPackets,
                              const PacketBuf& data, VCodecId codecId) {
  // 子类在开始解码时，解码这些配置数据，并检查是否变化
  // 网络流可能每个IDR帧都会重新发送
  auto sameDataFunc = [&](const PacketBuf& buf) {
    // 不比较头
    int32_t bufSize = buf.size - buf.prefixSize;
    int32_t dataSize = data.size - data.prefixSize;
    return bufSize == dataSize &&
           std::memcmp(buf.buff.data()+buf.prefixSize, data.buff.data()+buf.prefixSize, dataSize) == 0;
  };
  // 检查是否已存在相同配置数据
  bool bHaveData =
      std::any_of(configPackets.begin(), configPackets.end(), sameDataFunc);
  if (!bHaveData) {
    int32_t newId = paramSetId(codecId, data);
    for (int32_t i = 0; i < configPackets.size(); ++i) {
      if (configPackets[i].getNaluType(codecId) != data.getNaluType(codecId)) {
        continue;
      }
      // 同类型但id不同(如双PPS)的参数集并存保留, 只有同id才是内容更新
      if (newId >= 0 && paramSetId(codecId, configPackets[i]) != newId) {
        continue;
      }
      PacketBuf buf(data);
      configPackets[i] = buf;
      return ConfigAddType::update;
    }
    // 防病态流(参数集内容逐IDR轮换)无限增长
    if (configPackets.size() >= 16) {
      return ConfigAddType::duplicate;
    }
    PacketBuf buf(data);
    configPackets.push_back(buf);
    return ConfigAddType::add;
  }
  return ConfigAddType::duplicate;
}

AvoxPacket getPacket(PacketBufPtr packet) { return getPacket(*packet); }
AvoxPacket getPacket(PacketBuf& packet) {
  AvoxPacket aPacket = {};
  aPacket.data = {packet.buff.data(), packet.size, true};
  aPacket.pts = packet.pts;
  aPacket.dts = packet.dts;
  aPacket.duration = packet.duration;
  aPacket.prefixSize = packet.prefixSize;
  aPacket.frameType = packet.frameType;
  aPacket.packtype = packet.packtype;
  return aPacket;
}

}
