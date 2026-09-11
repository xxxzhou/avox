
#include <time.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "AvoxAudio.h"
#include "AvoxCodec.h"
#include "AvoxVersion.h"
#include "AvoxVideo.h"
#include "audio/AudioRender.hpp"
#include "module/Json.hpp"
#include "module/LogHelper.hpp"
#include "module/TaskTrack.hpp"
#include "muxer/MediaMuxer.hpp"
#include "player/MediaPlayer.hpp"

#ifdef WIN32
#include <psapi.h>
#include <windows.h>

#include <ctime>
#if _MSC_VER > 1910 && _HAS_CXX17
#include <filesystem>
namespace fs = std::filesystem;
#else
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING 1
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
#elif defined(__ANDROID__)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
// 引入包含 MAXPATHLEN 定义的头文件
#include <sys/param.h>
// 引入 Foundation 框架头文件
#include "avox_apple/IOSHelper.h"
#elif defined(__ONLY_LINUX__)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "AvoxPlayer.h"
#include "module/AvoxManager.hpp"
#include "module/LogHelper.hpp"
#include "module/RunTask.hpp"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/layer/VkInputLayer.hpp"
#include "avox_vulkan/layer/VkOutputLayer.hpp"
#include "avox_vulkan/vulkan/VkVideoRender.hpp"
#include "video/SurfaceRenderVk.hpp"
#endif

#ifdef AVOX_NODEJS
#endif

namespace avox {

// 限制在本文件内使用
static LogTask logTask = {};
static bool bCLog = false;
// 原子指针: 日志从任意线程进(logMsg→log), setLogObserver/restoreLogObserver
// 运行期重挂 observer 时, 裸指针的并发读会拿到撕裂/悬空值在 vcall 处崩
// (崩溃点稳定在 log(LogItem) 内, 新老版本均复现)。读侧一律快照后使用。
static std::atomic<ILogOb*> gLogOb{nullptr};
static ILogOb* gPrevLogOb =
    nullptr;  // setLogObserver 时保存前值, 供 restoreLogObserver 恢复
// 默认debug下同步输出，release下异步输出
#if AVOX_DEBUG
static bool bAsyncLog = false;
#else
static bool bAsyncLog = true;
#endif

class CLogOb : public ILogOb {
 public:
  CLogOb(logHandle logHandle) { onLogHandle = logHandle; }
  virtual ~CLogOb() {}

 private:
  logHandle onLogHandle = nullptr;

