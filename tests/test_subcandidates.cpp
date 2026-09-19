// 外挂字幕候选扫描单测(a01-T5): 主名匹配/分隔符边界/标记段解析/排序规则/
// 非本地与缺目录边界; 附 UTF-8 中文路径 openFileUtf8 路径自愈用例。
// 中文路径用显式 UTF-8 字节转义, 不依赖编译器源码字符集。
#include <doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "avox/subtitle/CharsetConvert.hpp"
#include "avox/subtitle/SubtitleScan.hpp"

using namespace avox;

namespace {

namespace fs = std::filesystem;

int64_t nowTick() {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

bool hasSuffix(const std::string& s, const char* suf) {
  const std::string t(suf);
  return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// C++17/20 双兼容: C++20 起 u8path 弃用、u8string() 返回 std::u8string
fs::path utf8Path(const std::string& s) {
#if defined(__cpp_char8_t)
  return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
#else
  return fs::u8path(s);
#endif
}

// 每用例独立临时目录(中文目录名同时作路径自愈的载体)
struct TempDir {
  fs::path path;
  bool chinese = false;

  explicit TempDir(const std::string& tag, bool withChinese = false)
      : chinese(withChinese) {
    // "中文" 的 UTF-8 字节
    const std::string zh = "\xe4\xb8\xad\xe6\x96\x87";
    std::string name = "avox_subscan_" + tag + "_" + std::to_string(nowTick());
    if (withChinese) {
      name = zh + "_" + name;
    }
    path = fs::temp_directory_path() / utf8Path(name);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  std::string u8() const {
#if defined(__cpp_char8_t)
    const std::u8string u = path.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
#else
    return path.u8string();
#endif
  }
  void touch(const std::string& name) {
    std::ofstream f(path / utf8Path(name), std::ios::binary);
    f << "1";
  }
};

}  // namespace

TEST_CASE("candidate scan: match/tier/codec/hints/order") {
  TempDir dir("basic");
  dir.touch("movie.mkv");
  dir.touch("movie.srt");
  dir.touch("movie.zh.srt");
  dir.touch("movie.gbk.srt");
  dir.touch("movie.chs.ass");
  dir.touch("movie-x.ass");
  dir.touch("movies.srt");   // 非分隔符跟随主名: 不命中
  dir.touch("other.srt");    // 不同主名: 不命中
  dir.touch("movie.txt");    // 扩展名不收
  dir.touch("movie.ssa");    // ssa 归 ass

  std::vector<SubtitleCandidateInfo> out;
  const int32_t n = listSubtitleCandidates(dir.u8() + "/movie.mkv", &out);
  // 排序: 同名 > 带标记; srt > ass; 同级按路径
  // tier0: movie.srt | tier1 srt: movie.gbk.srt, movie.zh.srt |
  // tier1 ass: movie-x.ass, movie.chs.ass, movie.ssa
  REQUIRE(n == 6);
  REQUIRE(out.size() == 6);
  // 排序: 同名 > 带标记; srt > ass; 同级按路径。
  // tier0(完全同名): movie.srt, movie.ssa | tier1 srt: movie.gbk.srt,
  // movie.zh.srt | tier1 ass: movie-x.ass, movie.chs.ass
  CHECK(hasSuffix(out[0].path, "movie.srt"));
  CHECK(out[0].codecId == SCodecId::srt);
  CHECK(out[0].lang.empty());
  CHECK_FALSE(out[0].gbkHint);
  CHECK(hasSuffix(out[1].path, "movie.ssa"));
  CHECK(out[1].codecId == SCodecId::ass);
  CHECK(hasSuffix(out[2].path, "movie.gbk.srt"));
  CHECK(out[2].gbkHint);
  CHECK(hasSuffix(out[3].path, "movie.zh.srt"));
  CHECK(out[3].lang == "zh");
  CHECK(hasSuffix(out[4].path, "movie-x.ass"));
  CHECK(out[4].codecId == SCodecId::ass);
  CHECK(hasSuffix(out[5].path, "movie.chs.ass"));
  CHECK(out[5].lang == "chs");
}

TEST_CASE("candidate scan: query count with null out") {
  TempDir dir("count");
  dir.touch("v.mkv");
  dir.touch("v.srt");
  dir.touch("v.ass");
  CHECK(listSubtitleCandidates(dir.u8() + "/v.mkv", nullptr) == 2);
}

TEST_CASE("candidate scan: non-local and missing dir") {
  TempDir dir("edge");
  dir.touch("v.srt");
  std::vector<SubtitleCandidateInfo> out;
  CHECK(listSubtitleCandidates("smb://host/share/v.mkv", &out) == 0);
  CHECK(listSubtitleCandidates("http://h/v.mkv", &out) == 0);
  CHECK(listSubtitleCandidates(dir.u8() + "/nodir/v.mkv", &out) == 0);
  CHECK(listSubtitleCandidates("", &out) == 0);
}

TEST_CASE("openFileUtf8: chinese path roundtrip") {
  TempDir dir("path", true);  // 目录名含中文
  const std::string file = dir.u8() + "/\xe5\xad\x97\xe5\xb9\x95.srt";  // 字幕.srt
  FILE* f = openFileUtf8(file.c_str(), "wb");
  REQUIRE(f != nullptr);
  const char data[] = "1\n00:00:01,000 --> 00:00:02,000\nhi\n";
  std::fwrite(data, 1, sizeof(data) - 1, f);
  std::fclose(f);
  f = openFileUtf8(file.c_str(), "rb");
  REQUIRE(f != nullptr);
  char buf[64] = {};
  const size_t got = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  CHECK(std::string(buf, got) == std::string(data, sizeof(data) - 1));
}
