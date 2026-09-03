#include "avox/AvoxInput.h"       // IScreenCapture / WindowShotInfo / toScreen
#include "avox/AvoxVideo.h"       // createImageBuffer / cropImage / saveImagePath
#include "avox/Input/ShotOps.hpp"  // listWindowDevices / activeWindow / shotWindow / shotScreen / showDesktop

#include <memory>
#include <string>
#include <vector>

namespace avox {

namespace {

// IScreenCapture 实现: 薄委托 ShotOps (不重写 win_capture 逻辑)。
// 与成员同名的自由函数用 ::avox:: 限定避免自递归。
class ScreenCapture : public IScreenCapture {
 public:
  int32_t getWindowCount() override {
    windowList = listWindowDevices(nullptr);
    return static_cast<int32_t>(windowList.size());
  }
  bool getWindowAt(int32_t index, WindowEntry* out) override {
    if (!out) return false;
    if (windowList.empty()) windowList = listWindowDevices(nullptr);
    if (index < 0 || index >= static_cast<int32_t>(windowList.size())) return false;
    const auto& e = windowList[index];
    titleBuf.assign(e.title);
    classBuf.assign(e.winClass);
    procBuf.assign(e.process);
    out->title = titleBuf.c_str();
    out->kind = e.kind;
    out->hwnd = nullptr;  // WindowDeviceEntry 不含 hwnd; 用 setWindow 后的 shotInfo().hwnd 或 findWindowByName
    out->winClass = classBuf.c_str();
    out->process = procBuf.c_str();
    return true;
  }
  bool activateWindow(const char* titleSub) override { return ::avox::activeWindow(titleSub); }
  bool activateHwnd(void* hwnd) override { return ::avox::activeWindowByHwnd(hwnd); }
  bool showDesktop() override { return ::avox::showDesktop(); }
  void undoDesktop() override { ::avox::undoDesktop(); }

  bool setWindow(const char* titleSub) override {
    auto buf = std::shared_ptr<IImageBuffer>(createImageBuffer());
    WindowShotInfo si{};
    if (!shotWindow(titleSub, buf.get(), &si)) { buffer.reset(); return false; }
    buffer = std::move(buf);
    info_ = si;
    isScreen = false;
    winName = titleSub ? titleSub : "";
    target = winName;
    return true;
  }
  bool setScreen(int32_t screenIndex, bool sd) override {
    auto buf = std::shared_ptr<IImageBuffer>(createImageBuffer());
    WindowShotInfo si{};
    if (!shotScreen(screenIndex, buf.get(), &si, sd, /*deferUndo=*/true)) { buffer.reset(); return false; }
    buffer = std::move(buf);
    info_ = si;
    isScreen = true;
    screenIdx = screenIndex;
    showDesk = sd;
    target = "screen:" + std::to_string(screenIndex);
    return true;
  }
  bool refresh() override {
    if (target.empty()) return false;
    return isScreen ? setScreen(screenIdx, showDesk) : setWindow(winName.c_str());
  }
  IImageBuffer* getBuffer() const override { return buffer.get(); }
  vec2i toScreen(int32_t bufX, int32_t bufY) const override {
    return ::avox::toScreen(info_, vec2i{bufX, bufY});
  }
  IScreenCapture* crop(int32_t x, int32_t y, int32_t w, int32_t h) const override {
    if (!buffer) return nullptr;
    auto buf = std::shared_ptr<IImageBuffer>(createImageBuffer());
    if (!buf) return nullptr;
    if (!cropImage(buffer.get(), buf.get(), x, y, w, h)) return nullptr;
    ScreenCapture* sub = new ScreenCapture();
    sub->buffer = std::move(buf);
    sub->target = target + " (crop)";
    return sub;
  }
  bool save(const char* path) const override {
    if (!buffer || !path) return false;
    return saveImagePath(path, buffer.get());
  }
  const char* targetName() const override { return target.c_str(); }
  int32_t width() const override { return info_.width; }
  int32_t height() const override { return info_.height; }

 private:
  std::shared_ptr<IImageBuffer> buffer;
  WindowShotInfo info_{};
  std::string target;
  bool isScreen = false;
  int32_t screenIdx = 0;
  bool showDesk = true;
  std::string winName;
  std::vector<WindowDeviceEntry> windowList;  // 枚举缓存
  std::string titleBuf, classBuf, procBuf;    // getWindowAt 返回指针的持有 (随下次失效)
};

}  // namespace

IScreenCapture* createScreenCapture() { return new ScreenCapture(); }

}
