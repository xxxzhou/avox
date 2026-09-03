# plugins/ —— 动态组件模块

把依赖大三方库的可选模块（opencv / onnx / sherpa / translation / inpaint 等）做成独立「组件」，
编译为 `avox_<name>.dll/.so`，输出到 avox.dll 同级的 `plugins/` 子目录。avox 首次访问时（lazy）
扫描该目录，发现 `avox_*` 开头的 dll 并加载，运行期探测能力。设计见 `doc/plan/动态加载组件设计.md`。

## 总开关与组件 option
- 根 `CMakeLists.txt` 只有一个 `option(AVOX_ENABLE_PLUGINS)`：ON 时 `add_subdirectory(plugins)`。
- 各组件 option（`AVOX_ENABLE_OPENCV` 等）定义在 **`plugins/options.cmake`**，根 CMakeLists 早期
  `include` 它（在 `AVOXOptions` / `add_subdirectory(src)` 之前），保证 src 的 gate（如 inpaint）可见。
- 第三方库的 `find_package` / link 过渡期仍在 `cmake/AVOXOptions.cmake`（对应 src 模块还在用），
  待各模块迁到 plugins 后再移入。

## 目录结构
```
plugins/
├── CMakeLists.txt       # PLUGINS_STATIC / PLUGINS_DYNAMIC 列表 + add_subdirectory(avox_<name>)
├── options.cmake        # 组件 option 定义(根早期 include)
└── avox_opencv/          # 组件目录(avox_<name>)
    ├── CMakeLists.txt   # register_plugin(avox_opencv DYNAMIC LIBS ... DEP_DLLS ...)
    ├── OpencvHelper.hpp/cpp   # 组件业务代码(原有)
    └── OpencvModule.hpp/cpp   # IModule 子类(loadModule 探测) + AVOX_REGISTER_MODULE
```

## 命名约定（重要）
| 概念 | 取值 | 用于 |
|------|------|------|
| 目录名 | `avox_<name>`（全名，含 avox 前缀） | 物理目录 |
| dll 名 | `avox_<name>.dll` / `libavox_<name>.so`（自动推导） | 产物 |
| **模块名** | `avox_<name>`（= dll stem，**全名**） | register / 扫描匹配 / `checkLoadModel` / `dependsOn`，全链路一致 |
| **IModule 子类** | `<Name>Module`（**不带 Avox 前缀**，目录已含） | 如 `OpencvModule` / `OnnxModule` / `SherpaModule` |

类名（短）与模块名（全）分离，在 `AVOX_REGISTER_MODULE(OpencvModule, avox_opencv)` 同时给出。

## 新增组件步骤
1. 建目录 `plugins/avox_<name>/`，放业务代码 + `<Name>Module.{hpp,cpp}`（`loadModule` 探测）。
2. `plugins/avox_<name>/CMakeLists.txt`：`register_plugin(avox_<name> DYNAMIC LIBS ... DEP_DLLS ...)`。
3. `plugins/options.cmake`：加 `option(AVOX_ENABLE_<NAME> "..." ON)`。
4. `plugins/CMakeLists.txt`：把 `avox_<name>` 加到 `PLUGINS_DYNAMIC`（或 `PLUGINS_STATIC`）列表。
5. 若该组件原在 src 下，清理 `src/CMakeLists.txt` 的 `add_sub_path` + 业务代码的相对 include。

## 编译模式（PLUGINS_STATIC / PLUGINS_DYNAMIC）
- **DYNAMIC**：独立 SHARED target，输出到 `${CMAKE_INSTALL_PREFIX}/$<CONFIG>/plugins/`，运行期 dlopen。
- **STATIC**：源码仍由 `src/CMakeLists.txt` 的 `add_sub_path` 编进 avox（老路），`StaticLinkModule` 注册。
- **「提升到内置」= 把名字从 `PLUGINS_DYNAMIC` 挪到 `PLUGINS_STATIC`，业务代码 0 改动。**

## register_plugin 参数
- `DEPS`：依赖的其它 avox 组件名（`avox_<name>`），`loadModule` 递归 + 失败传播。
- `LIBS`：该组件链接的第三方库（`${OpenCV_LIBRARIES}` 等）。
- `DEP_DLLS`：运行期依赖的第三方 dll（Windows，复制到 `plugins/` 自包含）。

## 跨 dll 铁律
- plugin 编译时只定义 `AVOX_PLUGIN_BUILDING`（导出 `NewModule` / `GetModuleABI`），**绝不定义 `AVOX_EXPORT_DEFINE`**
  （否则 `IModule` 被当 dllexport，虚表跨 dll 错乱）。由 `register_plugin` 自动设，组件 CMakeLists 不用手动。
- `IModule` 对象由 plugin `new`，unload 时直接 `delete`（实测 avox /MT 配置下跨 dll new/delete 安全：
  UCRT 静态堆复用 `GetProcessHeap()`，所有 /MT 模块共享进程默认堆；详见设计文档 §13）。
  业务侧用 `unique_ptr` / `delete` 即可，**无需 `destroy()` 自销毁**。
- `IModule::loadModule(IOption*)` 语义 = 运行期能力探测 + 初始化（false = 不可用 ≠ 崩溃）。
- 静态模式（iOS/WASM）：`AVOX_ENABLE_STATIC` 由 CMake 在 `AVOX_DLL_TYPE==STATIC` 时定义，
  `AVOX_REGISTER_MODULE` 自动走 `StaticLinkModule` 构造期注册（不导出 `NewModule`），`AVOX_PLUGIN_API` 为空。
  插件 .cpp 须 `#include "module/ModuleMgr.hpp"`（静态分支实例化 `StaticLinkModule` 需要）。

## 平台
- Windows / Linux：DYNAMIC（扫描 `plugins/` + dlopen）。
- iOS / Apple：强制 STATIC（App Store 禁止加载第三方可执行代码）。
- Android：STATIC 为主（`jniLibs` 不能枚举目录，扫描做不成）。
- 详见设计文档 §8。
