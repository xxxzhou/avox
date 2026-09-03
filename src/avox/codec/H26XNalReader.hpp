#pragma once

#include <assert.h>

#include <cstdint>
#include <type_traits>

#include "../AvoxDef.h"

namespace avox {

// 参考(https://github.com/HR1025/MMP-H26X)

class H26XNalReader {
 public:
  H26XNalReader(/* args */) = default;
  ~H26XNalReader() = default;
  H26XNalReader(const uint8_t* data, std::size_t size);
  void setData(const uint8_t* data, std::size_t size);

 private:
  // NALU数据指针
  const uint8_t* data = nullptr;
  // NALU数据大小
  std::size_t size = 0;
  // 当前字节位置
  std::size_t curPos = 0;
  // 当前比特位置
  uint8_t curBitPos = 8;
  // 当前字节值
  uint8_t curValue = 0;
  // 零字节计数
  uint32_t zeroCount = 0;

 public:
  // 读取无符号指数哥伦布编码值
  void ue(uint32_t& value);

  // 读取有符号指数哥伦布编码值
  void se(int32_t& value);
  void b8(uint8_t& value);

  void skip(std::size_t bits);

  std::size_t curBits() { return curPos * 8 + curBitPos % 8; }

  bool end();

  // 读取无符号整数（模板实现）
  template <typename T>
  void u(std::size_t bits, T& value, bool probe = false) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integer type");
    assert(bits <= sizeof(T) * 8);
    uint64_t tempValue = 0;
    if (!probe) {
      UOperation(bits, tempValue);
    } else {
      UPredOperation(bits, tempValue);
    }
    value = static_cast<T>(tempValue);
  }

  // 读取有符号整数（模板实现）
  template <typename T>
  void i(std::size_t bits, T& value) {
    static_assert(std::is_signed_v<T>, "T must be a signed integer type");
    assert(bits <= sizeof(T) * 8);
    uint64_t tempValue = 0;
    UOperation(bits, tempValue);
    value = static_cast<T>(tempValue);
  }

 private:
  // 读取比特的核心操作
  void UOperation(std::size_t bits, uint64_t& value);

  // 前瞻读取操作(探测不进位)
  void UPredOperation(std::size_t bits, uint64_t& value);

  // 自动读取一个字节
  void ReadOneByteAuto(bool force = false);
};

}