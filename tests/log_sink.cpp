// 测试专用的日志汇点: 被测源码 (H26XHelper/H264Parse 等) 经 LOGFLF 调用
// avox::log, 这里提供链接符号并把日志转发到 stderr, 便于失败时定位。
// 注意: 不用 doctest::MESSAGE —— 它是全局宏, 不能以 doctest:: 限定调用。
// 弱符号: avox_tests 不链 avox, 靠这里给定义; avox_agent_tests 链了 avox,
// Avox.o 里有一份强定义 —— 若这里也是强定义, MSVC 只告警 (LNK4217) 而
// Apple/Linux 的 ld 直接报 duplicate symbol, 全量构建必挂, 故让位给强符号。
#include <iostream>

#include "Avox.hpp"

#if defined(__clang__) || defined(__GNUC__)
#define AVOX_TEST_LOG_WEAK __attribute__((weak))
#else
#define AVOX_TEST_LOG_WEAK
#endif

namespace avox {

void log(LogLevel level, const std::string& message) AVOX_TEST_LOG_WEAK;
void log(const LogItem& item) AVOX_TEST_LOG_WEAK;
void logMsg(LogLevel level, const char* message) AVOX_TEST_LOG_WEAK;

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
