#include "AssOverlay.hpp"

#ifdef AVOX_ASS_HAVE_LIBASS
#include <cstdio>
#include <cstring>
#include <ass/ass.h>
#endif

namespace avox {

// ================== ASS/PGS overlay 渲染(libass) ==================
// 计划: doc/plan/player/ASS字幕渲染计划.md §3.3
// 链路: extradata/chunk/文件 → libass → ASS_Image 链 → 合成 RGBA canvas(bbox 裁剪,
// 双缓冲) → 核心 VK 图层变化时上传。PGS 不走本文件(FFmpeg pgssub, 见计划 §3.6)。

#ifdef AVOX_ASS_HAVE_LIBASS

bool AssOverlay::init(int32_t storageWidth, int32_t storageHeight) {
  if (bInit) return true;
  auto* lib = ass_library_init();
  if (!lib) return false;
  auto* rend = ass_renderer_init(lib);
  if (!rend) {
    ass_library_done(lib);
    return false;
  }
  ass_set_storage_size(rend, storageWidth, storageHeight);
  ass_set_frame_size(rend, storageWidth, storageHeight);
  // 字体: AUTODETECT(Win=DirectWrite/mac=CoreText/linux=fontconfig);
  // Android 等无系统提供器的平台, 走 fontsDir + defaultFont 兜底
  if (!fontsDir.empty()) {
    ass_set_fonts_dir(lib, fontsDir.c_str());
  }
  ass_set_fonts(rend,
                defaultFontPath.empty() ? nullptr : defaultFontPath.c_str(),
                defaultFontFamily.empty() ? nullptr : defaultFontFamily.c_str(),
                ASS_FONTPROVIDER_AUTODETECT, nullptr, 0);
  assLibrary = lib;
  assRenderer = rend;
  storageW = storageWidth;
  storageH = storageHeight;
  bInit = true;
  return true;
}

void AssOverlay::shutdown() {
  unload();
  if (assRenderer) {
    ass_renderer_done(static_cast<ASS_Renderer*>(assRenderer));
    assRenderer = nullptr;
  }
  if (assLibrary) {
    ass_library_done(static_cast<ASS_Library*>(assLibrary));
    assLibrary = nullptr;
  }
  bInit = false;
}

void AssOverlay::setFontsDir(const char* dir) {
  if (dir) fontsDir = dir;
}

void AssOverlay::setDefaultFont(const char* fontPath, const char* family) {
  if (fontPath) defaultFontPath = fontPath;
  if (family) defaultFontFamily = family;
}

bool AssOverlay::loadTrack(const char* extradata, int32_t size) {
  if (!bInit || !extradata || size <= 0) return false;
  unload();
  auto* track = ass_new_track(static_cast<ASS_Library*>(assLibrary));
  if (!track) return false;
  ass_process_codec_private(track, const_cast<char*>(extradata), size);
  assTrack = track;
  return true;
}

void AssOverlay::processChunk(const char* data, int32_t size, int64_t ptsMs,
                              int64_t durationMs) {
  if (!assTrack) return;
  ass_process_chunk(static_cast<ASS_Track*>(assTrack), const_cast<char*>(data),
                    size, ptsMs, durationMs);
}

bool AssOverlay::loadFile(const char* path) {
  if (!bInit || !path) return false;
  unload();
  const size_t len = std::strlen(path);
  const bool isSrt = len > 4 && std::strcmp(path + len - 4, ".srt") == 0;
  std::string ass;
  if (isSrt) {
    // .srt: 合成最小 ASS 剧本(默认样式 + 底部居中)再喂 libass。
    // 简化解析: 索引行跳过, 时间行含 "-->", 文本行到空行止; <i>/<b> 等标签剥除。
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    std::string srt;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) srt.append(buf, n);
    std::fclose(f);

    char head[256];
    std::snprintf(head, sizeof(head),
                  "[Script Info]\nScriptType: v4.00+\nPlayResX: %d\nPlayResY: %d\n\n"
                  "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
                  "OutlineColour, BackColour, Bold, Italic, BorderStyle, Outline, "
                  "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                  "Style: Default,sans-serif,54,&H00FFFFFF,&H00000000,&H00000000,"
                  "0,0,1,2,0,2,60,60,40,1\n\n"
                  "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                  "MarginR, MarginV, Effect, Text\n",
                  storageW > 0 ? storageW : 1920, storageH > 0 ? storageH : 1080);
    ass += head;

    auto toHms = [](int64_t ms, char* out, size_t cap) {
      int h = int(ms / 3600000), m = int(ms / 60000) % 60,
          s = int(ms / 1000) % 60, cs = int(ms % 1000) / 10;
      std::snprintf(out, cap, "%d:%02d:%02d.%02d", h, m, s, cs);
    };
    int64_t start = 0, end = 0;
    bool inText = false;
    std::string text;
    size_t pos = 0;
    char aBuf[32], bBuf[32];
    while (pos <= srt.size()) {
      size_t eol = srt.find('\n', pos);
      if (eol == std::string::npos) eol = srt.size();
      std::string line = srt.substr(pos, eol - pos);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      pos = eol + 1;
      size_t arrow = line.find("-->");
      if (arrow != std::string::npos) {
        auto trim = [](std::string& s) {
          size_t a = s.find_first_not_of(" \t");
          size_t b = s.find_last_not_of(" \t");
          s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        std::string a_ = line.substr(0, arrow), b_ = line.substr(arrow + 3);
        trim(a_);
        trim(b_);
        auto toMs = [](const std::string& s) {
          int h = 0, m = 0, sec = 0, ms = 0;
          std::sscanf(s.c_str(), "%d:%d:%d,%d", &h, &m, &sec, &ms);
          return int64_t(h) * 3600000 + int64_t(m) * 60000 + int64_t(sec) * 1000 + ms;
        };
        start = toMs(a_);
        end = toMs(b_);
        inText = true;
        text.clear();
        continue;
      }
      if (!inText) continue;  // 索引行/空行(块前)
      if (line.empty() || pos > srt.size()) {
        if (!text.empty()) {
          toHms(start, aBuf, sizeof(aBuf));
          toHms(end, bBuf, sizeof(bBuf));
          ass += std::string("Dialogue: 0,") + aBuf + "," + bBuf +
                 ",Default,,0,0,0,," + text + "\n";
        }
        inText = false;
        text.clear();
        if (pos > srt.size()) break;
        continue;
      }
      if (!text.empty()) text += "\\N";
      // 剥除 <i>/<b>/<u>/</...> 标签
      for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '<') {
          size_t close = line.find('>', i);
          if (close != std::string::npos) {
            i = close;
            continue;
          }
        }
        text += line[i];
      }
    }
    // 完整剧本一次性读入(ass_read_memory: 解析 Script/Styles/Events 全量)
    auto* read = ass_read_memory(static_cast<ASS_Library*>(assLibrary),
                                 const_cast<char*>(ass.c_str()),
                                 int32_t(ass.size()), nullptr);
    if (!read) return false;
    assTrack = read;
    return true;
  }
  // .ass 外挂
  auto* read = ass_read_file(static_cast<ASS_Library*>(assLibrary),
                             const_cast<char*>(path), nullptr);
  if (!read) return false;
  assTrack = read;
  return true;
}

