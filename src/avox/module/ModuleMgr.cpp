#include "ModuleMgr.hpp"

#include "AvoxManager.hpp"
#include "../AvoxBase.h"
#include "LogHelper.hpp"

#ifdef WIN32
#include <windows.h>
// SEH 异常码 → 可读字符串
static std::string sehCodeToString(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
  case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
  case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
  case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
  default: return "0x" + std::to_string(code);
  }
}
#endif

#ifdef WIN32
#include <Shlwapi.h>
#include <Windows.h>
#pragma comment(lib, "shlwapi.lib")
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

namespace avox {

namespace {
// 标记函数: 用于 dladdr 定位 avox.so 路径(Linux/Android)
void avoxModuleMarker() {}
}  // namespace

ModuleMgr* ModuleMgr::instance = nullptr;
ModuleMgr& ModuleMgr::Get() {
  if (instance == nullptr) {
    instance = new ModuleMgr();
  }
  return *instance;
}

ModuleMgr::ModuleMgr(/* args */) {}

ModuleMgr::~ModuleMgr() {
  // 先 unload 所有(delete module + FreeLibrary/dlclose)
  std::vector<std::string> names;
  for (auto& kv : modules) {
    names.push_back(kv.first);
  }
  for (const auto& n : names) {
    unloadModule(n.c_str());
  }
  // 再 delete ModuleInfo
  for (auto iter = modules.begin(); iter != modules.end(); iter++) {
    delete iter->second;
  }
  modules.clear();
  // 清除配置
  for (auto iter = moduleOptions.begin(); iter != moduleOptions.end(); iter++) {
    delete iter->second;
    iter->second = nullptr;
  }
  moduleOptions.clear();
}

std::string ModuleMgr::getAvoxDllDir() {
#ifdef WIN32
  char sz[512] = {0};
  HMODULE ihdll = GetModuleHandleA("avox.dll");
  ::GetModuleFileNameA(ihdll, sz, 512);
  ::PathRemoveFileSpecA(sz);
  return std::string(sz);
#else
  Dl_info info;
  if (dladdr((void*)(avoxModuleMarker), &info) && info.dli_fname) {
    std::string path(info.dli_fname);
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
      return path.substr(0, pos);
    }
    return path;
  }
  return ".";
#endif
}

std::string ModuleMgr::getAvoxPluginsDir() {
  // 外部指定优先(Android/Godot: 插件 so 由宿主解压到可写目录后 setPluginsDir 指定)
  if (!customPluginsDir.empty()) {
    return customPluginsDir;
  }
  std::string dir = getAvoxDllDir();
#ifdef WIN32
  return dir + "\\plugins";
#else
  return dir + "/plugins";
#endif
}

void ModuleMgr::setPluginsDir(const char* dir) {
  // 仅 startup 前有效: startup 后插件已扫描, 改目录不影响结果
  if (dir == nullptr) {
    customPluginsDir.clear();
  } else {
    customPluginsDir = dir;
  }
}

void ModuleMgr::setOption(const char* name, IOption* options) {
  if (moduleOptions.find(name) != moduleOptions.end()) {
    if (moduleOptions[name]) {
      delete moduleOptions[name];
      moduleOptions[name] = nullptr;
    }
  }
  moduleOptions[name] = options;
}

void ModuleMgr::registerModule(const char* name, loadModuleHandle handle) {
  if (modules.find(name) != modules.end()) {
    return;
  }
  ModuleInfo* moduleInfo = new ModuleInfo();
  modules[name] = moduleInfo;
  moduleInfo->name = name;
  // 文件名按平台推导(逻辑名与文件名分离, 不再混用)
#ifdef WIN32
  moduleInfo->fileName = std::string(name) + ".dll";
#else
  moduleInfo->fileName = "lib" + std::string(name) + ".so";
#endif
  moduleInfo->onLoadEvent = handle;
}

