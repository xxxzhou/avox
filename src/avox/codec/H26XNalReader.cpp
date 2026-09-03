#include "H26XNalReader.hpp"

#include "../module/LogHelper.hpp"

namespace avox {

using namespace std;

static constexpr uint8_t kLeftAndLookUp[8] = {0xFF, 0x7F, 0x3F, 0x1F,
                                              0x0F, 0x07, 0x03, 0x01};
static constexpr uint8_t kRightAndLookUp[8] = {0x80, 0xC0, 0xE0, 0xF0,
                                               0xF8, 0xFC, 0xFE, 0xFF};

H26XNalReader::H26XNalReader(const uint8_t* data, size_t size) {
  setData(data, size);
}

void H26XNalReader::setData(const uint8_t* data_, size_t size_) {
  data = data_;
  size = size_;
  curBitPos = 8;
  curValue = 0;
  zeroCount = 0;
  curPos = 0;
}

void H26XNalReader::ue(uint32_t& value) {
  int32_t leadingZeroBits = -1;

  // 查找连续的零比特，直到遇到第一个非零比特,指数哥伦布编码
  for (uint8_t b = 0; (!b) && (leadingZeroBits < 32); leadingZeroBits++) {
    u(1, b);
    // b 的值只能是 0 或 1，如果读取失败可能是数据越界
    if (b > 1) {
      LOGFLF(LogLevel::warn, "H26XNalReader::ue invalid bit value: %u", b);
      value = 0;
      return;
    }
  }
  uint32_t tmp = 0;
  u(leadingZeroBits, tmp);
  if (leadingZeroBits < 32) {
    value = (1 << leadingZeroBits) - 1 + tmp;
  } else {
    value = 0xffffffff + tmp;
    LOGFLF(LogLevel::warn, "ue failed value: %u", value);
  }
}

void H26XNalReader::se(int32_t& value) {
  uint32_t codeNum = 0;
  ue(codeNum);
  if (codeNum % 2 == 0) {
    value = -static_cast<int32_t>(codeNum >> 1);
  } else {
    value = static_cast<int32_t>((codeNum >> 1) + 1);
  }
}

void H26XNalReader::b8(uint8_t& value) { u(8, value); }

void H26XNalReader::skip(size_t bits) {
  if (bits + curBitPos < 8) {
    curBitPos += bits;
    return;
  } else {
    bits -= (8 - curBitPos);
    curBitPos = 8;
  }
  size_t skipByte = bits / 8;
  if (skipByte > 0) {
    curPos += skipByte;
  }
  ReadOneByteAuto(true);
  curBitPos = (uint8_t)(bits % 8);
}

bool H26XNalReader::end() { return curPos >= size - 1; }

void H26XNalReader::UOperation(size_t bits, uint64_t& value) {
  value = 0;
  bool appendFlag = false;
  do {
    // 读取一个字节,检查防止竞争字节
    ReadOneByteAuto();
    // 如果当前余的比特数大于等于要读取的比特数,则直接读取
    // readBits每次最大为8比特
    size_t readBits = bits <= (size_t)(8 - curBitPos)
                          ? bits
                          : (size_t)(8 - (uint8_t)curBitPos);
    // 还需要读取比特数
    bits = (size_t)(bits - readBits);
    value <<= readBits;
    if (readBits < 8 && !appendFlag) {
      // 如果 readBits 小于 8 且是第一次读取字节，则使用 kLeftAndLookUp
      // 数组来获取当前字节的高位比特。
      value |=
          (curValue & kLeftAndLookUp[curBitPos]) >> (8 - curBitPos - readBits);
    } else if (readBits < 8) {
      value |= (curValue & kRightAndLookUp[readBits - 1]) >>
               (8 - curBitPos - readBits);
    } else {
      value |= curValue;
    }
    curBitPos = curBitPos + (uint8_t)readBits;
    appendFlag = true;
  } while (bits != 0);
}

void H26XNalReader::UPredOperation(size_t bits, uint64_t& value) {
  uint8_t curBitPos_ = curBitPos;
  size_t curPosByte = curPos;

  UOperation(bits, value);

  curPos = curPosByte;
  if (curBitPos_ != 8) {
    ReadOneByteAuto(true);
  }
  curBitPos = curBitPos_;
}

void H26XNalReader::ReadOneByteAuto(bool force) {
  // curBitPos == 8 表示当前字节已经读取完毕，需要读取下一个字节
  if (curBitPos == 8 || force) {
    // 边界检查，防止读取越界
    if (curPos >= size) {
      LOGFLF(LogLevel::warn, "out of bounds: curPos:", curPos," size:", size);
      curValue = 0;
      curBitPos = 0;
      return;
    }

    curValue = data[curPos++];
    // 防竞争字节 0x000003
    if (zeroCount == 2 && curValue == 3) {
      if (curPos >= size) {
        LOGFLF(LogLevel::warn, "anti-competition out of bounds: curPos:", curPos, " size:", size);
        curValue = 0;
        zeroCount = 0;
        curBitPos = 0;
        return;
      }
      curValue = data[curPos++];
      zeroCount = 0;
    }
    if (curValue == 0) {
      zeroCount = (zeroCount + 1) % 3;
    } else {
      zeroCount = 0;
    }
    curBitPos = 0;
  }
}

// bool H26XNalReader::more_rbsp_data() const {
//   // 检查当前字节位置是否已经超过了数据的总长度
//   if (curPos + 3 >= size) {
//     return false;
//   }
//   // 检查当前字节是否为0x00 0x00 0x03 则表示找到了RBSP的结束标志
//   if (data[curPos] == 0x00 && data[curPos + 1] == 0x00 &&
//       data[curPos + 2] == 0x03) {
//     return false;
//   }
//   //
//   如果当前字节不是0x00，或者没有找到RBSP的结束标志，则表示还有更多的RBSP数据可供读取
//   return true;
// }

}