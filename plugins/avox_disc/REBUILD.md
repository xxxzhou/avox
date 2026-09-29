# avox_disc 依赖重编指引(新机器)

目标: 编出 libbluray 预编译产物(Windows)入库, 使 avox_disc 插件可构建。
产物不进 git, 按 avox_library 惯例放置; avox 侧由 cmake/FindLibbluray.cmake 自动查找。

## 一. 目录约定(工作区根 = github/)

```
github/
├── avox/                        # 主工程
├── avox_library/3rdparty/library/windows/libbluray/   # 产物入库位置
└── libbluray/                   # 本依赖源项目(1.3.4, 含 contrib/libudfread 子模块)
```

## 二. 构建(MSYS2, 全程在 C:/msys64/usr/bin/bash.exe 里)

1. 工具链: `pacman -S --needed autoconf automake libtool pkg-config make`
   (mingw64 gcc 已随 FFmpeg 构建就位)
2. `./bootstrap` (autoreconf; 只需一次)
3. `./configure --disable-bd-j --disable-bdjava-jar --disable-examples \
     --disable-doxygen-doc --without-libxml2 --without-freetype --without-fontconfig \
     --prefix="/d/Work/github/avox_library/3rdparty/library/windows/libbluray"`
   - 不带 --without-libxml2 会在 configure 撞 PKG_CHECK 失败(默认查 libxml)
   - BD-J 关掉后零外部依赖; udfread 用 contrib 内置子模块(clone 后
     `git submodule update --init contrib/libudfread`)
4. `make -j && make install`

## 三. MSVC 导入库(make_msvc_lib.py 只认 av*/sw* 命名, 手工调函数)

```bash
cd avox/script/ffmpeg && python -c "
import sys, shutil, os; sys.path.insert(0, '.')
import make_msvc_lib as m
dll = r'D:/Work/github/avox_library/3rdparty/library/windows/libbluray/bin/libbluray-2.dll'
out = r'D:/Work/github/avox_library/3rdparty/library/windows/libbluray/lib'
m.make_lib(dll, out)
shutil.copy2(os.path.join(out, 'libbluray-2.lib'), os.path.join(out, 'libbluray.lib'))"
```

产物三件: bin/libbluray-2.dll(运行期, DEP_DLLS 拷进 plugins/), lib/libbluray.lib(MSVC
名字绑定导入库), include/。FindLibbluray.cmake 按 include/libbluray/bluray.h 探测。

## 四. 已知坑

- configure 报 "Package 'libxml-2.0' not found" = 漏了 --without-libxml2
- MSVC 链接报 lnk2019 bluray 符号 = 用了 MinGW 的 .dll.a 当导入库, 走第三步生成 .lib
- 中文/UNC 路径依赖 libbluray file_win32 层(见源码 src/file/file_win32.c);
  WebDAV/HTTP 源上的 ISO 不在 v1 范围(需 bd_open_stream 自定义 IO, 后置)
