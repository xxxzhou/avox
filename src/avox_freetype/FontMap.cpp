#include "FontMap.hpp"

#include "avox/module/AvoxManager.hpp"
#ifdef __APPLE__
#include "avox_ios/IOSHelper.h"
#endif

namespace avox {

FontMap::FontMap() { FT_Init_FreeType(&ftLibrary); }

FontMap::~FontMap() {
  if (ftFace) {
    FT_Done_Face(ftFace);
    ftFace = nullptr;
  }
  if (ftLibrary) {
    FT_Done_FreeType(ftLibrary);
    ftLibrary = nullptr;
  }
}

bool FontMap::loadFont(const char* fontName_, int32_t fontSize_) {
  if (!ftLibrary) {
    LOGFLF(LogLevel::warn, "ftLibrary is null");
    return false;
  }
  if (ftFace && strcmp(fontName.c_str(), fontName_) == 0) {
    if (fontSize == fontSize_) {
      return true;
    }
  }
  loadFontFace(fontName_, ftLibrary, ftFace);
  LOGFLF(LogLevel::info, "load font:", fontName_, " size:", fontSize_);
  if (!ftFace) {
    LOGFLF(LogLevel::warn, "load font failed");
    return false;
  }
  fontName = fontName_;
  fontSize = fontSize_;
  FT_Set_Pixel_Sizes(ftFace, 0, fontSize);
  FT_Select_Charmap(ftFace, FT_ENCODING_UNICODE);
  glyphCache.clear();
  return true;
}

bool FontMap::cacheGlyph(wchar_t character) {
  if (FT_Load_Char(
          ftFace, character,
          FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP)) {
    return false;
  }
  FT_Bitmap& bitmap = ftFace->glyph->bitmap;
  CachedGlyph glyph = {};
  glyph.width = bitmap.width;
  glyph.height = bitmap.rows;
  glyph.baseline = ftFace->glyph->metrics.horiBearingY >> 6;
  // 空格特殊处理
  if (character == ' ') {
    if (glyph.width == 0) glyph.width = fontSize / 2;
    if (glyph.height == 0) glyph.height = fontSize;
    glyph.bitmap.resize(glyph.width * glyph.height, 0);
  } else {
    // 拷贝 FT_Bitmap 数据（FT_Load_Char 后 bitmap 是临时的，下次调用会失效）
    glyph.bitmap.resize(glyph.width * glyph.height);
    for (int y = 0; y < bitmap.rows; ++y) {
      for (int x = 0; x < bitmap.width; ++x) {
        glyph.bitmap[y * glyph.width + x] = bitmap.buffer[y * bitmap.pitch + x];
      }
    }
  }
  glyphCache[character] = std::move(glyph);
  return true;
}

void FontMap::loadText(const std::string& text,
                       std::vector<CachedGlyph>& glyphs) {
  std::wstring wtext = utf8TWstring(text);
  glyphs.clear();
  glyphs.resize(wtext.size());
  for (size_t i = 0; i < wtext.size(); ++i) {
    wchar_t c = wtext[i];
    auto it = glyphCache.find(c);
    if (it != glyphCache.end()) {
      glyphs[i] = it->second;
    } else {
      // 缓存未命中：实时渲染并缓存
      if (cacheGlyph(c)) {
        glyphs[i] = glyphCache[c];
      } else {
        glyphs[i] = {};  // 渲染失败，显示为空
      }
    }
  }
}

// fontName对应asset/fonts/下面的字体文件名
bool loadFontFace(const char* fontName, const FT_Library& ftLib,
                  FT_Face& ftFace) {
  if (!ftLib) {
    LOGFLF(LogLevel::warn, "FreeType library is not initialized");
    return false;
  }
  std::string fontPath;
#ifdef __ANDROID__
  // Android平台：从assets加载字体
  AAssetManager* assetManager = AvoxManager::Get().getAppEnv().assetManager;
  if (assetManager) {
    std::string assetPath = "fonts/";
    assetPath += fontName;
    AAsset* asset =
        AAssetManager_open(assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (asset) {
      const void* fontData = AAsset_getBuffer(asset);
      off_t fontSize = AAsset_getLength(asset);
      if (fontData && fontSize > 0) {
        // 从内存加载字体
        FT_Error error = FT_New_Memory_Face(
            ftLib, reinterpret_cast<const FT_Byte*>(fontData),
            static_cast<FT_Long>(fontSize), 0, &ftFace);
        AAsset_close(asset);
        if (error == 0 && ftFace) {
          LOGFLF(LogLevel::info, "loaded font from Android assets:", fontName);
          return true;
        } else {
          LOGFLF(LogLevel::warn,
                 "failed to load font from Android assets:", fontName);
        }
      }
      AAsset_close(asset);
    }
  }
#elif __APPLE__
// iOS/macOS平台
#ifdef TARGET_OS_IPHONE
  fontPath = getFontPath(fontName);
#else
  // macOS: 系统字体目录
  fontPath = "/System/Library/Fonts/";
  fontPath += fontName;
#endif

#elif _WIN32
  // Windows平台：系统字体目录
  fontPath = getAvoxPath() + "/assets/fonts/" + fontName;
#endif
  // 从文件系统加载字体
  FT_Error error = FT_New_Face(ftLib, fontPath.c_str(), 0, &ftFace);
  if (error == 0 && ftFace) {
    LOGFLF(LogLevel::info, "Loaded font from file system:", fontPath);
    return true;
  }
  // 如果文件系统加载失败，尝试使用默认字体
  LOGFLF(LogLevel::warn, "Failed to load font:", fontName,
         "from path:", fontPath);
#ifdef _WIN32
  // Windows默认字体
  const char* defaultFonts[] = {
      "C:\\Windows\\Fonts\\simhei.ttf",  // 黑体
      "C:\\Windows\\Fonts\\simsun.ttc",  // 宋体
      "C:\\Windows\\Fonts\\msyh.ttc",    // 微软雅黑
      "C:\\Windows\\Fonts\\arial.ttf"    // Arial
  };
#elif __APPLE__
  const char* defaultFonts[] = {
      "/System/Library/Fonts/PingFang.ttc",   // 苹方
      "/System/Library/Fonts/Helvetica.ttc",  // Helvetica
      "/System/Library/Fonts/Arial.ttf"       // Arial
  };
#elif __ANDROID__
  const char* defaultFonts[] = {"/system/fonts/DMSans-Regular.ttf"};
#else
  const char* defaultFonts[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",  // DejaVu Sans
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"  // Noto Sans CJK
  };
#endif
  // 尝试加载默认字体
  for (const char* defaultFont : defaultFonts) {
    error = FT_New_Face(ftLib, defaultFont, 0, &ftFace);
    if (error == 0 && ftFace) {
      LOGFLF(LogLevel::info, "Loaded default font:", defaultFont);
      return true;
    }
  }
  LOGFLF(LogLevel::warn, "Failed to load any font");
  return false;
};

void freetype_test() {
  FontMap fontMap;
  fontMap.loadFont("simhei.ttf", 16);
  std::string text = "Hello, 世界, 你好";
  std::vector<CachedGlyph> glyphs;
  fontMap.loadText(text, glyphs);
  for (int i = 0; i < glyphs.size(); ++i) {
    auto& glyph = glyphs[i];
    LOGFLF(LogLevel::info, "glyph:", i, " w:", glyph.width, " h:", glyph.height,
           " baseline:", glyph.baseline);
  }
}

}