const AssCanvas* AssOverlay::render(int64_t ptsMs) {
  if (!bInit || !assTrack) return nullptr;
  int detect = 0;
  auto* img = ass_render_frame(static_cast<ASS_Renderer*>(assRenderer),
                               static_cast<ASS_Track*>(assTrack), ptsMs, &detect);
  // 合成到联合 bbox; detect==0 且已有画布 → 内容未变, 返回当前 front
  if (detect == 0 && hasCanvas) {
    return &canvas[frontIdx];
  }
  int32_t x0 = storageW, y0 = storageH, x1 = 0, y1 = 0;
  for (auto* it = img; it; it = it->next) {
    if (it->w <= 0 || it->h <= 0) continue;
    if (it->dst_x < x0) x0 = it->dst_x;
    if (it->dst_y < y0) y0 = it->dst_y;
    if (it->dst_x + it->w > x1) x1 = it->dst_x + it->w;
    if (it->dst_y + it->h > y1) y1 = it->dst_y + it->h;
  }
  const int32_t back = frontIdx ^ 1;
  AssCanvas& out = canvas[back];
  if (x1 <= x0 || y1 <= y0) {
    // 无字幕(或全空): 空 canvas, seq 仍递增让消费方清层
    out = AssCanvas{};
    out.ptsMs = ptsMs;
    out.seq = ++seq;
    frontIdx = back;
    hasCanvas = true;
    return &out;
  }
  const int32_t w = x1 - x0, h = y1 - y0;
  std::string& buf = canvasBuf[back];
  buf.assign(size_t(w) * h * 4, 0);  for (auto* it = img; it; it = it->next) {
    if (it->w <= 0 || it->h <= 0) continue;
    // ASS_Image: bitmap 为 8bit 覆盖度; color 打包为 RGBA(R 高字节,
    // alpha 低字节且 0x00=不透明), 有效覆盖度 = bitmap × 颜色不透明度
    const uint32_t col = uint32_t(it->color);
    const uint8_t cr = uint8_t(col >> 24), cg = uint8_t(col >> 16),
                  cb = uint8_t(col >> 8), ca = uint8_t(col);
    const int32_t bx = it->dst_x - x0, by = it->dst_y - y0;
    for (int32_t y = 0; y < it->h; ++y) {
      const uint8_t* src = it->bitmap + size_t(y) * it->stride;
      uint8_t* dst = reinterpret_cast<uint8_t*>(buf.data()) +
                     size_t(by + y) * w * 4 + size_t(bx) * 4;
      for (int32_t x = 0; x < it->w; ++x) {
        const uint32_t cov = src[x];
        if (!cov) continue;
        const uint32_t a = cov * (255 - ca) / 255;
        if (!a) continue;
        uint8_t* px = dst + size_t(x) * 4;
        // src-over 合成: out = color*a + out*(1-a)
        px[0] = uint8_t((cr * a + px[0] * (255 - a)) / 255);
        px[1] = uint8_t((cg * a + px[1] * (255 - a)) / 255);
        px[2] = uint8_t((cb * a + px[2] * (255 - a)) / 255);
        px[3] = uint8_t(a + px[3] * (255 - a) / 255);
      }
    }
  }
  out = AssCanvas{};
  out.rgba = reinterpret_cast<const uint8_t*>(buf.data());
  out.width = w;
  out.height = h;
  out.stride = w * 4;
  out.x = x0;
  out.y = y0;
  out.ptsMs = ptsMs;
  out.seq = ++seq;
  frontIdx = back;
  hasCanvas = true;
  return &out;
}

