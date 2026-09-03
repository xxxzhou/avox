
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <X11/Xlib.h>

int test_x11() {
    std::cout << "测试X11连接...\n";
    Display* display = XOpenDisplay(NULL);
    if (display) {
        std::cout << "✓ X11连接成功\n";
        std::cout << "X11服务器: " << DisplayString(display) << "\n";
        XCloseDisplay(display);
        return 0;
    } else {
        std::cout << "✗ X11连接失败\n";
        return 1;
    }
}

int main() {
  int result = test_x11();
  return result;
}