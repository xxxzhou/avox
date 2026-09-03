#pragma once

#include <chrono>
#include <ctime>
#include <string>

#include "../AvoxDef.h"

namespace avox {

// ---- 跨平台 localtime (线程安全) ----
struct tm getLocalTime(std::time_t sec);

// ---- 时区偏移 (秒, 缓存一次) ----
long getFixedOffset();

// ---- 时间格式化 (内部 C++ 用, 返回 std::string) ----

// "20240115_143025" — 日志/截图文件名
std::string formatStamp_YMDHMS();
std::string formatStamp_YMDHMS(std::time_t t);

// "2024-01-15T14:30:25+08:00" — ISO 8601
std::string formatIso8601();
std::string formatIso8601(std::time_t t);

// "14:30:25.123" — 日志行时间 (HH:MM:SS.mmm)
std::string formatLogTime();
std::string formatLogTime(std::chrono::system_clock::time_point tp);

}
