#pragma once

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "avox/Avox.hpp"

namespace avox {

/**
 * @brief SHA256 哈希算法实现（无外部依赖）
 */
class Sha256 {
 public:
  Sha256() { reset(); }

  void update(const uint8_t* data, size_t len);
  void finalize(uint8_t digest[32]);

 private:
  void reset();
  void transform(const uint8_t* data);

  uint32_t state[8];
  uint64_t count;
  uint8_t buffer[64];

  static const uint32_t K[64];
};

// ========== SHA256 相关工具函数 ==========

/**
 * @brief 计算 SHA256 哈希
 */
inline std::vector<uint8_t> sha256(const std::string& data) {
  Sha256 sha;
  sha.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  uint8_t digest[32];
  sha.finalize(digest);
  return std::vector<uint8_t>(digest, digest + 32);
}

/**
 * @brief 计算 HMAC-SHA256
 */
inline std::vector<uint8_t> hmacSha256(const std::string& key, const std::string& data) {
  uint8_t ipad[64], opad[64];
  std::memset(ipad, 0x36, 64);
  std::memset(opad, 0x5c, 64);
  for (size_t i = 0; i < key.size() && i < 64; i++) {
    ipad[i] ^= static_cast<uint8_t>(key[i]);
    opad[i] ^= static_cast<uint8_t>(key[i]);
  }
  Sha256 sha;
  sha.update(ipad, 64);
  sha.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  uint8_t inner[32];
  sha.finalize(inner);

  Sha256 sha2;
  sha2.update(opad, 64);
  sha2.update(inner, 32);
  uint8_t result[32];
  sha2.finalize(result);
  return std::vector<uint8_t>(result, result + 32);
}

/**
 * @brief HMAC-SHA256（支持 vector<uint8_t> 作为 key）
 */
inline std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::string& data) {
  std::string keyStr(key.begin(), key.end());
  return hmacSha256(keyStr, data);
}

/**
 * @brief 十六进制编码
 */
inline std::string hexEncode(const std::vector<uint8_t>& data) {
  std::ostringstream oss;
  for (uint8_t b : data) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return oss.str();
}

/**
 * @brief Base64 编码
 */
inline std::string base64Encode(const std::vector<uint8_t>& data) {
  static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  int val = 0, valb = -6;
  for (uint8_t c : data) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      result.push_back(chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
  while (result.size() % 4) result.push_back('=');
  return result;
}

/**
 * @brief URL 编码
 */
inline std::string urlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (char c : value) {
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
    } else {
      escaped << std::uppercase;
      escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
      escaped << std::nouppercase;
    }
  }
  return escaped.str();
}

}