 public:
  virtual void onLogEvent(int level, const char* message) override {
    if (onLogHandle) {
      onLogHandle(level, message);
    }
  }
};

void setLogAction(logAction action) {
  CLogOb* temp = new CLogOb(action);
  setLogObserver(temp);
  bCLog = true;
}

void setLogObserver(ILogOb* observer) {
  // 如果上个gLogOb是项目内部申请的CLogOb,则释放
  ILogOb* cur = gLogOb.load();
  if (bCLog && cur) {
    delete cur;
  }
  gPrevLogOb = cur;
  gLogOb.store(observer);
  bCLog = false;
  log(LogLevel::info, "avox version:", AVOX_COMMIT_VERSION,
      " commit_hash:", AVOX_COMMIT_HASH, " commit_time:", AVOX_COMMIT_TIME,
      " build_time:", AVOX_BUILD_TIME);
}

void restoreLogObserver() {
  // 恢复 setLogObserver 之前的状态 (通常由 cmdPlay/cmdRecord
  // 等子命令结束时调用, 还原调用方(如 AgentShell/ChainRunner)设的 SilentLogOb,
  // 避免置空导致日志泄漏到控制台)
  ILogOb* cur = gLogOb.load();
  if (bCLog && cur) {
    delete cur;
  }
  gLogOb.store(gPrevLogOb);
  gPrevLogOb = nullptr;
  bCLog = false;
}

void flushLog() {
  // Release 下日志走异步队列(logTask), 调用者(如 chain runner)取回日志前
  // 先同步排空队列, 确保之前的日志都已送达 observer
  logTask.drain(2000);
}

// 进程退出前显式停异步日志线程: logTask 是文件级静态对象, CRT
// 静态析构顺序不确定, 若 logTask 析构(join 线程)时日志线程仍在 log()
// 访问已析构的全局(gLogOb/cout/TrackMgr), 会触发访问冲突。由
// DllMain(DLL_PROCESS_DETACH) 在静态析构前调用本函数, 先排空 + join。
void AvoxManager::clean() {
  // 先摘掉日志观察者: 宿主(如 godot 插件)设置的观察者对象可能在宿主 DLL
  // 卸载时 vtable 已失效(unmap), 本函数及静态析构阶段的任何 log() 都会在
  // onLogEvent 虚调用上访问违规(每次进程退出必崩, 见 tools 下 AVOX_*.dmp)。
  // bCLog 为 true 时观察者归 avox 内部管理, 需释放; 外部观察者只摘不删。
  ILogOb* cur = gLogOb.load();
  if (bCLog && cur) {
    delete cur;
  }
  gLogOb.store(nullptr);
  bCLog = false;
  // DllMain DETACH 时机, 静态析构尚未开始, ZL 等单例仍存活:
  // 逆序跑各模块清理(与 initFuncs 对称), 让模块在单例析构前主动收尾
  for (auto it = Get().cleanFuncs.rbegin(); it != Get().cleanFuncs.rend(); ++it) {
    if (!it->func) {
      continue;
    }
    log(LogLevel::info, it->desc);
    it->func();
  }
  if (logTask.running()) {
    logTask.drain(1000);
    logTask.stopTask();
  }
}

void log(LogLevel level, const std::string& message) {
  if (message.empty()) {
    return;
  }
  logMsg(level, message.c_str());
}

void log(const LogItem& item) {
  // 快照: 拿到指针后本次调用恒用同一实例, 避免与 setLogObserver 并发时撕裂
  ILogOb* ob = gLogOb.load(std::memory_order_acquire);
  if (ob) {
    ob->onLogEvent((int32_t)item.level, item.msg.c_str());
  } else {
#if !defined(__ANDROID__)
    std::ostringstream oss;
    string_format(oss, item.timestamp);
#endif
    switch (item.level) {
#ifdef __ANDROID__
#define XX(name, value, str)                                 \
  case LogLevel::name:                                       \
    __android_log_print(str, "avox", "%s", item.msg.c_str()); \
    break;
      AVOX_MAP_LOG(XX)
#undef XX
#elif defined(__APPLE__)
#define XX(name, value, strv)                            \
  case LogLevel::name:                                   \
    logApple(oss.str().c_str(), strv, item.msg.c_str()); \
    break;
      // 直接包含Foundation.h会因为NSString是C,不能混编到.cpp中报错
      AVOX_MAP_LOG(XX)
#undef XX
#else
#define XX(name, value, strv)                                         \
  case LogLevel::name:                                                \
    std::cout << "[" << oss.str() << "] " << strv << ": " << item.msg \
              << std::endl;                                           \
    break;
      AVOX_MAP_LOG(XX)
#undef XX
#endif
    }
  }
}

void logMsg(LogLevel level, const char* message) {
  LogItem item = {};
  item.level = level;
  item.msg = message;
  // 多实例时带所属 TaskTrack 可读前缀（如 [MP0]）；单实例/无归属则无前缀
  std::string tag = TrackMgr::get().currentTag();
  if (!tag.empty()) {
    item.msg = "[" + tag + "] " + item.msg;
  }
  item.timestamp = {localTimeStampMS() * 10000};
  if (bAsyncLog && logTask.running()) {
    logTask.addItem(item);
  } else {
    log(item);
  }
}

bool bConfigType(PackType packType) {
  return packType == PackType::vconfig || packType == PackType::aconfig;
}

const char* getPackTypeStr(PackType packType) {
  switch (packType) {
#define XX(name, value, str) \
  case PackType::name:       \
    return str;
    AVOX_MAP_PACK_TYPE(XX)
#undef XX
  }
  return "unknown";
}

std::vector<std::string> stringSplit(const std::string& str, char delim) {
  std::vector<std::string> elems;
  auto lastPos = str.find_first_not_of(delim, 0);
  auto pos = str.find_first_of(delim, lastPos);
  while (pos != std::string::npos || lastPos != std::string::npos) {
    elems.push_back(str.substr(lastPos, pos - lastPos));
    lastPos = str.find_first_not_of(delim, pos);
    pos = str.find_first_of(delim, lastPos);
  }
  return elems;
}

std::string getAddressStr(const IP4Address& address) {
  std::string msg = {};
  string_format(msg, address);
  return msg;
}

std::string getEndpointStr(const IP4Endpoint& endpoint) {
  std::string msg = {};
  string_format(msg, endpoint);
  return msg;
}

std::string extractFile(const char* path) {
  std::string fileStr(path);
  size_t pos = fileStr.find_last_of("/\\");
  if (pos != std::string::npos) {
    fileStr = fileStr.substr(pos + 1);
  }
  return fileStr;
}

bool checkLocalPath(const char* xurl) {
  // 防御：公共 API,外部绑定可能传 null 或空串
  if (xurl == nullptr || xurl[0] == '\0') return false;
  std::string url = xurl;
  // 去首尾空白（URL 偶尔带前后空格）
  size_t head = url.find_first_not_of(" \t\r\n");
  if (head == std::string::npos) return false;  // 全是空白
  size_t tail = url.find_last_not_of(" \t\r\n");
  url = url.substr(head, tail - head + 1);
  // Windows 盘符路径必须最先判：D:/x C:\x，含 D://x 这种写法
  // 提前于 scheme 判定，避免 d:// 被当成合法 scheme 误判为网络
  if (url.length() > 2 && url[1] == ':' &&
      (url[2] == '/' || url[2] == '\\')) {
    return true;
  }
  // file:// 本质是本地文件（大小写不敏感,兼容 FILE://）
  if (url.size() >= 7) {
    const char* kFile = "file://";
    bool isFile = true;
    for (size_t i = 0; i < 7 && isFile; ++i) {
      isFile = std::tolower(static_cast<unsigned char>(url[i])) ==
               std::tolower(static_cast<unsigned char>(kFile[i]));
    }
    if (isFile) return true;
  }
  // 严格 scheme 判定：:// 前必须是合法协议名（字母开头,字母/数字/+/-/.）
  // 天然大小写不敏感,且避开本地路径中途偶现的 ://（如 /home/u/a://b）
  size_t schemeEnd = url.find("://");
  if (schemeEnd != std::string::npos && schemeEnd > 0) {
    bool valid = std::isalpha(static_cast<unsigned char>(url[0]));
    for (size_t i = 1; i < schemeEnd && valid; ++i) {
      unsigned char c = static_cast<unsigned char>(url[i]);
      if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') valid = false;
    }
    if (valid) return false;
  }
  // // 或 \\ 开头为网络绝对路径（无协议式或 UNC 共享,如 \\server\share）
  if (url.length() >= 2 &&
      ((url[0] == '/' && url[1] == '/') ||
       (url[0] == '\\' && url[1] == '\\'))) {
    return false;
  }
  // Unix 绝对路径或相对路径都是本地路径
  return true;
}

bool checkDirectFile(const std::string& url) {
  if (url.empty()) return false;
  // 必须是本地路径才有意义判断
  if (!checkLocalPath(url.c_str())) return false;
  auto dotPos = url.rfind('.');
  auto slashPos = url.rfind('/');
  auto bslashPos = url.rfind('\\');
  size_t lastSep = 0;
  if (slashPos != std::string::npos) lastSep = slashPos;
  if (bslashPos != std::string::npos && bslashPos > lastSep)
    lastSep = bslashPos;
  // 点号在最后一个分隔符之后，且不是隐藏文件（如 .gitignore）
  if (lastSep > 0 && dotPos != std::string::npos) {
    return dotPos > lastSep && (dotPos - lastSep > 1);
  }
  // 无分隔符的纯文件名（如 test.mp4）
  return dotPos > 0;
}

std::string parentDir(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path;
  }
  // 根目录如 D:/，保留到盘符
  if (pos == 2 && path.length() > 1 && path[1] == ':') {
    return path.substr(0, 3);
  }
  return path.substr(0, pos);
}

