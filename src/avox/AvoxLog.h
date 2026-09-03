#pragma once

#include "AvoxDef.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace avox {

// __func__
#ifdef __ANDROID__
#define AVOX_MAP_LOG(XX)           \
  XX(info, 0, ANDROID_LOG_INFO)   \
  XX(warn, 1, ANDROID_LOG_WARN)   \
  XX(error, 2, ANDROID_LOG_ERROR) \
  XX(debug, 3, ANDROID_LOG_DEBUG)
#else
#define AVOX_MAP_LOG(XX) \
  XX(info, 0, "info")   \
  XX(warn, 1, "warn")   \
  XX(error, 2, "error") \
  XX(debug, 3, "debug")
#endif

enum class LogLevel : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_LOG(XX)
#undef XX
};

// 模块内请使用module/LogHelper.hpp提供的各种帮助输出，此文件主要用于模块外

class ILogOb {
 public:
  virtual ~ILogOb() = default;
  virtual void onLogEvent(int level, const char* message) {};
};

typedef void (*logAction)(int32_t level, const char* message);

extern "C" {

AVOX_EXPORT void setLogAction(logAction action);
// 回转
AVOX_EXPORT void setLogObserver(ILogOb* observer);
// 恢复 setLogObserver 之前的状态 (子命令结束时还原调用方的 observer, 避免置空)
AVOX_EXPORT void restoreLogObserver();
// 外部模块调用统一接口
AVOX_EXPORT void logMsg(LogLevel level, const char* message);
// 同步排空异步日志队列, 保证之前打的日志都已送达 observer
// agent chain runner 取回日志缓冲前调用, 避免尾部日志丢失
AVOX_EXPORT void flushLog();
}

}