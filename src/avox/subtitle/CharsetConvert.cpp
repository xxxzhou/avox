#include "CharsetConvert.hpp"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__has_include)
// 注意 NDK 的 iconv.h 按 __ANDROID_API__ >= 28 才暴露声明: 头文件存在
// (__has_include 为真)但声明被隐藏, 只查头文件会编译失败 → 加 API 条件,
// android-26 平台落 #else 回退(GBK 字幕原样透传); 升 android-28 平台即恢复
#if __has_include(<iconv.h>) && (!defined(__ANDROID__) || __ANDROID_API__ >= 28)
#include <iconv.h>
#define AVOX_SUBTITLE_HAVE_ICONV 1
#endif
#endif

namespace avox {
namespace {

// Windows 的 GBK 代码页(CP936); GB18030 需 54936, 但 936 覆盖常见 GBK 字幕
constexpr unsigned int kCodePageGbk = 936;

bool isUtf16Bom(const char* data, size_t size) {
  if (size < 2) {
    return false;
  }
  const uint8_t b0 = static_cast<uint8_t>(data[0]);
  const uint8_t b1 = static_cast<uint8_t>(data[1]);
  return (b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF);
}

size_t utf8BomLen(const char* data, size_t size) {
  if (size >= 3 && static_cast<uint8_t>(data[0]) == 0xEF &&
      static_cast<uint8_t>(data[1]) == 0xBB &&
      static_cast<uint8_t>(data[2]) == 0xBF) {
    return 3;
  }
  return 0;
}

#if defined(_WIN32)

// 严格模式(ERR_INVALID_CHARS)解 CP936: 字节不是合法 GBK 时直接失败
bool gbkToUtf8(const std::string& raw, std::string& out) {
  const int wlen = MultiByteToWideChar(kCodePageGbk, MB_ERR_INVALID_CHARS,
                                       raw.data(), static_cast<int>(raw.size()),
                                       nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::wstring wide(static_cast<size_t>(wlen), L'\0');
  if (MultiByteToWideChar(kCodePageGbk, 0, raw.data(),
                          static_cast<int>(raw.size()), wide.data(),
                          wlen) <= 0) {
    return false;
  }
  const int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0,
                                       nullptr, nullptr);
  if (ulen <= 0) {
    return false;
  }
  std::string utf8(static_cast<size_t>(ulen), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, utf8.data(), ulen, nullptr,
                      nullptr);
  out.swap(utf8);
  return true;
}

#elif defined(__APPLE__)

// CoreFoundation 自带 GB18030 解码(GBK 的超集), Apple 平台无需额外链接库
bool gbkToUtf8(const std::string& raw, std::string& out) {
  CFStringRef str = CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(raw.data()),
      static_cast<CFIndex>(raw.size()), kCFStringEncodingGB_18030_2000, false);
  if (!str) {
    return false;
  }
  const CFIndex cap =
      CFStringGetMaximumSizeForEncoding(CFStringGetLength(str),
                                        kCFStringEncodingUTF8) +
      1;
  std::vector<char> buf(static_cast<size_t>(cap), '\0');
  const Boolean ok =
      CFStringGetCString(str, buf.data(), cap, kCFStringEncodingUTF8);
  CFRelease(str);
  if (!ok) {
    return false;
  }
  out.assign(buf.data(), std::strlen(buf.data()));
  return true;
}

#elif defined(AVOX_SUBTITLE_HAVE_ICONV)

// GB18030 是 GBK 超集, 用它的名字覆盖面更广(iconv 也认这名字)
bool gbkToUtf8(const std::string& raw, std::string& out) {
  iconv_t cd = iconv_open("UTF-8", "GB18030");
  if (cd == reinterpret_cast<iconv_t>(-1)) {
    return false;
  }
  std::string in = raw;  // iconv 会推进输入指针
  std::vector<char> buf(raw.size() * 4 + 16, '\0');
  char* ip = in.data();
  size_t il = in.size();
  char* op = buf.data();
  size_t ol = buf.size();
  const size_t ret = iconv(cd, &ip, &il, &op, &ol);
  iconv_close(cd);
  if (ret == static_cast<size_t>(-1)) {
    return false;
  }
  out.assign(buf.data(), buf.size() - ol);
  return true;
}

#else

// Android(bionic 无 iconv 保证)等: 无转换能力, 由调用方按原样处理
bool gbkToUtf8(const std::string&, std::string&) { return false; }

#endif

}  // namespace

bool isUtf8Text(const char* data, size_t size) {
  if (!data) {
    return false;
  }
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t i = 0;
  while (i < size) {
    const uint8_t c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t extra = 0;
    uint32_t code = 0;
    if (c >= 0xC2 && c <= 0xDF) {  // 0xC0/0xC1 属过长编码
      extra = 1;
      code = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
      extra = 2;
      code = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {  // >=0xF5 超出 Unicode 范围
      extra = 3;
      code = c & 0x07u;
    } else {
      return false;
    }
    if (i + extra >= size) {
      return false;  // 截断的多字节序列
    }
    for (size_t k = 1; k <= extra; ++k) {
      const uint8_t cc = p[i + k];
      if ((cc & 0xC0u) != 0x80u) {
        return false;
      }
      code = (code << 6) | (cc & 0x3Fu);
    }
    if (extra == 2 && code < 0x800u) {
      return false;  // 过长编码
    }
    if (extra == 3 && (code < 0x10000u || code > 0x10FFFFu)) {
      return false;
    }
    if (code >= 0xD800u && code <= 0xDFFFu) {
      return false;  // UTF-16 代理区不应出现在 UTF-8
    }
    i += extra + 1;
  }
  return true;
}

SubtitleEncoding normalizeSubtitleText(const std::string& raw, std::string& out) {
  if (raw.empty()) {
    out.clear();
    return SubtitleEncoding::unknown;
  }
  const char* data = raw.data();
  const size_t size = raw.size();
  if (isUtf16Bom(data, size)) {
    out = raw;  // UTF-16: 不做 GBK 误转(需要时再补 UTF-16 分支)
    return SubtitleEncoding::utf16Raw;
  }
  const size_t bom = utf8BomLen(data, size);
  if (isUtf8Text(data + bom, size - bom)) {
    // 已是 UTF-8: 剥 BOM。留着会让首条序号行("\\xEF\\xBB\\xBF1")判不出数字被丢掉
    out.assign(data + bom, size - bom);
    return bom > 0 ? SubtitleEncoding::utf8BomStripped : SubtitleEncoding::utf8;
  }
  std::string converted;
  if (gbkToUtf8(raw, converted)) {
    out.swap(converted);
    return SubtitleEncoding::gbkTranscoded;
  }
  out = raw;  // 平台不支持或不是合法 GBK: 保底原样, 由上层决定是否报错
  return SubtitleEncoding::unknown;
}

const char* getSubtitleEncodingName(SubtitleEncoding encoding) {
  switch (encoding) {
#define XX(name, value, str)   \
  case SubtitleEncoding::name: \
    return str;
    AVOX_MAP_SUB_ENCODING(XX)
#undef XX
    default:
      return "invalid";
  }
}

}  // namespace avox