std::string getAvoxPath() {
#ifdef WIN32
  HMODULE ihdll = GetModuleHandleA("avox.dll");
  char buffer[MAX_PATH] = {0};

  if (ihdll != nullptr) {
    // 从 avox.dll 获取路径
    if (GetModuleFileNameA(ihdll, buffer, MAX_PATH) == 0) {
      // 失败则使用当前目录
      return ".";
    }
  } else {
    // avox.dll 未加载，使用可执行文件目录
    if (GetModuleFileNameA(nullptr, buffer, MAX_PATH) == 0) {
      return ".";
    }
  }

  fs::path modulePath = buffer;
  modulePath = modulePath.parent_path();
  std::string pathStr = modulePath.string();
  // 移除Windows扩展路径前缀
  const std::string prefix = "\\\\?\\";
  if (pathStr.size() >= prefix.size() &&
      pathStr.substr(0, prefix.size()) == prefix) {
    pathStr = pathStr.substr(prefix.size());
  }
  return pathStr;
#elif defined(__ANDROID__)
  // console 进程靠 /proc/self/exe 定位资源; APK 内指向 app_process, 取不到 assets, 与原返回 "" 等效
  char buffer[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
  if (count == -1) {
    return "";
  }
  buffer[count] = '\0';
  std::string path(buffer);
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return "";
  }
  return path.substr(0, pos);
#elif defined(__APPLE__)
  char buffer[MAXPATHLEN];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    std::string path(buffer);
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
      path = path.substr(0, pos);
    }
    return path;
  }
  return "";
#elif defined(__ONLY_LINUX__)
  char buffer[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
  if (count == -1) {
    perror("readlink");
    return "";
  }
  buffer[count] = '\0';
  std::string path(buffer);
  auto pos = path.find_last_of('/');
  if (pos != std::string::npos) {
    path = path.substr(0, pos);
  }
  return path;
#else
  return "";
#endif
}

// avox 运行目录 (avox.dll/exe 装载目录), extern "C" 导出 const char*。
// 路径恒定, 首次 getAvoxPath() 结果经 static 缓存 (C++11 起线程安全), 之后直返指针。
// C++ 侧直接用上面的 std::string getAvoxPath() 即可, 本函数供 C/SWIG 调用。
const char* getAvoxRunDir() {
  static const std::string dir = getAvoxPath();
  return dir.c_str();
}

std::string expandEnvPath(const std::string& path) {
  if (path.empty()) return path;
#ifdef _WIN32
  // %VAR% 占位符展开 (如 %APPDATA%, %LOCALAPPDATA%)
  std::string out;
  out.reserve(path.size());
  for (size_t i = 0; i < path.size();) {
    if (path[i] == '%') {
      size_t end = path.find('%', i + 1);
      if (end != std::string::npos && end > i + 1) {
        std::string name = path.substr(i + 1, end - i - 1);
        const char* val = std::getenv(name.c_str());
        if (val) {
          out += val;
          i = end + 1;
          continue;
        }
      }
    }
    out += path[i++];
  }
  return out;
#else
  // ~/~user 展开
  if (path[0] == '~') {
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + path.substr(1);
  }
  return path;
#endif
}

// ansi用char表示多字节编码(中国ansi对应GB2312),unicode(UTF-16)用wchar表示编码(多国家同一编码)
// uft8在unicode基础上,变长1-4Byte表示,用于传输与保存节约空间
// https://stackoverflow.com/questions/6693010/how-do-i-use-multibytetowidechar
// https://stackoverflow.com/questions/4804298/how-to-convert-wstring-into-string
std::wstring utf8TWstring(const std::string& str) {
  if (str.empty()) {
    return std::wstring();
  }
#ifdef WIN32
  size_t len = str.length() + 1;
  std::wstring ret = std::wstring(len, 0);
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &str[0],
                                 str.size(), &ret[0], len);
  ret.resize(size);
