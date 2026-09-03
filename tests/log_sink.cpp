// 测试专用的日志汇点: 被测源码 (H26XHelper/H264Parse 等) 经 LOGFLF 调用
// avox::log, 这里提供链接符号并把日志转发到 stderr, 便于失败时定位。
// 注意: 不用 doctest::MESSAGE —— 它是全局宏, 不能以 doctest:: 限定调用。
#include <iostream>

#include "Avox.hpp"

namespace avox {

void log(LogLevel level, const std::string& message) {
  if (message.empty()) {
    return;
  }
  std::cerr << "[avox-log:" << (int)level << "] " << message << std::endl;
}

void log(const LogItem& item) {
  log(item.level, item.msg);
}

void logMsg(LogLevel level, const char* message) {
  log(level, std::string(message ? message : ""));
}

}  // namespace avox
