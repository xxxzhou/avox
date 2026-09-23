# SoundTouch Android 预编译库构建脚本(与 build_windows.py 同源, 目标平台 android)
#
# 产物布局(FindSoundTouch.cmake android 分支约定: android/soundtouch/<abi>):
#   <OUT>/<abi>/include/soundtouch/  头文件
#   <OUT>/<abi>/lib/libSoundTouch.a  静态库
#
# 源码仓(不在 avox 内, 保持上游原样):
#   git clone https://codeberg.org/soundtouch/soundtouch.git <某目录>
#   git checkout 2.4.1                (2026-09 时点最新 release)
#
# 用法(任意目录):
#   python build_android.py                       # 源码目录默认 ../../soundtouch(与 avox 平级), arm64-v8a
#   python build_android.py --src D:/Work/github/soundtouch
#   python build_android.py --abi armeabi-v7a     # 32 位(上游 NEON 分支自动启用)
#
# 说明: 纯 C++17, 无外部依赖, LGPL-2.1; 静态库自包含,
#       c++_static 与 avox android 产物 STL 口径一致。
import argparse
import os
import shutil
import subprocess
import sys

AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SRC = os.path.join(os.path.dirname(AVOX_ROOT), "soundtouch")
DEFAULT_OUT = os.path.join(os.path.dirname(AVOX_ROOT), "avox_library",
                           "3rdparty", "library", "android", "soundtouch")
DEFAULT_NDK = os.path.join(os.environ.get("ANDROID_SDK_ROOT",
                                          r"C:\Users\mfjt5\AppData\Local\Android\Sdk"),
                           "ndk", "26.1.10909125")
NINJA_CANDIDATES = [
    os.path.join(os.environ.get("ANDROID_SDK_ROOT",
                                r"C:\Users\mfjt5\AppData\Local\Android\Sdk"),
                 "cmake", "3.22.1", "bin", "ninja.exe"),
]


def run(cmd):
    print("+ " + " ".join(cmd), flush=True)
    if subprocess.call(cmd) != 0:
        print("FAILED:", " ".join(cmd))
        sys.exit(1)


def find_ninja():
    exe = shutil.which("ninja")
    if exe:
        return exe
    try:
        import ninja
        cand = os.path.join(ninja.BIN_DIR, "ninja.exe")
        if os.path.isfile(cand):
            return cand
    except ImportError:
        pass
    for cand in NINJA_CANDIDATES:
        if os.path.isfile(cand):
            return cand
    sys.exit("缺少 ninja: pip install ninja 或 Android SDK cmake 目录")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default=DEFAULT_SRC, help="soundtouch 源码目录(默认 %s)" % DEFAULT_SRC)
    parser.add_argument("--abi", default="arm64-v8a", choices=["arm64-v8a", "armeabi-v7a"])
    parser.add_argument("--api", default="24", help="ANDROID_PLATFORM API level")
    parser.add_argument("--ndk", default=DEFAULT_NDK)
    parser.add_argument("--clean", action="store_true", help="清空源码仓内 build 目录重编")
    args = parser.parse_args()

    toolchain = os.path.join(args.ndk, "build", "cmake", "android.toolchain.cmake")
    if not os.path.isfile(toolchain):
        sys.exit(f"NDK toolchain 不存在: {toolchain}(用 --ndk 或 ANDROID_SDK_ROOT 指定)")
    src_dir = os.path.abspath(args.src)
    if not os.path.isfile(os.path.join(src_dir, "CMakeLists.txt")):
        sys.exit(f"源码目录无效(缺 CMakeLists.txt): {src_dir}\n"
                 f"clone https://codeberg.org/soundtouch/soundtouch.git 后 checkout 2.4.1, 再用 --src 指定")
    out_dir = os.path.join(os.environ.get("AVC_SOUNDTOUCH_OUT", DEFAULT_OUT), args.abi)
    build_dir = os.path.join(src_dir, "build", f"android-{args.abi}")
    if args.clean and os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    configure = [
        "cmake", "-S", src_dir, "-B", build_dir, "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={find_ninja()}",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DANDROID_ABI={args.abi}",
        f"-DANDROID_PLATFORM=android-{args.api}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=OFF",
        # c++_static 与 avox android 产物一致; float 采样(上游默认), NEON 自动开
        "-DANDROID_STL=c++_static",
        "-DSOUNDSTRETCH=OFF",
        "-DSOUNDTOUCH_DLL=OFF",
        # 上游 cmake_minimum_required(VERSION 3.5), CMake>=4 需放行; 旧版忽略
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_INSTALL_PREFIX=" + out_dir,
    ]
    run(configure)
    run(["cmake", "--build", build_dir])
    run(["cmake", "--install", build_dir])

    lib = os.path.join(out_dir, "lib", "libSoundTouch.a")
    inc = os.path.join(out_dir, "include", "soundtouch", "SoundTouch.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        print("FAILED: 产物不完整, 期待", lib, "和", inc)
        sys.exit(1)
    print("OK:", out_dir)


if __name__ == "__main__":
    main()
