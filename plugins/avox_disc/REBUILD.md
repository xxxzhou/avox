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

## 四. 其他平台(2026-09-29 四平台批)

同一源树(1.3.4), 统一 `--disable-shared --enable-static --disable-bdjava-jar
--disable-examples --without-libxml2 --without-freetype --without-fontconfig`,
产物落 avox_library/3rdparty/library/<平台>/libbluray/{include,lib}:

| 平台 | 宿主 | CC/CFLAGS | configure host | 产物位 |
|---|---|---|---|---|
| android arm64 | Windows+NDK26 clang.exe | `--target=aarch64-linux-android24 --sysroot=<ndk>/sysroot -O2 -fPIC` | `--host=aarch64-linux-android --build=x86_64-w64-mingw32` | android/libbluray/arm64-v8a |
| macOS arm64 | mac 原生 clang | 无(原生) | (默认) | darwin/libbluray |
| iOS 真机 | mac `xcrun -sdk iphoneos clang` | `-arch arm64 -miphoneos-version-min=13.0 -O2` | `--host=arm-apple-darwin` | ios/libbluray |
| iOS 模拟器 | mac `xcrun -sdk iphonesimulator clang` | `-arch arm64 -mios-simulator-version-min=13.0 -O2` | 同上 | ios-sim/libbluray(构建时设 `LIBBLURAY_DIR` 指此目录) |
| Linux x86_64 | WSL 原生 gcc | 无(原生) | (默认) | linux/libbluray |

- 跨平台复用 configure 的诀窍: 源树在 Windows 侧 `make dist` 出自包含
  tarball(libbluray-1.3.4.tar.bz2), 各机解包即 configure——免 per 机 autotools bootstrap。
- iOS 必带本仓补丁: src/file/mount_darwin.c 用 `TARGET_OS_IPHONE` 围掉
  DiskArbitration 段(iOS SDK 无此框架; 挂载枚举对文件路径直读无用)。
  补丁收在 libbluray 源仓本地分支 avox-disc-1.3.4。
- Android/Apple/Linux 全静态链进 libavox_disc.so/.dylib(无运行期 bluray 依赖,
  readelf NEEDED 不出现 libbluray = 验收过); Windows 走 dll+导入库(§三)。
- Android 加载同 avox_remote/avox_ass 的 setPluginsDir 模式(plugins/CMakeLists.txt
  ANDROID 段已加 avox_disc)。

## 五. 已知坑

- configure 报 "Package 'libxml-2.0' not found" = 漏了 --without-libxml2
- MSVC 链接报 lnk2019 bluray 符号 = 用了 MinGW 的 .dll.a 当导入库, 走第三步生成 .lib
- 中文/UNC 路径依赖 libbluray file_win32 层(见源码 src/file/file_win32.c);
  WebDAV/HTTP 源上的 ISO 不在 v1 范围(需 bd_open_stream 自定义 IO, 后置)
