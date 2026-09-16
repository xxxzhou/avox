// 字幕文本编码归一单测(外挂 GBK 字幕修复: sub-ext-srt-gbk)
// 覆盖: UTF-8 原样/剥 BOM; UTF-16 BOM 不误判; 严格 UTF-8 校验; GBK→UTF-8;
// GBK 外挂 SRT 端到端解析出正确文本。
#include <doctest.h>

#include <string>
#include <vector>

#include "avox/subtitle/CharsetConvert.hpp"
#include "avox/subtitle/SrtParser.hpp"

namespace {

// "你好": GBK / UTF-8 两种字节序列(对照用)
const std::string kGbkNiHao = "\xC4\xE3\xBA\xC3";
const std::string kUtf8NiHao = "\xE4\xBD\xA0\xE5\xA5\xBD";

// 本平台是否具备 GBK→UTF-8 能力(与 CharsetConvert.cpp 的平台分支保持一致)
#if defined(_WIN32) || defined(__APPLE__)
#define AVOX_TEST_HAVE_GBK 1
#elif defined(__has_include)
#if __has_include(<iconv.h>)
#define AVOX_TEST_HAVE_GBK 1
#endif
#endif

}  // namespace

TEST_CASE("字幕编码: UTF-8 原样返回, 判为合法") {
  const std::string utf8 =
      std::string("1\n00:00:01,000 --> 00:00:02,000\n") + kUtf8NiHao + "\n";
  std::string out;
  CHECK(avox::normalizeSubtitleText(utf8, out) == false);
  CHECK(out == utf8);
  CHECK(avox::isUtf8Text(utf8.data(), utf8.size()));
  CHECK(avox::isUtf8Text("hello", 5));
  CHECK(avox::isUtf8Text("", 0));
}

TEST_CASE("字幕编码: 剥 UTF-8 BOM(否则首条序号行判不出数字被丢)") {
  const std::string bomText = std::string("\xEF\xBB\xBF") + "1\nabc";
  std::string out;
  CHECK(avox::normalizeSubtitleText(bomText, out) == false);
  CHECK(out == "1\nabc");
  CHECK(out[0] == '1');
}

TEST_CASE("字幕编码: UTF-16 BOM 不按 GBK 误转") {
  const std::string utf16le("\xFF\xFE\x31\x00", 4);  // BOM + "1" (LE)
  std::string out;
  CHECK(avox::normalizeSubtitleText(utf16le, out) == false);
  CHECK(out == utf16le);  // 原样返回(当前不支持 UTF-16, 由上层决定)
}

TEST_CASE("字幕编码: 严格 UTF-8 校验拒绝非法序列") {
  CHECK_FALSE(avox::isUtf8Text("\xC0\xAF", 2));          // 过长编码
  CHECK_FALSE(avox::isUtf8Text("\xE4\xBD", 2));          // 截断
  CHECK_FALSE(avox::isUtf8Text("\x80", 1));              // 孤立续字节
  CHECK_FALSE(avox::isUtf8Text("\xED\xA0\x80", 3));      // UTF-16 代理区
  CHECK_FALSE(avox::isUtf8Text("\xF5\x80\x80\x80", 4));  // 超出 0x10FFFF
  CHECK_FALSE(avox::isUtf8Text(kGbkNiHao.data(), kGbkNiHao.size()));
}

TEST_CASE("字幕编码: GBK 转 UTF-8") {
  std::string out;
  const bool bConverted = avox::normalizeSubtitleText(kGbkNiHao, out);
#ifdef AVOX_TEST_HAVE_GBK
  CHECK(bConverted == true);
  CHECK(out == kUtf8NiHao);
  CHECK(avox::isUtf8Text(out.data(), out.size()));
#else
  CHECK(bConverted == false);  // 平台无转换能力: 原样返回(如部分 Android NDK)
  CHECK(out == kGbkNiHao);
#endif
}

TEST_CASE("字幕编码: 非法 UTF-8 且非法 GBK 时原样返回") {
  const std::string junk("\xFF\xFF\xFF", 3);  // 既非 UTF-8 也非 GBK 前导字节
  std::string out;
  CHECK(avox::normalizeSubtitleText(junk, out) == false);
  CHECK(out == junk);
}

TEST_CASE("字幕编码: GBK 外挂 SRT 端到端解析出正确文本") {
  const std::string raw =
      std::string("1\n00:00:01,000 --> 00:00:02,000\n") + kGbkNiHao + "\n";
  std::string text;
  const bool bConverted = avox::normalizeSubtitleText(raw, text);
  std::vector<avox::SrtParser::RawItem> items;
  REQUIRE(avox::SrtParser::parse(text.c_str(), text.size(), items));
  REQUIRE(items.size() == 1);
  CHECK(items[0].startMs == 1000);
  CHECK(items[0].endMs == 2000);
#ifdef AVOX_TEST_HAVE_GBK
  CHECK(bConverted);
  CHECK(items[0].text == kUtf8NiHao);
#else
  CHECK_FALSE(bConverted);
#endif
}