#else
  size_t len = str.length();
  std::vector<wchar_t> dest(len, 0);
  int dest_len = 0;
  for (int i = 0; i < len; i++, dest_len++) {
    // ansi
    if (str[i] <= 127) {
      dest[dest_len] = str[i];
    } else if ((str[i] & 0xF0) == 0xC0) {
      // 2byte
      dest[dest_len] = ((str[i] & 0x1F) << 6) + (str[i + 1] & 0x3F);
      i += 1;
    } else if ((str[i] & 0xF0) == 0xE0) {
      // 3byte
      dest[dest_len] = ((str[i] & 0x0F) << 12) + ((str[i + 1] & 0x3F) << 6) +
                       (str[i + 2] & 0x3F);
      i += 2;
    } else {
      // ignore 4byte
      log(LogLevel::warn, "Can't change utf8 4byte characters");
      return L"";
    }
  }
  std::wstring ret;
  ret.assign(dest.data(), dest_len);
#endif
  return ret;
}

std::string utf8TString(const std::wstring& wstr) {
  if (wstr.empty()) {
    return std::string();
  }
#ifdef WIN32
  int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0],
                                 wstr.size(), NULL, 0, NULL, NULL);
  std::string ret = std::string(size, 0);
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], wstr.size(),
                      &ret[0], size, NULL, NULL);
#else
  int source_len = wstr.length();
  std::vector<char> dest(source_len * 3 + 1, 0);
  const wchar_t* source = wstr.c_str();
  int dest_len = 0;
  for (int i = 0; i < source_len; i++) {
    if (wstr[i] <= 0x7F) {
      dest[dest_len] = wstr[i];
      dest_len++;
    } else if (wstr[i] >= 0x80 && wstr[i] <= 0x7FF) {
      wchar_t tmp = wstr[i];
      char first = 0, second = 0, third = 0;
      for (int j = 0; j < 3; j++) {
        wchar_t tmp_quota = tmp % 16;
        switch (j) {
          case 0:
            third = tmp_quota;
            break;
          case 1:
            second = tmp_quota;
            break;
          case 2:
            first = tmp_quota;
            break;
        }
        tmp /= 16;
      }

      dest[dest_len] = 0xC0 + (first << 2) + (second >> 2);
      dest[dest_len + 1] = 0x80 + (((second % 8) % 4) << 4) + third;
      dest_len += 2;
    } else if (wstr[i] >= 0x800 && wstr[i] <= 0xFFFF) {
      wchar_t tmp = wstr[i];
      char first = 0, second = 0, third = 0, fourth = 0;
      for (int j = 0; j < 4; j++) {
        wchar_t tmp_quota = tmp % 16;
        switch (j) {
          case 0:
            fourth = tmp_quota;
            break;
          case 1:
            third = tmp_quota;
            break;
          case 2:
            second = tmp_quota;
            break;
          case 3:
            first = tmp_quota;
            break;
        }
        tmp /= 16;
      }
      dest[dest_len] = 0xE0 + first;
      dest[dest_len + 1] = 0x80 + second << 2 + third >> 2;
      dest[dest_len + 2] = 0x80 + (((third % 8) % 4) << 4) + fourth;
      dest_len += 3;
    } else {
    }
  }
  dest[dest_len++] = '\0';
  std::string ret;
  ret.assign(dest.data(), dest_len);
#endif
  return ret;
}

bool equalsIgnoreCase(const std::string& str1, const std::string& str2) {
  if (str1.size() != str2.size()) {
    return false;
  }
  for (size_t i = 0; i < str1.size(); i++) {
    if (tolower(str1[i]) != tolower(str2[i])) {
      return false;
    }
  }
  return true;
}

bool parseIP4Address(const char* str, IP4Address& ip4Address) {
  std::string temp = str;
  std::vector<std::string> ip4array = stringSplit(str, '.');
  if (ip4array.size() != 4) {
    log(LogLevel::warn, "parseIP4Address:", str, " no vaild");
    return false;
  }
  uint8_t* p1 = &ip4Address.arr1;
  for (int32_t i = 0; i < 4; i++) {
    *(p1 + i) = std::stoi(ip4array[i]);
  }
  return true;
}

bool parseIP4Endpoint(const char* str, IP4Endpoint& pr4Endpoint) {
  std::string temp = str;
  std::vector<std::string> ip4array = stringSplit(str, ':');
  if (ip4array.size() != 2) {
    log(LogLevel::warn, "parseIP4Endpoint:", str, " no vaild");
    return false;
  }
  if (!parseIP4Address(ip4array[0].c_str(), pr4Endpoint.address)) {
    return false;
  }
  pr4Endpoint.port = std::stoi(ip4array[1]);
  return true;
}

const char* getACodecName(ACodecId codecId) {
  switch (codecId) {
#define XX(name, value, str) \
  case ACodecId::name:       \
    return str;
    AVOX_MAP_ACODEC(XX)
#undef XX
    default:
      return "invalid";
  }
}

const char* getVCodecName(VCodecId codecId) {
  switch (codecId) {
#define XX(name, value, str) \
  case VCodecId::name:       \
    return str;
    AVOX_MAP_VCODEC(XX)
#undef XX
    default:
      return "invalid";
  }
}

int32_t divUp(int32_t x, int32_t y) { return (x + y - 1) / y; }

