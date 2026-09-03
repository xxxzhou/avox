#include "X11Surface.hpp"

namespace avox {

X11Surface::X11Surface() {}

X11Surface::~X11Surface() { cleanup(); }

bool X11Surface::create(int width_, int height_, const char* title) {
  width = width_;
  height = height_;
  windowTitle = title ? title : "avox";

  // 连接到 X11 显示服务器
  display = XOpenDisplay(nullptr);
  if (!display) {
    LOGFLF(LogLevel::warn, "failed to open X11 display");
    return false;
  }

  // 获取屏幕信息
  int screen = DefaultScreen(display);
  Window root = RootWindow(display, screen);

  // 创建窗口
  window = XCreateSimpleWindow(display, root, 0, 0, width, height, 0,
                               BlackPixel(display, screen),
                               BlackPixel(display, screen));
  if (!window) {
    LOGFLF(LogLevel::warn, "failed to create X11 window");
    cleanup();
    return false;
  }

  // 设置窗口标题
  XStoreName(display, window, windowTitle.c_str());

  // 注册窗口关闭事件
  wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wmDeleteMessage, 1);

  // 选择需要接收的事件
  XSelectInput(display, window,
               ExposureMask | KeyPressMask | KeyReleaseMask |
                   ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);

  // 显示窗口
  XMapWindow(display, window);
  XFlush(display);

  log(LogLevel::info, "X11 window created: ", width, "x", height);
  return true;
}

void X11Surface::pollEvents() {
  if (!display || !window) return;

  XEvent event;
  while (XPending(display) > 0) {
    XNextEvent(display, &event);

    switch (event.type) {
      case Expose:
        // 窗口需要重绘
        break;

      case ConfigureNotify:
        // 窗口大小改变
        if (event.xconfigure.width != width ||
            event.xconfigure.height != height) {
          width = event.xconfigure.width;
          height = event.xconfigure.height;
          log(LogLevel::info, "X11 window resized: ", width, "x", height);
        }
        break;

      case ClientMessage:
        // 检查窗口关闭消息
        if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
          closeRequested = true;
        }
        break;

      case DestroyNotify:
        closeRequested = true;
        break;

      default:
        break;
    }
  }
}

void X11Surface::cleanup() {
  if (window && display) {
    XDestroyWindow(display, window);
    window = 0;
  }
  if (display) {
    XCloseDisplay(display);
    display = nullptr;
  }
}

}
