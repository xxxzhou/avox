#pragma once

#include "AvoxCore.h"
#include <functional>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <sys/system_properties.h>
#include <EGL/egl.h>
#endif

// C++ 通用方法

namespace avox {

struct LogItem {
  LogLevel level = LogLevel::info;
  std::string msg;
  Timespan timestamp = {};
};

// 回调定义,在ao下的模块可以使用C++传值改用std::function.
// typedef C++ function后缀定义 handle,C为action,前缀不要加on
// 定义的handle变量为on前缀,event后缀
// 定义的handle包装方法为on前缀,handle后缀
// 定义的set handle变量的方法为set前缀(不包含on),handle后缀
typedef std::function<void(int32_t level, const char *message)> logHandle;

AVOX_EXPORT void log(LogLevel level, const std::string &message);

AVOX_EXPORT void log(const LogItem &item);

AVOX_EXPORT std::vector<std::string> stringSplit(const std::string &str,
                                                char delim);

AVOX_EXPORT std::string getAddressStr(const IP4Address &address);

AVOX_EXPORT std::string getEndpointStr(const IP4Endpoint &endpoint);

AVOX_EXPORT std::string extractFile(const char *path);

AVOX_EXPORT std::string getAvoxPath();

// 展开路径里的环境变量占位符: Windows 的 %APPDATA%/%LOCALAPPDATA%/... ,
// POSIX 的 ~/~user。无占位符原样返回。供 read/grep 等接受用户路径的
// 工具使用, 让 hysp_pc 默认日志路径 %APPDATA%/hysp_pc/logs/main.log 跨用户通用。
AVOX_EXPORT std::string expandEnvPath(const std::string& path);
// 检测是否是完整文件路径（如 D:/video.mp4）
AVOX_EXPORT bool checkDirectFile(const std::string& url);
// 从文件路径提取父目录
AVOX_EXPORT std::string parentDir(const std::string& path);

AVOX_EXPORT std::wstring utf8TWstring(const std::string &str);
AVOX_EXPORT std::string utf8TString(const std::wstring &str);
AVOX_EXPORT bool equalsIgnoreCase(const std::string &str1,
                                 const std::string &str2);

AVOX_EXPORT int32_t divUp(int32_t x, int32_t y);
AVOX_EXPORT int32_t alignSize(int32_t size, int32_t align);

AVOX_EXPORT void sleepThread(bool yield, int32_t ms);

AVOX_EXPORT int randInt(int start = 0, int end = 100);

// 帧率转FrameRate
AVOX_EXPORT FrameRate getFrameRate(double fps);

AVOX_EXPORT std::string getImageFilePath(const char* filename);
AVOX_EXPORT std::string getModelFilePath(const char* filename);

// 注册 AVOX_HOME 环境变量 + avox.pth, 让外部 Python 能 import avox。
// avox_cli / avox_agent 启动时调用一次。幂等: 路径未变则跳过。
// 1) 写注册表用户环境变量 AVOX_HOME=<install_root>
// 2) 找 Python site-packages, 写 avox.pth 指向 <install_root>/plugins
AVOX_EXPORT void ensurePythonPath();

// ============== Android 环境 (仅 __ANDROID__; 内部 C++ 用, 不导出) ==============
// 原在 AvoxCore.h (导出伞头), 移到此处使 AndroidEnv 不进公共导出头。
#ifdef __ANDROID__
struct AndroidEnv {
  JavaVM *vm = nullptr;
  // 在调用 initAndroid 的线程里有效, 注意不同线程这值不同
  JNIEnv *env = nullptr;
  jobject activity = nullptr;
  jclass activityClass = nullptr;
  jobject application = nullptr;
  int32_t sdkVersion = 0;
  AAssetManager *assetManager = nullptr;
  // 共用的 display
  EGLDisplay display = EGL_NO_DISPLAY;
};
#endif

}