# WebAssembly 支持

AVPlay 支持 WebAssembly 编译目标，可在浏览器环境中运行。

## 安装步骤

### 1. 安装 Emscripten SDK

根据 [emsdk 文档](https://emscripten.org/docs/getting_started/downloads.html) 安装 emsdk。

Windows 下命令：

```bat
emsdk.bat install latest
emsdk.bat activate latest
emsdk_env.bat
```

### 2. 安装构建工具

Windows 使用 emcmake 编译，如果只有 MSVC 编译器，emcmake 会提示找不到兼容的 cmake 生成器：

```
emcmake: no compatible cmake generator found; Please install ninja or mingw32-make
```

解决方案：[Windows 上 VSCode 中配置 MinGW](https://blog.csdn.net/qq_40280673/article/details/142245539) 安装 mingw32-make。

## 构建

```bash
python build_wasm.py
```

构建脚本会执行：

```bash
# 构建 CMake 配置文件，生成 Makefile
emcmake cmake ../..

# 编译
emmake make -s WASM=1 -s USE_PTHREADS=1
```

## 导出函数

默认情况下，Emscripten 生成的代码只会调用 `main()` 函数，其他的函数将被视为无用代码。在一个函数名之前添加 `EMSCRIPTEN_KEEPALIVE` 能够防止这样的事情发生：

```cpp
#include <emscripten/emscripten.h>

extern "C" {
  EMSCRIPTEN_KEEPALIVE
  void myExportedFunction() {
    // ...
  }
}
```

AVPlay 使用 `AVOX_EXPORT` 宏统一处理导出：

```cpp
#if defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
  #define AVOX_EXPORT EMSCRIPTEN_KEEPALIVE
#endif
```

## 线程支持

AVPlay WebAssembly 版本支持 pthreads 多线程：

```bash
emmake make -s WASM=1 -s USE_PTHREADS=1 \
  -s EXPORTED_RUNTIME_METHODS=ccall,cwrap \
  -s EXPORTED_FUNCTIONS=[]
```

## 调试

启动本地 HTTP 服务器：

```bash
python -m http.server 9980
```

然后在浏览器中访问 `http://localhost:9980`。

## 当前支持状态

| 功能 | 状态 |
|------|------|
| 基础编译 | ✅ 已完成 |
| pthreads 多线程 | ✅ 已完成 |
| 音频播放 | 🔄 计划中 |
| 视频渲染 (WebGL) | 🔄 计划中 |
| Vulkan 计算 | ❌ 不支持（浏览器限制）|

## CMake 配置参考

[QtWasmCMake](https://github.com/OlivierLDff/QtWasmCMake/blob/master/QtWasmCMake.cmake)

## 文档

- [C++ 项目转成 wasm 全过程](https://zhuanlan.zhihu.com/p/158586853)
- [编译 C/C++ 为 WebAssembly](https://developer.mozilla.org/zh-CN/docs/WebAssembly/C_to_Wasm)
- [wasm-threading](https://42yeah.github.io/webassembly/2019/01/14/wasm-threading.html)
- [C/C++ 面向 WebAssembly 编程](https://www.hellobit.com.cn/b/767368973/3524648429.html)
