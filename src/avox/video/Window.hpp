#pragma once

#include <string>

#include "../AvoxLayer.h"
#include "../module/Observer.hpp"
#include "../module/RunTask.hpp"
#include "../module/TickCounter.hpp"

// linux x11
#ifdef __ONLY_LINUX__
#include "avox_linux/LinuxHelper.h"
#define AvoxSurfaceType ILinuxSurface*
#endif

// 前置声明平台原生类型
#ifdef WIN32
struct HWND__;
typedef struct HWND__* HWND;
#define AvoxSurfaceType HWND
//  win32实例 HINSTANCE
#define AvoxInstanceType void*
#endif
#ifdef __ANDROID__
struct ANativeWindow;
#define AvoxSurfaceType ANativeWindow*
struct android_app;
#define AvoxInstanceType android_app*
#endif
#ifdef __APPLE__
#ifdef __OBJC__
@class CAMetalLayer;
#else
typedef void CAMetalLayer;
#endif
#define AvoxSurfaceType CAMetalLayer*
//  ios实例 NSApplication
#define AvoxInstanceType void*
#endif

namespace avox {

// 检查窗口是否有效，当前窗口大小
class IWindowOb {
 public:
  virtual ~IWindowOb() {}

 public:
  virtual void onRenderWindow() {};
};

class Window : public Observer<IWindowOb> {
 public:
  Window();
  virtual ~Window();

 protected:
  int32_t wdWidth = 1280;
  int32_t wdHeight = 720;
  std::string wdTitle = "avox";
  // AvoxSurfaceType wdHandle = nullptr;
  AvoxSurfaceType surface = nullptr;
#ifdef WIN32
  // windows用hwnd创建surface
  AvoxSurfaceType hwnd = nullptr;
#endif
  RenderType renderType = RenderType::other;

  // 长宽比保持,如果是0,全屏填满
  bool bFullScreen = false;
  float aspect = 0.0f;

  // 窗口状态变化日志
  TickLog vaildWdLog = {};

 public:
  virtual void initSurface(void* surface);
  virtual IRenderContext* getRenderContext();
  // vk/dx11显示context
  virtual void renderContext(IRenderContext* context) {};

 public:
  const char* getTitle() const { return wdTitle.c_str(); }
  // 如果返回true,则表示窗口大小变化了
  bool updateSize();
  AvoxSurfaceType getSurface() const { return surface; }
  AvoxSurfaceType getInitSurface() const {
#ifdef WIN32
    return hwnd;
#else
    return surface;
#endif
  }
  RenderType getRenderType();
  void setFullScreen(bool bFull) { bFullScreen = bFull; };  

 public:
  int32_t getWidth() { return wdWidth; }
  int32_t getHeight() { return wdHeight; }

  bool preTick();
  void tick();
  void close();

 protected:
  // 初始化子类实现
  virtual void onInitWin() {};
  virtual bool onValidWin() { return true; };
  virtual void onChangeSize() {};
  // 用来检查窗口宽高，比如当前宽高不合法，如何处理
  virtual bool onPreTick();
  virtual void onTickWin() {};
  virtual void onCloseWin() {};
};

AvoxSurfaceType getNativeSurface(void* surface);

void getSurfaceSize(AvoxSurfaceType surface, int32_t& width, int32_t& height);

}
