# avox 代码规范

## 常用AI指令

1. claude --dangerously-skip-permissions
2. 当前上下文太多了,你简洁成2千左右多字描述,我们重开一个会话重新开始,这2千描述需要让另一会话知道当前情况.(Deepseek不需要,其缓存命中只有没命中的1/30,而GLM需要,其缓存命中只有没命中的1/5左右)

## 1. 命名规范

| 类型 | 规则 | 示例 |
|------|------|------|
| 类/结构/枚举 | `PascalCase` | `MediaPlayer`, `AudioDesc` |
| 函数/方法 | `camelCase` | `getVolume()`, `setHardDecode()` |
| 类成员变量 | `snakeCase`（禁止 `_` 前后缀） | `int inputSize;` ✅ / `int _inputSize;` ❌ |
| 方法参数 | 允许 `_` 后缀 | `void setUrl(const char* url_)` |
| 常量/宏 | `kPascalCase` 或 `UPPER_CASE` | `kMaxSize`, `AVOX_EXPORT` |
| 回调类 | `IXxxOb` 后缀 | `IMediaPlayerOb` |

命名空间：所有代码在 `avox` 命名空间内（`namespace avox {` / `}`）。

## 2. .h 文件类型约束

`.h` 文件需导出给 C#/Java/Node.js（SWIG），**禁止使用 std 类型**：

| ❌ 禁止 | ✅ 替代 |
|---------|---------|
| `std::string` | `const char*` |
| `std::vector<T>` | `T*` + `getCount()` / `getIndex(int)` |
| `std::shared_ptr<T>` | 裸指针 `T*` + `create*/destroy*` |
| `std::unique_ptr<T>` | 裸指针 `T*` |
| `std::function<...>` | 回调类 `IXxxOb` |

```cpp
// ❌ AVOX_EXPORT std::string getFileName();
// ❌ AVOX_EXPORT std::vector<int> getTrackIds();
// ✅
AVOX_EXPORT const char* getFileName();
AVOX_EXPORT int getTrackCount();
AVOX_EXPORT int getTrackId(int index);
```

## 3. 模块导出规范

### 文件约定

| 文件 | 用途 |
|------|------|
| `*Export.h` | 对外暴露的 C 接口/抽象类 |
| `*Helper.hpp` | 内部类型转换（不导出，避免暴露第三方类型如 `cv::Mat`/`AVPacket`） |

### Export.h 模板

```cpp
#pragma once
#include "avox/AvoxPlayer.h"
namespace avox {
class IRtcPlayer {
 public:
  IRtcPlayer() = default;
  virtual ~IRtcPlayer() = default;
 public:
  virtual void setRollType(RtcRollType type) = 0;
  virtual bool open() = 0;
  virtual void close() = 0;
};
extern "C" {
AVOX_EXPORT IRtcPlayer* createWebRtcPlayer();
AVOX_EXPORT void addRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob);
AVOX_EXPORT void removeRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob);
}
}
```

### 关键规则

- `create*()` 返回需释放的对象，`get*()` 返回托管对象（不需释放）
- 回调注册：`addXxxOb()` / `removeXxxOb()`
- 回调对象释放前，若被观察对象还存在，必须先 `removeXxxOb` 注销

## 4. 跨 DLL 安全规范

avox 全链路 /MT（静态 CRT），每个 DLL 独立 CRT 堆。简单跨 DLL new/delete 安全，但 **STL 容器跨 DLL 传递必崩**（inline 析构在错误 CRT 释放 → `__acrt_first_block == header`）。

| ✅ 安全 | ❌ 不安全 |
|---------|----------|
| `const char*`（只读不释放） | `std::string` / `std::vector` 跨 DLL 传递 |
| 原始类型/枚举/C 函数指针 | 返回 STL 容器的虚函数/导出函数 |
| inline 函数（每个 DLL 自己实例化） | |
| 虚析构（dispatch 到子类 DLL 的 CRT） | |

**设计原则**：插件接口只传 `const char*` 和原始类型；日志/工具函数用 inline 实现（放头文件）；STL 容器只在本 DLL 内构造和析构。

**为什么必须 /MT —— 一个 /MD 的反面实证（2026-09-14）**

`std::mutex` / `std::condition_variable` 这类**不是纯头文件实现**：对象布局由编译期头文件内联
的构造函数给出，加锁行为却由**进程加载到的那个 `msvcp140.dll`** 提供。模块用 `/MD` 就出现两个
版本源：你的构建工具链（如 14.44）与目标机器 `System32` 里的旧 VC++ Redist（实测 14.27）。
两版对 in-situ mutex 的内部表示不兼容（新版 `_Type=2` 且 `_Critical_section` 惰性构造、
`_Mtx_lock` 按 `mtx+0x10` 直接调用；旧版立即构造并按 `mtx+8` 解引用 CS 指针），于是
**第一次 `std::lock_guard` 就 `0xC0000005` 读地址 0 崩溃**：

- **编译期零告警**，且触发点与崩溃点在语义上毫无关系（表现为"某个完全无关的操作一用就崩"）；
- 只影响 `/MD` 且用到这类同步原语的模块；同进程里 `/MT` 的 DLL（如 avox.dll）完全没事 ——
  排查时最容易被误导的地方正是"引擎没事，只有我的插件崩"。