bool ModuleMgr::loadModule(const char* name) {
  ensureStarted();
  auto it = modules.find(name);
  if (it == modules.end()) {
    return false;
  }
  ModuleInfo* moduleInfo = it->second;
  if (moduleInfo->state == ModuleInfo::Loaded) {
    return true;
  }
  if (moduleInfo->state == ModuleInfo::Failed) {
    return false;
  }
  if (moduleInfo->state == ModuleInfo::Loading) {
    log(LogLevel::warn, moduleInfo->name + ": 循环依赖,跳过");
    return false;
  }
  moduleInfo->state = ModuleInfo::Loading;
  // 创建实例
  if (!moduleInfo->module) {
    if (moduleInfo->onLoadEvent) {
      // 静态注册工厂
      moduleInfo->module = moduleInfo->onLoadEvent();
    } else {
      // 动态: 完整路径 LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH
      // 需完整路径才生效, 能从 plugin 同目录找到 opencv_world4xx.dll 等依赖)
#ifdef WIN32
      std::string dllPath = getAvoxPluginsDir() + "\\" + moduleInfo->fileName;
#else
      std::string dllPath = getAvoxPluginsDir() + "/" + moduleInfo->fileName;
#endif
      loadModuleAction loadAction = nullptr;
#ifdef WIN32
      // 把 plugins/ 目录加入进程 DLL 搜索路径, 使插件的依赖 DLL (如 onnxruntime.dll /
      // opencv_world4xx.dll) 也能从 plugins/ 找到。必须在 LoadLibraryExA 之前调用。
      static bool s_pluginsDirAdded = false;
      if (!s_pluginsDirAdded) {
        std::string pluginsDir = getAvoxPluginsDir();
        // Convert UTF-8 path to wide string for AddDllDirectory
        int wlen = MultiByteToWideChar(CP_UTF8, 0, pluginsDir.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
          std::wstring wPath(wlen, L'\0');
          MultiByteToWideChar(CP_UTF8, 0, pluginsDir.c_str(), -1, &wPath[0], wlen);
          AddDllDirectory(wPath.c_str());
        }
        s_pluginsDirAdded = true;
      }
      // SEH 包裹 LoadLibrary: 插件 DllMain/静态初始化可能崩溃,
      // catch 后打印错误继续, 不让整个进程 crash
      // 独立 lambda 避免 SEH __try 与 C++ 析构函数冲突 (MSVC C2712)
      auto safeLoadLibrary = [](const char* path) -> HMODULE {
        __try {
          return LoadLibraryExA(path, nullptr,
              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
              | LOAD_LIBRARY_SEARCH_USER_DIRS);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          return nullptr;
        }
      };
      moduleInfo->handle = safeLoadLibrary(dllPath.c_str());
      if (!moduleInfo->handle) {
        // SEH 捕获的崩溃 handle 也为 nullptr, 无法区分, 用 GetLastError 辅助判断
        DWORD err = GetLastError();
        log(LogLevel::error, moduleInfo->name +
                                 ": dll load failed (may crash in DllMain). path=" + dllPath +
                                 " err=" + std::to_string(err));
        moduleInfo->state = ModuleInfo::Failed;
        return false;
      }
      loadAction = (loadModuleAction)GetProcAddress(
          (HMODULE)moduleInfo->handle, "NewModule");
#else
      moduleInfo->handle = dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (moduleInfo->handle) {
        loadAction = (loadModuleAction)dlsym(moduleInfo->handle, "NewModule");
      }
#endif
      if (!moduleInfo->handle) {
#ifdef WIN32
        log(LogLevel::warn, moduleInfo->name +
                                ": dll load failed. path=" + dllPath +
                                " err=" + std::to_string(GetLastError()));
#else
        log(LogLevel::warn,
            moduleInfo->name + ": dll load failed. path=" + dllPath);
#endif
        moduleInfo->state = ModuleInfo::Failed;
        return false;
      }
      if (!loadAction) {
        log(LogLevel::warn,
            moduleInfo->name + " loaded, but no NewModule symbol.");
        moduleInfo->state = ModuleInfo::Failed;
        return false;
      }
      moduleInfo->module = loadAction();
    }
    if (!moduleInfo->module) {
      log(LogLevel::warn, moduleInfo->name + ": init module failed.");
      moduleInfo->state = ModuleInfo::Failed;
      return false;
    }
  }
  // ABI 校验(动态)
  if (moduleInfo->handle) {
    typedef int (*GetABIAction)();
    GetABIAction getABI = nullptr;
#ifdef WIN32
    getABI = (GetABIAction)GetProcAddress((HMODULE)moduleInfo->handle,
                                          "GetModuleABI");
#else
    getABI = (GetABIAction)dlsym(moduleInfo->handle, "GetModuleABI");
#endif
    if (getABI && getABI() != AVOX_PLUGIN_ABI_VERSION) {
      log(LogLevel::warn, moduleInfo->name + ": ABI 不匹配,跳过");
      moduleInfo->state = ModuleInfo::Failed;
      return false;
    }
  }
  // 递归依赖(任一失败则本模块直接 unavailable, 不探测自身)
  for (int i = 0; i < moduleInfo->module->depCount(); ++i) {
    const char* dep = moduleInfo->module->getDep(i);
    if (!loadModule(dep)) {
      log(LogLevel::warn,
          moduleInfo->name + ": 因依赖 " + dep + " 失败而不可用");
      moduleInfo->state = ModuleInfo::Failed;
      return false;
    }
  }
  // 探测 + 初始化
  IOption* option = nullptr;
  if (moduleOptions.find(name) != moduleOptions.end()) {
    option = moduleOptions[name];
  }
  if (moduleInfo->module->loadModule(option)) {
    moduleInfo->state = ModuleInfo::Loaded;
    log(LogLevel::info, moduleInfo->name + ": regedit module success.");
    return true;
  }
  moduleInfo->state = ModuleInfo::Failed;
  log(LogLevel::warn, moduleInfo->name + ": regedit module failed.");
  return false;
}