void AssOverlay::flush() {
  if (assTrack) ass_flush_events(static_cast<ASS_Track*>(assTrack));
}

void AssOverlay::unload() {
  if (assTrack) {
    ass_free_track(static_cast<ASS_Track*>(assTrack));
    assTrack = nullptr;
  }
  hasCanvas = false;
}

#else  // 骨架模式: 未链接 libass

bool AssOverlay::init(int32_t storageWidth, int32_t storageHeight) {
  (void)storageWidth;
  (void)storageHeight;
  return false;
}

void AssOverlay::shutdown() { bInit = false; }

void AssOverlay::setFontsDir(const char* dir) {
  if (dir) fontsDir = dir;
}

void AssOverlay::setDefaultFont(const char* fontPath, const char* family) {
  if (fontPath) defaultFontPath = fontPath;
  if (family) defaultFontFamily = family;
}

bool AssOverlay::loadTrack(const char* extradata, int32_t size) {
  (void)extradata;
  (void)size;
  return false;
}

void AssOverlay::processChunk(const char* data, int32_t size, int64_t ptsMs,
                              int64_t durationMs) {
  (void)data;
  (void)size;
  (void)ptsMs;
  (void)durationMs;
}

bool AssOverlay::loadFile(const char* path) {
  (void)path;
  return false;
}

const AssCanvas* AssOverlay::render(int64_t ptsMs) {
  (void)ptsMs;
  return nullptr;
}

void AssOverlay::flush() {}

void AssOverlay::unload() {}

#endif

}
