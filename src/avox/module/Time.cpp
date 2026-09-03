#include "Time.hpp"
#include "../AvoxTime.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <mutex>  // _get_timezone
#endif

namespace avox {

// ========== 跨平台 localtime (线程安全) ==========

struct tm getLocalTime(std::time_t sec) {
  struct tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &sec);
#else
  localtime_r(&sec, &tm);
#endif
  return tm;
}

// ========== 时区偏移 (秒, 缓存一次) ==========

// localtime_s只能得到秒级精度,UTC与本地差值都是小时
// 故缓存一次UTC与本地的差值，避免每次都调用localtime_s
long getFixedOffset() {
  static long fixed_offset = []() {
    time_t utc_time = time(nullptr);
    struct tm local_tm{}, utc_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &utc_time);
    gmtime_s(&utc_tm, &utc_time);
#else
    localtime_r(&utc_time, &local_tm);
    gmtime_r(&utc_time, &utc_tm);
#endif
    time_t local_sec = mktime(&local_tm);
    time_t utc_sec = mktime(&utc_tm);
    return static_cast<long>(difftime(local_sec, utc_sec));
  }();
  return fixed_offset;
}

// ========== 时间格式化 ==========

// "20240115_143025" — 日志/截图文件名
std::string formatStamp_YMDHMS() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  return formatStamp_YMDHMS(t);
}

std::string formatStamp_YMDHMS(std::time_t t) {
  struct tm lt = getLocalTime(t);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
                lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                lt.tm_hour, lt.tm_min, lt.tm_sec);
  return buf;
}

// "2024-01-15T14:30:25+08:00" — ISO 8601
std::string formatIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  return formatIso8601(t);
}

std::string formatIso8601(std::time_t t) {
  struct tm lt = getLocalTime(t);
  int offsetMin = 0;
#ifdef _WIN32
  long winOffset = 0;
  _get_timezone(&winOffset);
  offsetMin = -static_cast<int>(winOffset / 60);
#else
  offsetMin = lt.tm_gmtoff / 60;
#endif
  char sign = (offsetMin >= 0) ? '+' : '-';
  int absH = std::abs(offsetMin) / 60;
  int absM = std::abs(offsetMin) % 60;
  char ibuf[48];
  std::snprintf(ibuf, sizeof(ibuf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                lt.tm_hour, lt.tm_min, lt.tm_sec, sign, absH, absM);
  return ibuf;
}

// "14:30:25.123" — 日志行时间 (HH:MM:SS.mmm)
std::string formatLogTime() {
  return formatLogTime(std::chrono::system_clock::now());
}

std::string formatLogTime(std::chrono::system_clock::time_point tp) {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()) %
            1000;
  auto t = std::chrono::system_clock::to_time_t(tp);
  struct tm lt = getLocalTime(t);
  char ts[32];
  std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d",
                lt.tm_hour, lt.tm_min, lt.tm_sec, static_cast<int>(ms.count()));
  return ts;
}

// ========== AvoxTime.h 导出函数实现 ==========

int64_t timeStampMS() {
  using namespace std::chrono;
  auto now = system_clock::now().time_since_epoch();
  auto timeStamp = duration_cast<milliseconds>(now).count();
  return timeStamp;
}

int64_t localTimeStampMS() {
  using namespace std::chrono;
  auto now = system_clock::now();
  int64_t utc_ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
  return utc_ms + getFixedOffset() * 1000;
}

int64_t localTimeMS(int64_t ms) { return ms + getFixedOffset() * 1000; }

int64_t localTimeTick(int64_t utcTick) {
  return utcTick + getFixedOffset() * 10000000LL;
}

int64_t timeTick() {
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  auto timeStamp =
      std::chrono::duration_cast<std::chrono::microseconds>(now).count() * 10;
  return timeStamp;
}

double timeTickMS() {
  Timespan startTime = {timeTick()};
  return startTime.getTotalMilliSeconds();
}

bool sleepToTick(int64_t timeTarget) {
  int64_t nowT = timeTick();
  if (nowT >= timeTarget) {
    return false;
  }
  int64_t millsecond = (timeTarget - nowT) / 10000;
  if (millsecond > 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millsecond - 1));
  }
  return true;
}

FrameRate getFrameRate(double fps) {
  FrameRate temp = {};
  temp.build(fps);
  return temp;
}

int64_t getFrameTick(double fps) {
  FrameRate frameRate = getFrameRate(fps);
  int64_t result = (int64_t)frameRate.denominator * 1000 * ticksPerMillisecond /
                   (int64_t)frameRate.numerator;
  return result;
}

int64_t getFrameRateTick(const FrameRate& frameRate, int32_t frameNumber) {
  FrameSize frameSize = {frameNumber, 0.0f};
  double seconds = frameRate.asSeconds(frameSize);
  int64_t tick = seconds * ticksPerSecond;
  return tick;
}

}
