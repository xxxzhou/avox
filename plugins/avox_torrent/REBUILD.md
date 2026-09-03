# avox_torrent 依赖重编指引(新机器)

目标: 在新机器上编译 libtorrent 预编译产物(Windows + Android)并入库, 使 avplay 的 avox_torrent 插件可构建。
产物不进 git, 按 avc_library 惯例放置; avplay 侧由 `cmake/FindLibtorrent.cmake` 自动查找。

## 一. 目录约定(工作区根 = github/)

```
github/
├── avplay/                          # 主工程
├── avc_library/3rdparty/library/    # 产物入库位置(git 管理, 只收最终产物)
├── libtorrent/                      # 本依赖源项目(v2.0.14)
└── boost/                           # 仅头文件, 不入 git
```

## 二. 准备

1. **源码**(必须 v2.0.14, 新版本 API 有差异):
   ```bash
   git clone --depth 1 --branch v2.0.14 https://github.com/arvidn/libtorrent.git
   cd libtorrent && git submodule update --init --recursive
   ```
   ⚠️ 不 init 子模块必报 `deps/try_signal/try_signal.cpp: No such file` 配置失败。
2. **Boost 头文件**(libtorrent 编译和 avox_torrent 编译都要, 仅头文件无需构建):
   ```bash
   curl -L -o boost.tar.gz https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
   mkdir -p ../boost && tar -xzf boost.tar.gz -C ../boost --strip-components=1 boost_1_86_0/boost
   ```
   ⚠️ 校验布局必须是 `boost/boost/version.hpp`(嵌套两层)。放别处时同步改两个脚本里的 `BOOST_HINT`。
3. **工具链**: Windows: VS2019+/CMake4.x/Python3; Android: NDK r26+(设 `ANDROID_NDK` 环境变量)。

## 三. Windows 编译(libtorrent 项目内)

```bash
python build_windows.py
```
- 自动: 配置(静态+/MT/OpenSSL MT静态) → 编 torrent-rasterbar → 安装到 `../avc_library/3rdparty/library/windows/libtorrent/{include,lib}`
- 产物: `lib/torrent-rasterbar.lib`(约110MB Release 静态库)
- 重编: `AVOX_FORCE_RECONFIGURE=1 python build_windows.py` 或 `--clean`

注意事项:
- /MT 与 avplay 全局静态 CRT 对应, 改成 /MD 会 LNK2038 运行时库冲突
- encryption 默认 ON(MSE/PE 协议加密, 可连"强制加密"的 peer)。OpenSSL 取系统安装
  (`C:/Program Files/OpenSSL-Win64`, 可用环境变量 `OPENSSL_ROOT_DIR` 覆盖):
  static_runtime=ON 时 CMake FindOpenSSL 自动按 CRT 选中 **MT 静态版** 并封进 lib,
  无 DLL 依赖; 与 avplay 主程(httplib/ZLMediaKit 走 MD import lib + 部署 DLL)互不冲突,
  插件内一份静态、主程一份 DLL, 进程内两份拷贝互不共享状态, 这是安全的。
  `--no-encryption` 可退回纯明文(TORRENT_DISABLE_ENCRYPTION)
- 配置期 FindBoost 的 CMP0167 警告(CMake4)可忽略, 只要最终 `-- Configuring done`

## 四. Android 编译(libtorrent 项目内)

```bash
python build_android.py                       # 默认 arm64-v8a + armeabi-v7a
python build_android.py --abi arm64-v8a       # 单ABI
```
- 产物: `../avc_library/3rdparty/library/android/libtorrent/<abi>/{include,lib/torrent-rasterbar.a}`
- 参数: API level 24+, `-DANDROID_STL=c++_static`(与 avplay Android 侧一致), 其余选项同 Windows
- OpenSSL 按 ABI 自动探测库仓预编译(`avc_library/3rdparty/library/android/openssl/<abi>/{libssl.a,libcrypto.a}`):
  有则 encryption=ON(NDK 下 find_path 被 sysroot 重根, 脚本已显式钉住三件套路径), 没有则该 ABI 降级 encryption=OFF。
  当前 arm64-v8a 有预编译 OpenSSL(加密开), armeabi-v7a 暂无(加密关; 补编 v7a OpenSSL 入库后重跑脚本即自动升级)
- ⚠️ armeabi-v7a 需 NDK 内含 32 位工具链; 缺失时先只编 arm64
- ⚠️ avplay 当前 Android 加载是"类静态"路径(插件动态扫描在 Android 不可用, 见 plugins/README.md), Android 产物入库供后续静态链入方案使用, 本次插件本身不接 Android

## 五. 验证(avplay 侧)

```bash
python build_windows.py
```
- CMake 日志出现 `libtorrent found:` + Include/Library/Boost 四行 = 查找成功
- 找不到时 `AVOX_ENABLE_TORRENT` 自动降级关闭(不阻塞其他模块), 常见原因: 产物路径不对 / boost 布局不对
- 成功产物: `build/.../install/AMD64/<CONFIG>/plugins/avox_torrent.dll`

## 六. 已知坑速查

| 现象 | 原因/解法 |
|------|-----------|
| try_signal.cpp not found | 子模块没 init(见 二.1) |
| Could NOT find Boost | boost 目录缺失或少一层 boost/ 嵌套(见 二.2) |
| LNK2038 运行时库不匹配 | libtorrent 与 avplay 的 /MT|/MD 不一致 |
| **链接期大量 LNK2019(全部 lt 符号)** | **deprecated-functions=OFF 定义了 TORRENT_NO_DEPRECATE, 符号挪进 libtorrent::v2 inline namespace, 消费者没同步该宏。保持脚本默认 ON 即可, 两个脚本都别加这项** |
| 插件编过但链接缺 lt 符号 | 混用了旧版 libtorrent 头与库, 确认 v2.0.14 |
| C2039 best()/priority.hpp 等 | 源码版本不对, API 已变 |
| **Android 配置期 Could NOT find Boost** | **NDK 工具链把 find_path 重根进 sysroot, 外部 BOOST_ROOT 被过滤。脚本已直接传 `-DBoost_INCLUDE_DIR` 跳过搜索** |
| **Android install 期 Invalid character escape '\W'** | **libtorrent 的 pkg-config 安装辅助把 Boost_INCLUDE_DIR 原样写进生成的 cmake 文件, 反斜杠路径会炸。两个脚本传给 cmake 的路径一律用正斜杠** |

## 七. 涉及文件清单

- 独立项目: `libtorrent/build_windows.py`、`libtorrent/build_android.py`(本目录新增)
- avplay: `cmake/FindLibtorrent.cmake`(查找), `cmake/AVOXOptions.cmake`(find_package 挂接+降级), `plugins/options.cmake`(`AVOX_ENABLE_TORRENT` 开关), `plugins/CMakeLists.txt`(插件清单)
