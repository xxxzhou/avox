#include "PacketBuf.hpp"

#include <algorithm>

#include "../module/LogHelper.hpp"

namespace avox {

bool checkAvccPacket(const uint8_t* data, int32_t size) {
  // annexb 4肯定不是avcc包，但是ann3可能是avcc包
  if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
    return false;
  } else {
    uint32_t avccSize =
        ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
    avccSize += 4;
    while (avccSize <= size) {
      // 如果长度对的上，说明是avcc包
      if (avccSize == size) {
        return true;
      }
      if (avccSize > size + 4) {
        break;
      }
      // 下一包的长度
      uint32_t nextSize = ((data[avccSize] << 24) | (data[avccSize + 1] << 16) |
                           (data[avccSize + 2] << 8) | data[avccSize + 3]);
      avccSize += nextSize + 4;
    }
  }
  return false;
}

void copyBuf(PacketBufPtr& pack, const AvoxPacket& data) {
  if (pack) {
    pack->form(data);
  }
  pack = std::make_shared<PacketBuf>(data);
}

void PacketBuf::form(const AvoxPacket& packet) {
  frameType = packet.frameType;
  packtype = packet.packtype;
  pts = packet.pts;
  dts = packet.dts;
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

int32_t PacketBuf::getNaluType(VCodecId codeId) {
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
  bool bHaveData =
      std::any_of(configPackets.begin(), configPackets.end(), sameDataFunc);
  // 检查是否已存在相同配置数据
  if (!bHaveData) {
    bool bUpdate = false;
    PacketBuf buf(data);
    for (int32_t i = 0; i < configPackets.size(); ++i) {
      if (configPackets[i].getNaluType(codecId) == buf.getNaluType(codecId)) {
        configPackets[i] = buf;
        bUpdate = true;
        return ConfigAddType::update;
      }
    }
    if (!bUpdate) {
      configPackets.push_back(buf);
      return ConfigAddType::add;
    }
  }
  return ConfigAddType::duplicate;
}

AvoxPacket getPacket(PacketBufPtr packet) { return getPacket(*packet); }
AvoxPacket getPacket(PacketBuf& packet) {
  AvoxPacket aPacket = {};
  aPacket.data = {packet.buff.data(), packet.size, true};
  aPacket.pts = packet.pts;
  aPacket.dts = packet.dts;
  aPacket.prefixSize = packet.prefixSize;
  aPacket.frameType = packet.frameType;
  aPacket.packtype = packet.packtype;
  return aPacket;
}

}
