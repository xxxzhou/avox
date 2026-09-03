#include "DshLayout.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace avox {

namespace {

// 安全字符: dsh 的 /^[A-Za-z0-9._-]$/ ('~' 不在其内, 由调用方单独判)。
bool isSafeUnit(uint16_t unit) {
  if (unit >= 'A' && unit <= 'Z') return true;
  if (unit >= 'a' && unit <= 'z') return true;
  if (unit >= '0' && unit <= '9') return true;
  return unit == '.' || unit == '_' || unit == '-';
}

// "~XXXX": dsh 用 code.toString(16).toUpperCase().padStart(4, '0')。
void appendEscape(std::string& out, uint16_t unit) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "~%04X", unit);
  out += buffer;
}

// UTF-8 字节流 -> UTF-16 码单元序列 (增补平面拆代理对), 逐单元喂转义 —— 与 dsh
// 在 JS 字符串上逐 charCodeAt 处理逐字节一致。
//
// 非法 UTF-8 序列按每个坏字节一个 U+FFFD 处理 (dsh 域内不会出现非法序列, 这里只求
// 不崩溃且可恢复)。返回码单元个数 (含代理对占两席)。
size_t appendUnits(const std::string& text, std::vector<uint16_t>& units) {
  const size_t start = units.size();
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      units.push_back(lead);
      i += 1;
      continue;
    }
    size_t length = 0;
    uint32_t code = 0;
    uint32_t lowest = 0;
    uint32_t highest = 0;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
      code = lead & 0x1F;
      lowest = 0x80;
      highest = 0x7FF;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      code = lead & 0x0F;
      lowest = 0x800;
      highest = 0xFFFF;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      code = lead & 0x07;
      lowest = 0x10000;
      highest = 0x10FFFF;
    }
    bool ok = length > 0;
    for (size_t k = 1; ok && k < length; ++k) {
      const bool inRange = i + k < text.size();
      const unsigned char cont =
          inRange ? static_cast<unsigned char>(text[i + k]) : 0;
      ok = inRange && (cont & 0xC0) == 0x80;
      if (ok) code = (code << 6) | (cont & 0x3F);
    }
    // 过长编码 / 越界 / 代理区都是非法序列: 退一个字节重来。
    if (!ok || code < lowest || code > highest
        || (code >= 0xD800 && code <= 0xDFFF)) {
      units.push_back(0xFFFD);
      i += 1;
      continue;
    }
    if (code < 0x10000) {
      units.push_back(static_cast<uint16_t>(code));
    } else {
      code -= 0x10000;
      units.push_back(static_cast<uint16_t>(0xD800 + (code >> 10)));
      units.push_back(static_cast<uint16_t>(0xDC00 + (code & 0x3FF)));
    }
    i += length;
  }
  return units.size() - start;
}

}  // namespace

std::string dshEncodeSegment(const std::string& raw) {
  if (raw.empty()) {
    throw std::runtime_error("cannot encode an empty path segment");
  }
  if (raw == ".") return "~002E";
  if (raw == "..") return "~002E~002E";
  std::vector<uint16_t> units;
  appendUnits(raw, units);
  std::string out;
  for (uint16_t unit : units) {
    if (unit != '~' && isSafeUnit(unit)) {
      out.push_back(static_cast<char>(unit));
    } else {
      appendEscape(out, unit);
    }
  }
  return out;
}

std::string dshProjectKey(const std::string& cwd) {
  if (cwd.empty()) {
    throw std::runtime_error("cannot encode an empty project path");
  }
  std::vector<uint16_t> units;
  appendUnits(cwd, units);
  std::string readable;
  bool separatorRun = false;
  for (uint16_t unit : units) {
    const char ch = static_cast<char>(unit);
    if (ch == '/' || ch == '\\' || ch == ':') {
      // 连续分隔符折叠成一个 '-'。
      if (!separatorRun) readable.push_back('-');
      separatorRun = true;
    } else if (unit != '~' && isSafeUnit(unit)) {
      readable.push_back(ch);
      separatorRun = false;
    } else {
      appendEscape(readable, unit);
      separatorRun = false;
    }
  }
  // 去首部 '-' (dsh 的 replace(/^-+/, '')), 全空 -> "root"。
  size_t begin = 0;
  while (begin < readable.size() && readable[begin] == '-') ++begin;
  std::string slug = begin > 0 ? readable.substr(begin) : readable;
  if (slug.empty()) slug = "root";
  // 转义后的 readable 是纯 ASCII, 字符数即码单元数, 截断与 slice(0, 251) 等价。
  if (slug.size() > 251) slug = slug.substr(0, 251);
  return "--" + slug + "--";
}

std::string dshProjectDir(const std::string& root,
                          const std::optional<std::string>& cwd) {
  const std::filesystem::path base(root);
  if (!cwd.has_value()) return (base / "_no-cwd").string();
  return (base / dshProjectKey(*cwd)).string();
}

std::string dshSessionDir(const std::string& root,
                          const std::optional<std::string>& cwd,
                          const SessionId& id) {
  return (std::filesystem::path(dshProjectDir(root, cwd))
          / dshEncodeSegment(id.value))
      .string();
}

std::string dshSessionLogPath(const std::string& root,
                              const std::optional<std::string>& cwd,
                              const SessionId& id) {
  return (std::filesystem::path(dshSessionDir(root, cwd, id)) / "session.jsonl")
      .string();
}

std::string currentPathUtf8() {
  const std::filesystem::path current = std::filesystem::current_path();
#ifdef _WIN32
  // u8string() 恒 UTF-8 (C++20 起 元素是 char8_t, 值就是 UTF-8 码元, 逐个搬进 char)。
  const std::u8string text = current.u8string();
  return std::string(text.begin(), text.end());
#else
  // POSIX 的窄路径本来就是本地编码, 而本地编码在主流部署里就是 UTF-8。
  return current.string();
#endif
}

}
