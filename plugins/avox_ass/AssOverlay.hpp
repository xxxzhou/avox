#pragma once

#include <mutex>
#include <string>

#include "avox/subtitle/IAssOverlay.hpp"

namespace avox {

// IAssOverlay 实现(libass 封装)。
//
// 依赖策略: libass/fribidi/harfbuzz 在独立仓 avox-ass-deps 预编译(计划见
// doc/plan/player/ASS字幕渲染计划.md)。CMake 变量 AVOX_ASS_DEPS_DIR 指向产物
// 时链接 libass 并定义 AVOX_ASS_HAVE_LIBASS=1 走真实渲染;未指到则只编骨架,
// init() 返回 false → 调用方降级(等价无插件), 核心构建零污染。
//
// libass 句柄以 void* 持有(头文件不引 ass/*.h, 消费方零 libass 依赖)。
class AssOverlay : public IAssOverlay {
 public:
  AssOverlay() = default;
  ~AssOverlay() override = default;

 public:
  bool init(int32_t storageWidth, int32_t storageHeight) override;
  void shutdown() override;

  void setFontsDir(const char* dir) override;
  void setDefaultFont(const char* fontPath, const char* family) override;

  // 轨级样式覆盖(a01-T3): libass 排版层, PGS/文本路径不适用
  void setStyleScale(float scale) override;
  void setStyleFont(const char* family) override;

  bool loadTrack(const char* extradata, int32_t size) override;
  void processChunk(const char* data, int32_t size, int64_t ptsMs,
                    int64_t durationMs) override;
  bool loadFile(const char* path) override;

  const AssCanvas* render(int64_t ptsMs) override;

  void flush() override;
  void unload() override;

 private:
  // 字体配置(init 时生效, 保存为插件内 std::string, 不跨 DLL)
  std::string fontsDir;
  std::string defaultFontPath;
  std::string defaultFontFamily;
  int32_t storageW = 0;
  int32_t storageH = 0;
  bool bInit = false;
  // 轨级样式覆盖存档(a01-T3; setter 产品线程, load/render 播放线程, styleMtx 保护)
  // 注册到 libass 的 overrides 为 library 级(libass 拷贝存), loadTrack/loadFile
  // 对新轨 force-style 重应用; 清除对已应用轨不回滚(重载轨生效)
  void applyStyleOverride();
  mutable std::mutex styleMtx;
  double styleScale = 1.0;
  std::string styleFont;  // 空 = 不覆盖

  // libass 句柄(AVOX_ASS_HAVE_LIBASS 下使用; 骨架模式恒 nullptr)
  void* assLibrary = nullptr;   // ASS_Library*
  void* assRenderer = nullptr;  // ASS_Renderer*
  void* assTrack = nullptr;     // ASS_Track*

  // 外挂文件编码探测(loadFile 记录, unload 复位; 随 getFileEncoding 透出)
  SubtitleEncoding fileEncoding = SubtitleEncoding::unknown;

  // RGBA canvas 双缓冲(插件堆内, 消费方只读)
  std::string canvasBuf[2];     // 像素存储(素, 避免 vector 跨 DLL; 仅本 DLL 内用)
  AssCanvas canvas[2];          // 对外描述
  int32_t frontIdx = 0;
  int32_t seq = 0;
  bool hasCanvas = false;

 public:
  // IAssOverlay: 外挂文件编码探测结果(loadFile 记录, unload 复位)
  virtual SubtitleEncoding getFileEncoding() override { return fileEncoding; }
};

}
