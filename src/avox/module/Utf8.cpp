#include "Utf8.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace avox {

namespace {

// 10xxxxxx 续字节。
inline bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// 从 i 起量一个合法 UTF-8 序列的长度; 起始非法返回 0。
// 严格校验: 拒绝 overlong (C0/C1、E0 后 <A0、F0 后 <90)、代理区 (ED 后 >9F)、
// 超 U+10FFFF (F4 后 >8F) 与裸续字节 (80-BF) / F5-FF。
int sequenceLengthAt(const std::string& text, size_t i) {
  const unsigned char c = static_cast<unsigned char>(text[i]);
  const size_t left = text.size() - i;
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) {
    return left >= 2 && isContinuation(static_cast<unsigned char>(text[i + 1])) ? 2 : 0;
  }
  if (c == 0xE0) {
    return left >= 3 && static_cast<unsigned char>(text[i + 1]) >= 0xA0
                       && static_cast<unsigned char>(text[i + 1]) <= 0xBF
                       && isContinuation(static_cast<unsigned char>(text[i + 2])) ? 3 : 0;
  }
  if ((c >= 0xE1 && c <= 0xEC) || (c >= 0xEE && c <= 0xEF)) {
    return left >= 3 && isContinuation(static_cast<unsigned char>(text[i + 1]))
                       && isContinuation(static_cast<unsigned char>(text[i + 2])) ? 3 : 0;
  }
  if (c == 0xED) {
    return left >= 3 && static_cast<unsigned char>(text[i + 1]) >= 0x80
                       && static_cast<unsigned char>(text[i + 1]) <= 0x9F
                       && isContinuation(static_cast<unsigned char>(text[i + 2])) ? 3 : 0;
  }
  if (c == 0xF0) {
    return left >= 4 && static_cast<unsigned char>(text[i + 1]) >= 0x90
                       && static_cast<unsigned char>(text[i + 1]) <= 0xBF
                       && isContinuation(static_cast<unsigned char>(text[i + 2]))
                       && isContinuation(static_cast<unsigned char>(text[i + 3])) ? 4 : 0;
  }
  if (c >= 0xF1 && c <= 0xF3) {
    return left >= 4 && isContinuation(static_cast<unsigned char>(text[i + 1]))
                       && isContinuation(static_cast<unsigned char>(text[i + 2]))
                       && isContinuation(static_cast<unsigned char>(text[i + 3])) ? 4 : 0;
  }
  if (c == 0xF4) {
    return left >= 4 && static_cast<unsigned char>(text[i + 1]) >= 0x80
                       && static_cast<unsigned char>(text[i + 1]) <= 0x8F
                       && isContinuation(static_cast<unsigned char>(text[i + 2]))
                       && isContinuation(static_cast<unsigned char>(text[i + 3])) ? 4 : 0;
  }
  return 0;
}

}  // namespace

bool isValidUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const int len = sequenceLengthAt(text, i);
    if (len == 0) return false;
    i += static_cast<size_t>(len);
  }
  return true;
}

std::string patchInvalidUtf8(const std::string& text) {
  if (isValidUtf8(text)) return text;
  std::string out;
  out.reserve(text.size() + 8);
  for (size_t i = 0; i < text.size();) {
    const int len = sequenceLengthAt(text, i);
    if (len == 0) {
      // 逐字节吞掉脏序列: 每个非法字节换一个 U+FFFD, 不会把合法邻居一起吃掉。
      out += "\xEF\xBF\xBD";
      ++i;
      continue;
    }
    out.append(text, i, static_cast<size_t>(len));
    i += static_cast<size_t>(len);
  }
  return out;
}

std::string ensureUtf8(const std::string& text) {
  if (isValidUtf8(text)) return text;
#ifdef _WIN32
  // 整体重编码: 本机 ANSI (中文 Windows = GBK/936)。dwFlags=0 时未定义序列自动落
  // U+FFFD 而不是整串失败 —— 与「转不动的字节变 U+FFFD」语义一致。
  const int wideLen = MultiByteToWideChar(936, 0, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
  if (wideLen > 0) {
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()),
                        wide.data(), wideLen);
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen,
                                            nullptr, 0, nullptr, nullptr);
    if (utf8Len > 0) {
      std::string utf8(static_cast<size_t>(utf8Len), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, utf8.data(), utf8Len,
                          nullptr, nullptr);
      return utf8;
    }
  }
#endif
  return patchInvalidUtf8(text);
}

}