所以**集成方（宿主程序、UE/Unity/Godot/Flutter 胶水插件）也必须继承 `/MT` 约定**，不要依赖
目标机器上的 VC++ Redistributable。确实必须 `/MD` 时：把与构建工具链同版本的
`msvcp140*.dll` / `vcruntime140*.dll` / `concrt140.dll` 随包放进 exe 目录（exe 目录优先于
`System32`），商店包同样必须自带，并在打包脚本里校验版本。

自检：`dumpbin /dependents your_plugin.dll` 不应出现 `MSVCP140.dll`。

**本仓现状（2026-09-14 全量扫描 `install/AMD64/Release`）**：`avox.dll`、全部 avox 插件与
全部 54 个测试 exe 均**无动态 CRT 依赖**（静态 /MT），因此本仓自身不会踩上面那条；少数三方
DLL 例外：`plugins/opencv_world4130.dll` 依赖 `MSVCP140.dll`（OpenCV 官方 /MD 构建，随包分发
时按上条处理），`fdk-aac.dll` / `libcrypto-3-x64.dll` / `libssl-3-x64.dll` 只依赖
`VCRUNTIME140.dll`（纯 C 运行时，跨版本稳定，风险低）。

## 5. 日志宏

```cpp
// LogHelper.hpp — inline 零分配，跨 DLL 安全
inline const char* extractFileInline(const char* path) {
  if (!path) return "";
  const char* file = path;
  while (*path) { if (*path == '/' || *path == '\\') file = path + 1; path++; }
  return file;
}
#define LOGFLF(level, ...)                                                    \
  log(level, extractFileInline(__FILE__), ":", __LINE__, " ", __func__, " ",  \
      __VA_ARGS__)
// LOGFLF(LogLevel::info, "state: ", state);
// → AvoxPlayer.cpp:123 onStateChange state: playing
```

## 6. 枚举定义宏

```cpp
#define AVOX_MAP_PLAYER_STATE(XX) \
  XX(none, 0, "none") XX(playing, 1, "playing") XX(pause, 2, "pause")

enum class PlayerState {
#define XX(name, value, str) name = value,
  AVOX_MAP_PLAYER_STATE(XX)
#undef XX
};
// 自动生成 getPlayerStateStr() 字符串转换
```

## 7. 条件编译

```cpp
// ❌ 模块内部不要用 #ifdef AVOX_ENABLE_XXX 包裹自己的方法
//    src/CMakeLists.txt 已通过 add_sub_path() 控制编译
// ✅ 引用其他可选模块功能时才需要
#ifdef AVOX_ENABLE_ZLMEDIAKIT
void useZlMediaKit() { /* ... */ }
#endif
```

## 8. 线程安全

| 方法类型 | 线程要求 |
|----------|----------|
| `open/close/seek` | 可跨线程（内部命令队列） |
| `getState/getPosition` | 可跨线程（原子/锁） |
| 回调函数 | 在调用者线程，不要阻塞 |
| 渲染方法 | 渲染线程 |

注意：回调内不要调用播放器同步方法；复杂操作用命令队列。

## 9. 资源生命周期

| 创建方式 | 所有权 | 释放 |
|----------|--------|------|
| `create*()` | 调用者 | 用户 `delete` |
| `get*()` | 被调用者 | 不需释放 |
| 回调参数 | 调用期间有效 | 不要保存指针 |

```cpp
IMediaPlayer* player = createMediaPlayer();       // 用户所有
ISurfaceRender* render = player->getSurfaceRender(); // 托管，不释放
MyOb ob;
addMediaPlayerOb(player, &ob);                    // 注册引用，不获取所有权
removeMediaPlayerOb(player, &ob);                 // ob 释放前必须先注销
delete player;
```

## 10. 第三方库集成

| 类型 | 位置 | 示例 |
|------|------|------|
| 大型预编译库 | `3rdparty/library/{platform}/` | FFmpeg, WebRTC, OpenCV |
| 源码编译库 | `3rdparty/{name}/` (submodule) | sherpa-onnx, ZLMediaKit |

CMake 流程：根 `option(AVOX_ENABLE_XXX)` → `cmake/FindXXX.cmake` 查找 → `AVOXOptions.cmake` 设 `AVOX_ENABLE_XXX` ON/OFF → `src/CMakeLists.txt` 按 option `add_sub_path()`。

## 11. SWIG 绑定

- `avox/*.h` 核心头文件：始终 `%include`
- `avox_xxx/*Export.h`：用 `#ifdef AVOX_ENABLE_XXX` 包裹
- 生成的 wrap 文件（如 `swig/nodejs/files/`）不入库, 构建时由 swig 重新生成
- `create*()` 返回需释放对象：加 `%newobject` 标记

## 12. Git 提交

格式：`<type>(<scope>): <subject>`

| Type | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `refactor` | 重构 |
| `docs` | 文档 |
| `test` | 测试 |
| `chore` | 构建/工具 |

## 附录：平台宏

| 平台 | 宏 | 目录 |
|------|-----|------|
| Windows | `_WIN32` | `avox_windows/` |
| Android | `__ANDROID__` | `avox_android/` |
| iOS/macOS | `__APPLE__` + `TARGET_OS_IPHONE`/`TARGET_OS_OSX` | `avox_apple/` |
| Linux | `__LINUX__` | `avox_linux/` |
