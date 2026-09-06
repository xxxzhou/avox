#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../AvoxBase.h"
#include "IModule.hpp"
namespace avox {

typedef IModule* (*loadModuleAction)();
typedef std::function<IModule*(void)> loadModuleHandle;

class ModuleInfo {
 public:
  enum State { NotLoaded, Loading, Loaded, Failed };
  void* handle = nullptr;            // dlopen/LoadLibrary 句柄(动态), nullptr(静态)
  IModule* module = nullptr;         // 模块实例
  std::string name = "";            // 逻辑名(avox_opencv)
  std::string fileName = "";        // 动态库文件名(avox_opencv.dll / libavox_opencv.so)
  loadModuleHandle onLoadEvent = nullptr;  // 静态注册工厂(非 null 表示静态)
  State state = NotLoaded;

 public:
  ModuleInfo(/* args */) {}
  ~ModuleInfo() {}
};

// 1. 后期扩展插件，用来管理插件
// 2. 管理模块初始化,如vulkan模块初始化调用
class AVOX_EXPORT ModuleMgr {
 public:
  static ModuleMgr& Get();
  ~ModuleMgr();

 protected:
  ModuleMgr();

 private:
  static ModuleMgr* instance;
  std::map<std::string, ModuleInfo*> modules;
  std::map<std::string, IOption*> moduleOptions;

 private:
  ModuleMgr(const ModuleMgr&) = delete;
  ModuleMgr& operator=(const ModuleMgr&) = delete;
  // lazy 启动标志: 首次 checkLoadModel/loadModule/ensureStarted 时触发 startup
  bool bStarted = false;
  // 外部指定插件目录(非空优先于 dll 同级 plugins/)。Android/Godot 场景:
  // jniLibs 平铺无法携带 plugins/ 子目录, 由宿主把插件 so 解压到可写目录后指定
  std::string customPluginsDir;
  // 推导 avox.dll/so 所在目录 / 其下 plugins 子目录
  std::string getAvoxDllDir();
  std::string getAvoxPluginsDir();
  // 扫描 plugins/ 目录, 发现 avox_* 动态插件并 registerModule
  void scanPluginsDir();

 public:
  // 动态加载模块
  void setOption(const char* name, IOption* options);
  void setPluginsDir(const char* dir);  // 覆盖插件扫描目录(必须在 startup/ensureStarted 之前)
  void registerModule(const char* name, loadModuleHandle handle = nullptr);
  // 加载(含递归依赖 + 失败传播), 返回是否 Loaded
  bool loadModule(const char* name);
  void unloadModule(const char* name);
  void regAndLoad(const char* name);
  // avox 启动时调: 扫描 plugins/ + 加载所有已注册模块
  void startup();
  // 幂等 lazy 触发: 首次调执行 startup(扫描 plugins/ + 加载), 之后直接返回。核心模块(如 SubtitleAsr)
  // 构造时调一次, 确保依赖的 plugin 工厂已注册, 无需业务程序显式 checkLoadModel
  void ensureStarted();

 public:
  bool checkLoadModel(const char* name);
};

template <class module>
class StaticLinkModule {
 public:
  // 静态注册: 全局对象构造期把工厂(lambda)注册进 ModuleMgr, 无需 dlopen。
  // 工厂独立于本对象(不捕获 this), registerModule 内拷贝进 std::function 即可。
  explicit StaticLinkModule(const std::string& name) {
    ModuleMgr::Get().registerModule(
        name.c_str(), []() -> IModule* { return new module(); });
  }
};
}
