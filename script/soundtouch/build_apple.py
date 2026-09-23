import os
import shutil
import subprocess
import sys

# SoundTouch macOS arm64 / iOS arm64 静态库构建(与 build_windows.py 同源, 2.4.1)
# 纯 C++17, 无外部依赖, LGPL-2.1
# 产物布局(与 windows/soundtouch 同构):
#   <avox_library>/3rdparty/library/<darwin|ios>/soundtouch/{include/soundtouch, lib/libSoundTouch.a}
#
# 源码仓不在 avox 内(上游保持原样): 脚本自动 shallow clone 2.4.1 到 _cache;
# 也可 --src 指定已 clone 的目录
#
# 用法:
#   python3 build_apple.py macos
#   python3 build_apple.py ios

TARGET = sys.argv[1] if len(sys.argv) > 1 else "macos"
if TARGET not in ("macos", "ios"):
    sys.exit("用法: python3 build_apple.py macos|ios")
LIB_PLATFORM = "darwin" if TARGET == "macos" else "ios"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AVC_LIBRARY = (os.environ.get("AVC_SOUNDTOUCH_AVC_LIBRARY")
               or os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
               or os.path.join(os.path.dirname(ROOT), "avox_library"))
OUT_DIR = os.path.join(AVC_LIBRARY, "3rdparty", "library", LIB_PLATFORM, "soundtouch")
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")
SRC_DEFAULT = os.path.join(CACHE, "soundtouch")

IOS_MIN = "13.0"
MACOS_MIN = "11.0"


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    ret = subprocess.run(cmd).returncode
    if ret != 0:
        sys.exit(f"命令失败(exit {ret}): {' '.join(cmd)}")


def ensure_src(src):
    if os.path.isfile(os.path.join(src, "CMakeLists.txt")):
        return src
    if src != SRC_DEFAULT:
        sys.exit(f"源码目录无效(缺 CMakeLists.txt): {src}")
    # codeberg 直连偶发 schannel 断连(Windows), mac/linux 侧一般正常; 失败重试
    run(["git", "clone", "--depth", "1", "--branch", "2.4.1",
         "https://codeberg.org/soundtouch/soundtouch.git", src])
    return src


def main():
    argv = sys.argv[2:]
    src = argv[argv.index("--src") + 1] if "--src" in argv else SRC_DEFAULT
    src = os.path.abspath(ensure_src(src))
    build = os.path.join(src, "build-" + TARGET)
    if os.path.isdir(build):
        shutil.rmtree(build)

    base = ["cmake", "-S", src, "-B", build,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DSOUNDSTRETCH=OFF",
            "-DSOUNDTOUCH_DLL=OFF",
            # 上游 cmake_minimum_required(VERSION 3.5), CMake>=4 需放行; 旧版忽略
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
            "-DCMAKE_INSTALL_PREFIX=" + OUT_DIR]
    if TARGET == "ios":
        base += ["-DCMAKE_SYSTEM_NAME=iOS",
                 "-DCMAKE_OSX_SYSROOT=iphoneos",
                 "-DCMAKE_OSX_ARCHITECTURES=arm64",
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={IOS_MIN}"]
    else:
        base += ["-DCMAKE_OSX_ARCHITECTURES=arm64",
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MIN}"]
    run(base)
    run(["cmake", "--build", build, "--config", "Release", "-j",
         str(os.cpu_count() or 4)])
    run(["cmake", "--install", build, "--config", "Release"])

    lib = os.path.join(OUT_DIR, "lib", "libSoundTouch.a")
    inc = os.path.join(OUT_DIR, "include", "soundtouch", "SoundTouch.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        sys.exit(f"产物不完整, 期待 {lib} 和 {inc}")
    print("OK:", OUT_DIR)


if __name__ == "__main__":
    main()
