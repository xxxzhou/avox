#pragma once

#include <any>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>

#include "ModuleMgr.hpp"

namespace avox {

// 注册函数
struct RegFunc {
  std::string desc;
  std::function<void()> func;
};

// OBJTYPE对应一个OBJCLASS实现
template <typename OBJTYPE, typename OBJCLASS, typename CLASSDESC>
struct RegeditObj {
public:
  struct ObjectInfo {
    std::function<OBJCLASS *()> initFunc;
    CLASSDESC desc;
  };

private:
  // 存储对象类型与包含创建对象函数及对应描述信息的结构体之间的映射关系
  std::map<OBJTYPE, ObjectInfo> funcMap;

public:
  // 注册函数，将对象类型、创建对象的函数（可带参数）以及对象描述信息关联起来
  void regInitFunc(const OBJTYPE &type, const CLASSDESC &desc,
                   std::function<OBJCLASS *()> func) {
    ObjectInfo info = {};
    info.desc = desc;
    info.initFunc = func;
    funcMap[type] = info;
  }

  // 根据对象类型创建对应的对象实例，可传递初始化参数
  const ObjectInfo &initFunc(const OBJTYPE &type) { return funcMap[type]; }

  // 检查指定的对象类型是否已注册
  bool hasObjectId(const OBJTYPE &type) {
    return funcMap.find(type) != funcMap.end();
  }
};

// OBJTYPE对应多个OBJCLASS实现
template <typename OBJTYPE, typename OBJCLASS, typename CLASSDESC>
struct RegeditObjList {
public:
  struct ObjectInfo {
    std::function<OBJCLASS *()> initFunc;
    CLASSDESC desc;
  };

private:
  // 存储对象类型与包含创建对象函数及对应描述信息的结构体之间的映射关系
  std::map<OBJTYPE, std::vector<ObjectInfo>> funcMap;

public:
  // 注册函数，将对象类型、创建对象的函数（可带参数）以及对象描述信息关联起来
  void regInitFunc(const OBJTYPE &type, const CLASSDESC &desc,
                   std::function<OBJCLASS *()> func) {
    ObjectInfo info = {};
    info.desc = desc;
    info.initFunc = func;
    funcMap[type].push_back(info);
  }

  // 根据对象类型创建对应的对象实例，可传递初始化参数
  const std::vector<ObjectInfo> &initFuncs(const OBJTYPE &type) {
    return funcMap[type];
  }

  // 检查指定的对象类型是否已注册
  bool hasObjectId(const OBJTYPE &type) {
    return funcMap.find(type) != funcMap.end();
  }
};

// 插件工厂注册(string key + C函数指针, 无CLASSDESC, 跨DLL安全)
// 用 C 函数指针(非 std::function): std::function 的 type-erasure manager 实例化在
// plugin 的 reg 调用点(plugin dll), 进程退出 AvoxManager 析构晚于 plugin 卸载 -> 调已卸载 manager 崩溃。
// 裸函数指针析构不调代码, 无此患; 无捕获 lambda 隐式转换。
template <typename OBJCLASS> struct RegeditFactory {
  using Factory = OBJCLASS *(*)();

private:
  std::map<std::string, Factory> funcMap;

public:
  void reg(const std::string &name, Factory factory) { funcMap[name] = factory; }
  OBJCLASS *create(const std::string &name) const {
    ModuleMgr::Get().ensureStarted();
    auto it = funcMap.find(name);
    return it == funcMap.end() ? nullptr : it->second();
  }
  bool has(const std::string &name) const { return funcMap.count(name) > 0; }
};

// 注册管理器,每一个OBJTYPE只有一个全局的OBJCLASS实现
template <typename OBJTYPE, typename OBJCLASS> struct RegeditMgr {
public:
  struct ObjectInfo {
    std::function<OBJCLASS *()> initFunc;
    OBJCLASS *instance = nullptr;
  };
  ~RegeditMgr() { cleanup(); }

private:
  std::map<OBJTYPE, ObjectInfo> funcMap;

public:
  void regMgrObj(const OBJTYPE &type, std::function<OBJCLASS *()> func) {
    ObjectInfo info = {};
    info.initFunc = func;
    funcMap[type] = info;
  }
  // 延迟初始化，在第一次使用时才会初始化
  // 未注册的type返回nullptr(不能调空initFunc, 会抛bad_function_call)
  OBJCLASS *getMgr(const OBJTYPE &type) {
    auto &info = funcMap[type];
    if (!info.instance && info.initFunc) {
      info.instance = info.initFunc();
    }
    return info.instance;
  }

  void cleanup() {
    for (auto &pair : funcMap) {
      if (pair.second.instance) {
        delete pair.second.instance;
        pair.second.instance = nullptr;
      }
    }
    funcMap.clear();
  }
};

// 插件单例注册(string key + C函数指针 + 不delete实例, 跨DLL安全)
// 与 RegeditMgr 类似: 每个key只有一个全局实例, 首次get()时lazy创建。
// 与 RegeditMgr 的区别:
//   - 用C函数指针(非std::function) → 跨DLL安全(同RegeditFactory的理由)
//   - 不delete实例 → 退出时随进程/DLL卸载自动清理(不调已卸载DLL的析构)
//   - get()而非getMgr() → 语义"拿全局单例"(非"拿管理器")
// 典型用例: IPyRunner (Python 执行器是进程全局单例, 不可delete)
template <typename OBJCLASS> struct RegeditSingleton {
  using Factory = OBJCLASS *(*)();

private:
  std::map<std::string, Factory> funcMap;
  std::map<std::string, OBJCLASS *> instanceMap;

public:
  void reg(const std::string &name, Factory factory) { funcMap[name] = factory; }
  // 延迟初始化: 首次调时触发 ensureStarted + 工厂创建, 之后返回同一实例
  OBJCLASS *get(const std::string &name) const {
    auto it = instanceMap.find(name);
    if (it != instanceMap.end()) return it->second;
    // 先触发 startup (扫描 plugins/ + loadModule 注册工厂), 再查 funcMap
    ModuleMgr::Get().ensureStarted();
    auto fit = funcMap.find(name);
    if (fit == funcMap.end()) return nullptr;
    OBJCLASS *inst = fit->second();
    // const_cast: get 是 const 但 instanceMap 需写入 (lazy init)
    const_cast<std::map<std::string, OBJCLASS *> &>(instanceMap)[name] = inst;
    return inst;
  }
  bool has(const std::string &name) const { return funcMap.count(name) > 0; }
};

}