void ModuleMgr::regAndLoad(const char* name) {
  registerModule(name);
  loadModule(name);
}

void ModuleMgr::unloadModule(const char* name) {
  auto it = modules.find(name);
  if (it == modules.end()) {
    return;
  }
  ModuleInfo* moduleInfo = it->second;
  if (moduleInfo->state != ModuleInfo::Loaded) {
    return;
  }
  if (moduleInfo->module) {
    moduleInfo->module->unloadModule();
    // 实测 avox /MT 配置下跨 dll new/delete 安全(UCRT 静态堆复用 process heap),
    // 无需 destroy() 自销毁; 静态模块本就同堆。直接 delete。
    delete moduleInfo->module;
    moduleInfo->module = nullptr;
  }
  if (moduleInfo->handle) {
#ifdef WIN32
    FreeLibrary((HMODULE)moduleInfo->handle);
#else
    dlclose(moduleInfo->handle);
#endif
    moduleInfo->handle = nullptr;
  }
  moduleInfo->state = ModuleInfo::NotLoaded;
}

bool ModuleMgr::checkLoadModel(const char* name) {
  ensureStarted();
  auto it = modules.find(name);
  if (it == modules.end()) {
    return false;
  }
  return it->second->state == ModuleInfo::Loaded;
}

void ModuleMgr::scanPluginsDir() {
  std::string pluginsDir = getAvoxPluginsDir();
#ifdef WIN32
  std::string pattern = pluginsDir + "\\avox_*.dll";
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    std::string filename = fd.cFileName;
    size_t dot = filename.find_last_of('.');
    std::string modName =
        (dot != std::string::npos) ? filename.substr(0, dot) : filename;
    registerModule(modName.c_str());
    log(LogLevel::info, "discovered plugin: " + modName);
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
#elif defined(__linux__) || defined(__ANDROID__)
  DIR* dir = opendir(pluginsDir.c_str());
  if (!dir) {
    return;
  }
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    std::string filename = ent->d_name;
    // libavox_*.so
    if (filename.rfind("libavox_", 0) != 0) {
      continue;
    }
    if (filename.size() < 7 || filename.substr(filename.size() - 3) != ".so") {
      continue;
    }
    // 模块名 = 去 "lib" 前缀 + ".so" 后缀
    std::string modName = filename.substr(3, filename.size() - 3 - 3);
    registerModule(modName.c_str());
    log(LogLevel::info, "discovered plugin: " + modName);
  }
  closedir(dir);
#endif
}

void ModuleMgr::ensureStarted() {
  if (bStarted) {
    return;
  }
  bStarted = true;
  startup();
}

void ModuleMgr::startup() {
  // 1. 扫描 plugins/ 目录发现动态插件
  scanPluginsDir();
  // 2. 加载所有已注册模块(静态注册表 + 扫描发现的动态)
  // 收集名字副本, 避免递归过程中 modules 变化的影响
  std::vector<std::string> names;
  names.reserve(modules.size());
  for (const auto& kv : modules) {
    names.push_back(kv.first);
  }
  for (const auto& n : names) {
    loadModule(n.c_str());
  }
}

void checkModelLoad(const char* modelName) {
  ModuleMgr::Get().checkLoadModel(modelName);
}

}