int randInt(int start, int end) {
  static std::mt19937 engine(std::random_device{}());
  std::uniform_int_distribution<int> dist(start, end);
  return dist(engine);
}

int32_t alignSize(int32_t size, int32_t align) {
  return divUp(size, align) * align;
}

IOption* createJsonOption() { return new Json(ArgType::Object); }

void setElectronSurface(ISurfaceRender* surfaceRender, uint32_t handle) {
  void* surface = reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
  surfaceRender->setSurface(surface);
}

void* getRenderSharedHandle(ISurfaceRender* surfaceRender) {
  // ISurfaceRender确定是WindowRender,所以为static_cast
  // 否则应该用dynamic_cast,static_cast可能返回一个错误的指针
  WindowRender* render = static_cast<WindowRender*>(surfaceRender);
  if (!render) {
    return nullptr;
  }
  return render->getVkVideoRender()->getOutGpuBuffer();
}

#ifdef AVOX_ENABLE_VULKAN

// ── 输出: AVOX 写,外部读 ──

bool enableVkOutput(ISurfaceRender* sr, int32_t w, int32_t h) {
  if (!sr || w <= 0 || h <= 0) {
    return false;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    LOGFLF(LogLevel::warn, "enableVkOutput: failed to get VkVideoRender");
    return false;
  }
  VkOutputLayer* outputLayer = vkRender->getOutputLayer();
  if (!outputLayer) {
    LOGFLF(LogLevel::warn, "enableVkOutput: outputLayer is null");
    return false;
  }
  // 幂等: 已建立直接返回。图重建(功能开关等)后新层未激活, 会重新走建立流程
  if (outputLayer->isInteropActive()) {
    return true;
  }
  // 用传入的 w,h 创建可导出的 VkSharedImage
  VkSharedImageDesc desc = {};
  desc.width = w;
  desc.height = h;
  desc.format = VK_FORMAT_R8G8B8A8_UNORM;
  // SAMPLED: 共享内存被 D3D11 OpenSharedResource1 导入时需要映射 SHADER_RESOURCE 绑定位
  desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT;
#ifdef _WIN32
  desc.handleType = VkShareHandleType::opaqueWin32;
#elif defined(__ANDROID__)
  desc.handleType = VkShareHandleType::androidHwBuffer;
#endif
  if (!outputLayer->getSharedImage()->createExportable(desc)) {
    LOGFLF(LogLevel::warn, "enableVkOutput: createExportable failed");
    return false;
  }
  outputLayer->setVkInterop(true);
  LOGFLF(LogLevel::info, "enableVkOutput: w:", w, " h:", h);
  return true;
}

bool getVkOutputHandle(ISurfaceRender* sr, VkSharedHandle* out) {
  if (!sr || !out) {
    return false;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    return false;
  }
  VkOutputLayer* outputLayer = vkRender->getOutputLayer();
  if (!outputLayer || !outputLayer->getSharedImage() || !outputLayer->getSharedImage()->isValid()) {
    return false;
  }
  VkShareHandle memHandle = outputLayer->getSharedImage()->exportHandle();
  if (memHandle.type == VkShareHandleType::none) {
    LOGFLF(LogLevel::warn, "getVkOutputHandle: exportHandle failed");
    return false;
  }
#ifdef _WIN32
  out->memHandle = (uint64_t)(uintptr_t)memHandle.handle;
#elif defined(__ANDROID__)
  if (memHandle.type != VkShareHandleType::androidHwBuffer || !memHandle.handle) {
    LOGFLF(LogLevel::warn, "getVkOutputHandle: not androidHwBuffer");
    return false;
  }
  out->ahb = memHandle.handle;
#endif
  // 导出的 handle 所有权转移给调用者,清空 VkShareHandle 防止 RAII 释放
  memHandle.handle = nullptr;
  memHandle.type = VkShareHandleType::none;
  return true;
}

void disableVkOutput(ISurfaceRender* sr) {
  if (!sr) {
    return;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    return;
  }
  VkOutputLayer* outputLayer = vkRender->getOutputLayer();
  if (outputLayer) {
    // 只停 copy,不释放共享内存(对面可能还在用)
    // 共享内存随 ISurfaceRender 销毁时在 onUnInit 释放
    outputLayer->setVkInterop(false);
  }
  LOGFLF(LogLevel::info, "disableVkOutput: done");
}

// ── D3D11 输出: AVOX 自建 NT 共享纹理,VK 每帧拷入,外部 DX11 设备打开复制(仅 Windows) ──
#ifdef _WIN32

