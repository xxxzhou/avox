#pragma once

#include <string>
#include <vector>

#include "../AvoxBase.h"

// ============ plugin 导出宏(独立于 AVOX_EXPORT) ============
// AVOX_EXPORT 用于 avox 内部类的导出/导入(由 AVOX_EXPORT_DEFINE 切换);
// plugin 自己的 C 工厂函数(NewModule/GetModuleABI)用 AVOX_PLUGIN_API,
// 由 AVOX_PLUGIN_BUILDING 切换: plugin 编译时 CMake 传 AVOX_PLUGIN_BUILDING(dllexport),
// avox 主程序消费时不定义(dllimport)。
// 静态链接(AVOX_ENABLE_STATIC, iOS/WASM 等无 dll 边界)时为空 —— 同一二进制内符号直接可见,无需导出。
// 铁律: 动态模式 plugin 编译时绝不定义 AVOX_EXPORT_DEFINE(否则 IModule 被 dllexport, 跨 dll 虚表错乱)。
#if AVOX_ENABLE_STATIC
  #define AVOX_PLUGIN_API
#elif defined(_WIN32)
  #ifdef AVOX_PLUGIN_BUILDING
    #define AVOX_PLUGIN_API __declspec(dllexport)
  #else
    #define AVOX_PLUGIN_API __declspec(dllimport)
  #endif
#else
  #define AVOX_PLUGIN_API __attribute__((visibility("default")))
#endif

// plugin ABI 版本: NewModule/GetModuleABI 守卫, avox 与 plugin 不一致则跳过该插件
#define AVOX_PLUGIN_ABI_VERSION 1

// 统一注册入口(插件 .cpp 末尾调用):
// - 静态(iOS/WASM, AVOX_ENABLE_STATIC): 全局 StaticLinkModule 对象构造期注册工厂进 ModuleMgr,
//   无 dll, 不导出符号。此分支实例化 StaticLinkModule, 插件 .cpp 须 #include "module/ModuleMgr.hpp"。
// - 动态(Win/Linux): 导出 NewModule + GetModuleABI, 运行期 dlopen 后取符号 + ABI 校验。
#if AVOX_ENABLE_STATIC
#define AVOX_REGISTER_MODULE(ModuleClass, name)                       \
  static avox::StaticLinkModule<ModuleClass> _linkMod_##name(#name);
#else
#define AVOX_REGISTER_MODULE(ModuleClass, name)                        \
  extern "C" AVOX_PLUGIN_API IModule* NewModule() { return new ModuleClass(); } \
  extern "C" AVOX_PLUGIN_API int GetModuleABI() { return AVOX_PLUGIN_ABI_VERSION; }
#endif

namespace avox {
// 做插件模块的基类
class AVOX_EXPORT IModule {
 private:
  void* handle = nullptr;
  // 依赖列表(成员, 随 IModule 构造/析构, 虚析构走插件 CRT → 跨 DLL 安全)
  std::vector<std::string> deps;

 public:
  IModule(/* args */) = default;
  virtual ~IModule() = default;

 public:
  // load module 时调用(语义: 运行期能力探测 + 初始化, false=不可用≠崩溃)
  virtual bool loadModule(IOption* option) { return false; };
  // unload module 时调用
  virtual void unloadModule() {}
  // 添加依赖(子类构造函数中调用, 同 CRT 分配, 安全)
  void addDep(const char* name) { deps.emplace_back(name); }
  // 依赖数量(跨 DLL 安全: 返回 int)
  int depCount() const { return static_cast<int>(deps.size()); }
  // 按索引取依赖名(跨 DLL 安全: 返回 const char* 指向本对象内部, 生命周期=本对象)
  const char* getDep(int index) const { return deps[index].c_str(); }
};

}
