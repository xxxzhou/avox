#include "SubtitleScan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace avox {

namespace {

namespace fs = std::filesystem;

// C++17/20 双兼容: C++20 起 u8path 弃用、u8string() 返回 std::u8string
#if defined(__cpp_char8_t)
fs::path utf8Path(const std::string& s) {
  return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
}
std::string pathUtf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
#else
fs::path utf8Path(const std::string& s) { return fs::u8path(s); }
std::string pathUtf8(const fs::path& p) { return p.u8string(); }
#endif

// 常见语言标记段(小写匹配, hint 保留原串)
bool parseLangToken(const std::string& tok, std::string* lang) {
  static const char* kLangs[] = {"zh",     "chs",    "cht",   "gb",  "big5",
                                 "eng",    "en",     "zhhans", "zhhant", "zhcn",
                                 "zhtw",   "jpn",    "jp",    "kor", "kr"};
  for (const char* k : kLangs) {
    if (tok == k) {
      *lang = tok;
      return true;
    }
  }
  return false;
}

std::string lowerAscii(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return (char)std::tolower(c);
  });
  return out;
}

}  // namespace

int32_t listSubtitleCandidates(const std::string& videoUrl,
                               std::vector<SubtitleCandidateInfo>* out) {
  if (out) {
    out->clear();
  }
  // 非本地路径 v1 不扫(远程源的字幕发现归各自会话层)
  if (videoUrl.size() < 2 || videoUrl.find("://") != std::string::npos) {
    return 0;
  }
  const fs::path video = utf8Path(videoUrl);
  const fs::path dir = video.parent_path();
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return 0;
  }
  // 主名: 视频文件名去最后扩展名(经 pathUtf8 归一成 UTF-8 态比较)
  const std::string mainName = lowerAscii(pathUtf8(video.stem()));
  if (mainName.empty()) {
    return 0;
  }
  struct Ranked : SubtitleCandidateInfo {
    int nameTier = 1;  // 0=完全同名 1=带标记段
  };
  std::vector<Ranked> found;
  fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied,
                            ec);
  for (fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string lower = lowerAscii(pathUtf8(it->path().filename()));
    // 扩展名过滤(三个后缀等长 4)
    if (lower.size() <= 4) continue;
    const std::string ext = lower.substr(lower.size() - 4);
    SCodecId codec = SCodecId::ass;
    if (ext == ".srt") {
      codec = SCodecId::srt;
    } else if (ext == ".ass" || ext == ".ssa") {
      codec = SCodecId::ass;
    } else {
      continue;
    }
    // 主名匹配: 去扩展名 == 主名, 或「主名+单个分隔符+标记段」开头
    // (movies.srt 对 movie.mkv 不算命中, movie-zh.srt / movie.zh.srt 算)
    const std::string stem = lower.substr(0, lower.size() - 4);
    std::string tags;
    int nameTier = 1;
    if (stem == mainName) {
      nameTier = 0;
    } else if (stem.size() > mainName.size() + 1 &&
               stem.compare(0, mainName.size(), mainName) == 0) {
      const char sep = stem[mainName.size()];
      if (sep != '.' && sep != '-' && sep != '_') {
        continue;
      }
      tags = stem.substr(mainName.size() + 1);
    } else {
      continue;
    }
    // 标记段解析: .gbk 编码后缀 + 首个语言段; 其余段(.forced/.sdh 等)忽略
    Ranked c;
    c.codecId = codec;
    c.nameTier = nameTier;
    size_t start = 0;
    while (start < tags.size()) {
      size_t dot = tags.find('.', start);
      const std::string tok =
          tags.substr(start, dot == std::string::npos ? std::string::npos
                                                      : dot - start);
      if (tok == "gbk") {
        c.gbkHint = true;
      } else if (c.lang.empty()) {
        parseLangToken(tok, &c.lang);  // 非语言段解析失败即忽略
      }
      if (dot == std::string::npos) {
        break;
      }
      start = dot + 1;
    }
    c.path = pathUtf8(it->path());
    found.push_back(std::move(c));
  }
  std::stable_sort(found.begin(), found.end(),
                   [](const Ranked& a, const Ranked& b) {
                     if (a.nameTier != b.nameTier) return a.nameTier < b.nameTier;
                     const int aSrt = a.codecId == SCodecId::srt ? 0 : 1;
                     const int bSrt = b.codecId == SCodecId::srt ? 0 : 1;
                     if (aSrt != bSrt) return aSrt < bSrt;
                     return a.path < b.path;
                   });
  if (!out) {
    return (int32_t)found.size();
  }
  out->reserve(found.size());
  for (auto& c : found) {
    out->push_back(std::move(c));
  }
  return (int32_t)out->size();
}

}