static VkOutputLayer* getVkOutputLayerDx11(ISurfaceRender* sr) {
  if (!sr) {
    return nullptr;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  return vkRender ? vkRender->getOutputLayer() : nullptr;
}

bool enableVkOutputDx11(ISurfaceRender* sr) {
  VkOutputLayer* outputLayer = getVkOutputLayerDx11(sr);
  if (!outputLayer) {
    LOGFLF(LogLevel::warn, "enableVkOutputDx11: outputLayer is null");
    return false;
  }
  outputLayer->setDx11Output(true);
  LOGFLF(LogLevel::info, "enableVkOutputDx11: ok");
  return true;
}

uint64_t getVkOutputDx11Handle(ISurfaceRender* sr) {
  VkOutputLayer* outputLayer = getVkOutputLayerDx11(sr);
  if (!outputLayer || !outputLayer->getWinImage() ||
      !outputLayer->getWinImage()->getInit()) {
    return 0;
  }
  return (uint64_t)(uintptr_t)outputLayer->getWinImage()->getHandle();
}

uint64_t getVkOutputDx11FenceHandle(ISurfaceRender* sr) {
  VkOutputLayer* outputLayer = getVkOutputLayerDx11(sr);
  if (!outputLayer || !outputLayer->getWinImage() ||
      !outputLayer->getWinImage()->getInit()) {
    return 0;
  }
  return (uint64_t)(uintptr_t)outputLayer->getWinImage()->getFenceHandle();
}

void disableVkOutputDx11(ISurfaceRender* sr) {
  VkOutputLayer* outputLayer = getVkOutputLayerDx11(sr);
  if (!outputLayer) {
    return;
  }
  outputLayer->setDx11Output(false);
  LOGFLF(LogLevel::info, "disableVkOutputDx11: done");
}
#else
// 非 Windows 无 D3D11 互操作, 保留导出符号
bool enableVkOutputDx11(ISurfaceRender* sr) {
  (void)sr;
  return false;
}
uint64_t getVkOutputDx11Handle(ISurfaceRender* sr) {
  (void)sr;
  return 0;
}
uint64_t getVkOutputDx11FenceHandle(ISurfaceRender* sr) {
  (void)sr;
  return 0;
}
void disableVkOutputDx11(ISurfaceRender* sr) { (void)sr; }
#endif

// ── 输入: 外部写,AVOX 读 ──

bool enableVkInput(ISurfaceRender* sr, int32_t w, int32_t h) {
  if (!sr || w <= 0 || h <= 0) {
    return false;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    LOGFLF(LogLevel::warn, "enableVkInput: failed to get VkVideoRender");
    return false;
  }
  VkInputLayer* inputLayer = vkRender->getInputLayer();
  if (!inputLayer) {
    LOGFLF(LogLevel::warn, "enableVkInput: inputLayer is null");
    return false;
  }
  // 记录 desc 供 setVkInputHandle 使用,先不导入
  // bVkInterop 在 setVkInputHandle 成功后才置 true
  LOGFLF(LogLevel::info, "enableVkInput: w:", w, " h:", h);
  return true;
}

bool setVkInputHandle(ISurfaceRender* sr, const VkSharedHandle* handle) {
  if (!sr || !handle || handle->memHandle == 0) {
    return false;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    LOGFLF(LogLevel::warn, "setVkInputHandle: failed to get VkVideoRender");
    return false;
  }
  VkInputLayer* inputLayer = vkRender->getInputLayer();
  if (!inputLayer) {
    LOGFLF(LogLevel::warn, "setVkInputHandle: inputLayer is null");
    return false;
  }
  // 构造 VkShareHandle 从 uint64_t
  VkShareHandle memHandle = {};
  memHandle.type = VkShareHandleType::opaqueWin32;
  memHandle.handle = (void*)(uintptr_t)handle->memHandle;
  // 用 outputLayer 的 outFormat 作为 desc,或用 inputLayer 的 inFormats
  VkSharedImageDesc desc = {};
  // 从 outputLayer 获取实际尺寸(可能因 aspect 调整)
  VkOutputLayer* outputLayer = vkRender->getOutputLayer();
  if (outputLayer) {
    const ImageFormat& fmt = outputLayer->getOutFormat();
    desc.width = fmt.width;
    desc.height = fmt.height;
    desc.format = (fmt.imageType == ImageType::bgra8) ? VK_FORMAT_B8G8R8A8_UNORM
                                                       : VK_FORMAT_R8G8B8A8_UNORM;
  } else {
    LOGFLF(LogLevel::warn, "setVkInputHandle: outputLayer is null, cannot get format");
    return false;
  }
  desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  desc.handleType = VkShareHandleType::opaqueWin32;
  if (!inputLayer->getSharedImage()->importFromHandle(memHandle, desc)) {
    LOGFLF(LogLevel::warn, "setVkInputHandle: importFromHandle failed");
    return false;
  }
  // 导入成功后,清空 VkShareHandle 防止 RAII 释放(句柄所有权已转移给 VkDevice)
  memHandle.handle = nullptr;
  memHandle.type = VkShareHandleType::none;
  inputLayer->setVkInterop(true);
  LOGFLF(LogLevel::info, "setVkInputHandle: success, w:", desc.width, " h:", desc.height);
  return true;
}

void disableVkInput(ISurfaceRender* sr) {
  if (!sr) {
    return;
  }
  VkVideoRender* vkRender = getVkVideoRender(sr);
  if (!vkRender) {
    return;
  }
  VkInputLayer* inputLayer = vkRender->getInputLayer();
  if (inputLayer) {
    inputLayer->setVkInterop(false);
    inputLayer->requestRelease();
  }
  LOGFLF(LogLevel::info, "disableVkInput: done");
}

#else  // !AVOX_ENABLE_VULKAN

bool enableVkOutput(ISurfaceRender* sr, int32_t w, int32_t h) {
  LOGFLF(LogLevel::warn, "enableVkOutput: Vulkan not enabled");
  return false;
}
bool getVkOutputHandle(ISurfaceRender* sr, VkSharedHandle* out) { return false; }
void disableVkOutput(ISurfaceRender* sr) {}
bool enableVkOutputDx11(ISurfaceRender* sr) {
  LOGFLF(LogLevel::warn, "enableVkOutputDx11: Vulkan not enabled");
  return false;
}
uint64_t getVkOutputDx11Handle(ISurfaceRender* sr) { return 0; }
uint64_t getVkOutputDx11FenceHandle(ISurfaceRender* sr) { return 0; }
void disableVkOutputDx11(ISurfaceRender* sr) {}
bool enableVkInput(ISurfaceRender* sr, int32_t w, int32_t h) {
  LOGFLF(LogLevel::warn, "enableVkInput: Vulkan not enabled");
  return false;
}
bool setVkInputHandle(ISurfaceRender* sr, const VkSharedHandle* handle) { return false; }
void disableVkInput(ISurfaceRender* sr) {}

#endif  // AVOX_ENABLE_VULKAN

void addSurfaceRenderOb(ISurfaceRender* surfaceRender, ISurfaceRenderOb* ob) {
  WindowRender* render = static_cast<WindowRender*>(surfaceRender);
  if (!render) {
    return;
  }
  render->addObserver(ob);
  LOGFLF(LogLevel::info, "surfaceRender:", surfaceRender, " ob:", ob);
}

void removeSurfaceRenderOb(ISurfaceRender* surfaceRender,
                           ISurfaceRenderOb* ob) {
  WindowRender* render = static_cast<WindowRender*>(surfaceRender);
  if (!render) {
    return;
  }
  render->removeObserver(ob);
}

void addAudioTapOb(IAudioRender* r, IAudioTapOb* ob) {
  AudioRender* render = static_cast<AudioRender*>(r);
  if (!render) {
    return;
  }
  render->addTapOb(ob);
}

void removeAudioTapOb(IAudioRender* r, IAudioTapOb* ob) {
  AudioRender* render = static_cast<AudioRender*>(r);
  if (!render) {
    return;
  }
  render->removeTapOb(ob);
}

const char* getVRenderTypeStr(RenderType type) {
  switch (type) {
#define XX(name, value, str) \
  case RenderType::name:     \
    return str;
    AVOX_MAP_RENDER_TYPE(XX)
#undef XX
    default:
      return "unknow";
  }
}

uint64_t getCurrentMemoryUsageKB() {
  uint64_t mem_kb = 0;

#ifdef _WIN32
  // Windows: 使用 WorkingSetSize
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    mem_kb = pmc.WorkingSetSize / 1024;
  }

#elif __ANDROID__
  // Android: 读取 /proc/self/status 中的 VmRSS
  // 注意：VmRSS 是进程实际占用的物理内存
  std::ifstream statusFile("/proc/self/status");
  std::string line;
  while (std::getline(statusFile, line)) {
    if (line.find("VmRSS:") == 0) {
      // 提取行中的数字，VmRSS 格式通常为: "VmRSS:     12345 kB"
      for (char c : line) {
        if (isdigit(c)) {
          mem_kb = mem_kb * 10 + (c - '0');
        }
      }
      break;
    }
  }

#elif __APPLE__
  // iOS/macOS: 使用 task_info 接口
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info,
                &count) == KERN_SUCCESS) {
    mem_kb = info.resident_size / 1024;
  }
