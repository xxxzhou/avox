// stdout 镜像进日志文件: fd 级 tee (判定行+SDK日志都落盘, 供事后/大模型判定),
// 控制台显示不变。可选 onLine 回调逐行喂给调用方 (Android APK 横幅用)。
// 只覆盖启动后的输出, 进程启动早期 (DllMain/插件注册) 的几行不落盘
#pragma once

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace avox {
namespace playmatrix {

struct StdoutTee {
  FILE* file = nullptr;
  int oldFd = -1;
  int pipeFd[2] = {-1, -1};
  std::thread reader;
  std::function<void(const std::string&)> onLine;  // 每收到完整一行 (含'\n'前内容)
  std::string pending;

  bool start(const std::string& path) {
    file = fopen(path.c_str(), "wb");
    if (!file) {
      return false;
    }
#if defined(_WIN32)
    if (_pipe(pipeFd, 8192, _O_BINARY) != 0) {
      fclose(file);
      file = nullptr;
      return false;
    }
    oldFd = _dup(1);
    _dup2(pipeFd[1], 1);
    _setmode(1, _O_BINARY);
#else
    if (::pipe(pipeFd) != 0) {
      fclose(file);
      file = nullptr;
      return false;
    }
    oldFd = ::dup(1);
    ::dup2(pipeFd[1], 1);
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);
    reader = std::thread([this] { pump();
    });
    return true;
  }

  void pump() {
    char buf[4096];
    for (;;) {
#if defined(_WIN32)
      int n = _read(pipeFd[0], buf, sizeof(buf));
#else
      ssize_t n = ::read(pipeFd[0], buf, sizeof(buf));
#endif
      if (n <= 0) {
        break;
      }
      // 控制台补 \r (stdout 是二进制模式, \n 裸出会阶梯显示)
#if defined(_WIN32)
      std::string cr;
      cr.reserve((size_t)n * 2);
      for (int i = 0; i < n; ++i) {
        cr.push_back(buf[i]);
        if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) {
          cr.push_back('\r');
        }
      }
      _write(oldFd, cr.data(), (int)cr.size());
#else
      ::write(oldFd, buf, (size_t)n);
#endif
      fwrite(buf, 1, (size_t)n, file);
      fflush(file);
      feedLines(buf, (size_t)n);
    }
  }

  void feedLines(const char* data, size_t n) {
    if (!onLine) {
      return;
    }
    pending.append(data, n);
    size_t start = 0;
    size_t pos;
    while ((pos = pending.find('\n', start)) != std::string::npos) {
      onLine(pending.substr(start, pos - start));
      start = pos + 1;
    }
    pending.erase(0, start);
  }

  void stop() {
    if (!file) {
      return;
    }
    fflush(stdout);
#if defined(_WIN32)
    _dup2(oldFd, 1);  // fd1 还原控制台
    _close(pipeFd[1]);
    reader.join();
    _close(oldFd);
#else
    ::dup2(oldFd, 1);
    ::close(oldFd);
    ::close(pipeFd[1]);
    reader.join();
#endif
    if (onLine && !pending.empty()) {
      onLine(pending);
      pending.clear();
    }
    fclose(file);
    file = nullptr;
  }
};

}  // namespace playmatrix
}  // namespace avox
