#include "ToolIo.hpp"

#include <atomic>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#endif

namespace avox {

std::filesystem::path resolveAgentPath(const std::string& raw) {
  std::filesystem::path path(expandEnvPath(raw));
  if (path.is_relative()) path = std::filesystem::current_path() / path;
  return path.lexically_normal();
}

std::string normalizeLf(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    // \r 后紧跟 \n 才是 CRLF; 孤立 \r 原样保留 (dsh 同款语义)。
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
    out += text[i];
  }
  return out;
}

bool checkCrlfDominant(const std::string& raw) {
  const size_t limit = raw.size() < 4096 ? raw.size() : 4096;
  size_t crlf = 0;
  size_t lf = 0;
  for (size_t i = 0; i < limit; ++i) {
    if (raw[i] != '\n') continue;
    if (i > 0 && raw[i - 1] == '\r') {
      crlf++;
    } else {
      lf++;
    }
  }
  return crlf > lf;
}

std::string restoreCrlf(const std::string& lfText) {
  const std::string normalized = normalizeLf(lfText);
  std::string out;
  out.reserve(normalized.size() + 16);
  for (char c : normalized) {
    if (c == '\n') {
      out += "\r\n";
    } else {
      out += c;
    }
  }
  return out;
}

std::optional<std::string> readWholeFile(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return std::nullopt;
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::string content;
  content.resize(static_cast<size_t>(size));
  if (size > 0 && !file.read(content.data(), size)) return std::nullopt;
  return content;
}

bool writeWholeFileAtomic(const std::filesystem::path& path,
                          const std::string& content) {
  std::error_code ec;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
  }
  // 临时文件放同目录 (跨卷 rename 不是原子替换); 计数器防本进程内并发撞名。
  static std::atomic<uint64_t> counter{0};
  const std::filesystem::path temp =
      parent / (path.filename().string() + ".avox-tmp-"
                + std::to_string(counter.fetch_add(1)));
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (!content.empty() && !file.write(content.data(), content.size())) {
      file.close();
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
#ifdef _WIN32
  // std::filesystem::rename 在 Windows 上不覆盖既有文件; MoveFileExW 才行。
  if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
    DeleteFileW(temp.c_str());
    return false;
  }
#else
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    std::remove(temp.c_str());
    return false;
  }
#endif
  return true;
}

}