#endif

  return mem_kb;
}

// 获取图像文件路径，处理不同平台的资源路径
std::string getImageFilePath(const char* filename) {
  std::string imagePath;
#ifdef __APPLE__
  imagePath = getImagePath(filename);
#elif defined(__ANDROID__)
  imagePath = "/assets/images/";
  imagePath += filename;
#elif defined(_WIN32) || defined(__ONLY_LINUX__)
  imagePath = getAvoxPath() + "/assets/images/" + filename;
#else
  imagePath = filename;
#endif
  return imagePath;
}

std::string getModelFilePath(const char* filename) {
  // 构建模型路径
  std::string modelPath;
#ifdef __APPLE__
  modelPath = getModelPath(filename);
#elif defined(__ANDROID__)
  modelPath = std::string("/assets/models/") + filename;
#elif defined(_WIN32) || defined(__ONLY_LINUX__)
  modelPath = getAvoxPath() + "/assets/models/" + filename;
#endif
  return modelPath;
}

// ── ensurePythonPath: 注册 AVOX_HOME + avox.pth ──

#ifdef _WIN32
// 读注册表用户环境变量
static std::string readUserEnvVar(const char* name) {
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &key) != ERROR_SUCCESS)
    return "";
  char buf[2048] = {};
  DWORD size = sizeof(buf);
  DWORD type = 0;
  LSTATUS st = RegQueryValueExA(key, name, nullptr, &type, (LPBYTE)buf, &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_SZ) return "";
  return buf;
}

// 写注册表用户环境变量
static bool writeUserEnvVar(const char* name, const char* value) {
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    return false;
  LSTATUS st = RegSetValueExA(key, name, 0, REG_SZ, (const BYTE*)value,
                              (DWORD)(strlen(value) + 1));
  RegCloseKey(key);
  if (st != ERROR_SUCCESS) return false;
  // 广播 WM_SETTINGCHANGE 让其他进程 (新开的 cmd/python) 感知环境变量变化
  SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment",
                      SMTO_ABORTIFHUNG, 2000, nullptr);
  return true;
}

// 找所有 Python 的 site-packages 目录 (注册表 + SearchPath, 多版本全覆盖)
#include <map>
static std::map<std::string, std::string> findAllSitePackages() {
  std::map<std::string, std::string> result;
  auto tryPython = [&](const char* exePath) {
    // 查 user site-packages (优先, 有写权限)
    std::string cmd = std::string("\"") + exePath
        + "\" -c \"import site; print(site.getusersitepackages())\" 2>nul";
    FILE* fp = _popen(cmd.c_str(), "r");
    if (fp) {
      char sitePath[512] = {};
      if (fgets(sitePath, sizeof(sitePath), fp)) {
        _pclose(fp);
        size_t len = strlen(sitePath);
        while (len > 0 && (sitePath[len - 1] == '\n' || sitePath[len - 1] == '\r'))
          sitePath[--len] = '\0';
        if (len > 0 && GetFileAttributesA(sitePath) != INVALID_FILE_ATTRIBUTES)
          result[sitePath] = exePath;
      } else {
        _pclose(fp);
      }
    }
    // 系统 site-packages (回退)
    std::string installDir = exePath;
    auto sep = installDir.find_last_of("\\/");
    if (sep != std::string::npos) installDir = installDir.substr(0, sep);
    std::string sysSite = installDir + "\\Lib\\site-packages";
    if (GetFileAttributesA(sysSite.c_str()) != INVALID_FILE_ATTRIBUTES)
      result[sysSite] = exePath;
  };
  // 1) 从注册表遍历所有 PythonCore 版本 (HKCU + HKLM)
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    HKEY key;
    if (RegOpenKeyExA(root, "Software\\Python\\PythonCore", 0, KEY_READ, &key) != ERROR_SUCCESS)
      continue;
    DWORD index = 0;
    char ver[64];
    while (RegEnumKeyA(key, index++, ver, sizeof(ver)) == ERROR_SUCCESS) {
      std::string subkey = std::string("Software\\Python\\PythonCore\\") + ver + "\\InstallPath";
      HKEY ipKey;
      if (RegOpenKeyExA(root, subkey.c_str(), 0, KEY_READ, &ipKey) != ERROR_SUCCESS) continue;
      char exePath[512] = {};
      DWORD size = sizeof(exePath);
      DWORD type = 0;
      // 优先读 ExecutablePath
      if (RegQueryValueExA(ipKey, "ExecutablePath", nullptr, &type, (LPBYTE)exePath, &size)
          != ERROR_SUCCESS || type != REG_SZ || strlen(exePath) == 0) {
        // 回退读默认值 (InstallPath 目录)
        char installPath[512] = {};
        size = sizeof(installPath);
        if (RegQueryValueExA(ipKey, "", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS
            && type == REG_SZ) {
          snprintf(exePath, sizeof(exePath), "%s\\python.exe", installPath);
        }
      }
      if (strlen(exePath) > 0 && GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES)
        tryPython(exePath);
      RegCloseKey(ipKey);
    }
    RegCloseKey(key);
  }
  // 2) SearchPath 找 python.exe (用系统环境变量 PATH, 不受进程 PATH 限制)
  char found[512];
  if (SearchPathA(nullptr, "python.exe", nullptr, sizeof(found), found, nullptr) > 0)
    tryPython(found);
  // python3.exe 也试
  if (SearchPathA(nullptr, "python3.exe", nullptr, sizeof(found), found, nullptr) > 0)
    tryPython(found);
  return result;
}
#endif  // _WIN32

void ensurePythonPath() {
  std::string root = getAvoxPath();
  if (root.empty()) return;
#ifdef _WIN32
  // 1) 写 AVOX_HOME 注册表用户环境变量 (路径未变则跳过)
  std::string curHome = readUserEnvVar("AVOX_HOME");
  // 统一比较 (忽略尾部分隔符)
  std::string rootNorm = root;
  while (!rootNorm.empty() && (rootNorm.back() == '/' || rootNorm.back() == '\\'))
    rootNorm.pop_back();
  std::string curNorm = curHome;
  while (!curNorm.empty() && (curNorm.back() == '/' || curNorm.back() == '\\'))
    curNorm.pop_back();
  if (_stricmp(rootNorm.c_str(), curNorm.c_str()) != 0) {
    writeUserEnvVar("AVOX_HOME", rootNorm.c_str());
    LOGFLF(LogLevel::info, "AVOX_HOME = %s", rootNorm.c_str());
  }
  // 2) 写 avox.pth 到所有发现的 Python site-packages (路径未变则跳过)
  std::string pluginsDir = rootNorm + "\\plugins";
  auto allSites = findAllSitePackages();
  for (auto& [siteDir, pyExe] : allSites) {
    std::string pthPath = siteDir + "\\avox.pth";
    // 读现有 pth 内容, 未变则跳过
    std::ifstream pf(pthPath);
    std::string existing;
    if (pf.is_open()) {
      std::getline(pf, existing);
      pf.close();
    }
    if (existing != pluginsDir) {
      // 确保 site-packages 目录存在 (user site 可能首次使用, 尚未创建)
      CreateDirectoryA(siteDir.c_str(), nullptr);
      std::ofstream of(pthPath);
      if (of.is_open()) {
        of << pluginsDir << "\n";
        of.close();
        LOGFLF(LogLevel::info, "avox.pth = %s (python: %s)", pthPath.c_str(), pyExe.c_str());
      } else {
        LOGFLF(LogLevel::warn, "avox.pth write failed: %s", pthPath.c_str());
      }
    }
  }
  if (allSites.empty()) {
    LOGFLF(LogLevel::warn, "ensurePythonPath: no Python found, avox.pth not written");
  }
#endif  // _WIN32
}

}
