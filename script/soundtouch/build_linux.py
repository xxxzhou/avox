# SoundTouch Linux 预编译库构建脚本(与 build_windows.py 同源, 目标平台 linux x64)
#
# 产物布局(FindSoundTouch.cmake linux 分支约定: linux/soundtouch):
#   <OUT>/include/soundtouch/  头文件
#   <OUT>/lib/libSoundTouch.a  静态库
#
# 源码仓(不在 avox 内, 保持上游原样):
#   git clone https://codeberg.org/soundtouch/soundtouch.git <某目录>
#   git checkout 2.4.1                (2026-09 时点最新 release)
#
# 用法(任意目录):
#   python3 build_linux.py                        # 源码目录默认 ../../soundtouch(与 avox 平级)
#   python3 build_linux.py --src ~/src/soundtouch
#
# 说明: 纯 C++17, 无外部依赖, LGPL-2.1; -fPIC 便于链入共享插件。
import argparse
import os
import shutil
import subprocess
import sys

AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SRC = os.path.join(os.path.dirname(AVOX_ROOT), "soundtouch")
DEFAULT_OUT = os.path.join(os.path.dirname(AVOX_ROOT), "avc_library",
                           "3rdparty", "library", "linux", "soundtouch")


def run(cmd):
    print("+ " + " ".join(cmd), flush=True)
    if subprocess.call(cmd) != 0:
        print("FAILED:", " ".join(cmd))
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default=DEFAULT_SRC, help="soundtouch 源码目录(默认 %s)" % DEFAULT_SRC)
    parser.add_argument("--clean", action="store_true", help="清空源码仓内 build 目录重编")
    args = parser.parse_args()

    src_dir = os.path.abspath(args.src)
    if not os.path.isfile(os.path.join(src_dir, "CMakeLists.txt")):
        sys.exit(f"源码目录无效(缺 CMakeLists.txt): {src_dir}\n"
                 f"clone https://codeberg.org/soundtouch/soundtouch.git 后 checkout 2.4.1, 再用 --src 指定")
    build_dir = os.path.join(src_dir, "build", "linux")
    if args.clean and os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    configure = [
        "cmake", "-S", src_dir, "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=OFF",
        # 链入 avox_tempo.so 共享插件需 PIC
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DSOUNDSTRETCH=OFF",
        "-DSOUNDTOUCH_DLL=OFF",
        # 上游 cmake_minimum_required(VERSION 3.5), CMake>=4 需放行; 旧版忽略
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_INSTALL_PREFIX=" + os.environ.get("AVC_SOUNDTOUCH_OUT", DEFAULT_OUT),
    ]
    run(configure)
    run(["cmake", "--build", build_dir, "-j", str(os.cpu_count() or 4)])
    run(["cmake", "--install", build_dir])

    out_dir = os.environ.get("AVC_SOUNDTOUCH_OUT", DEFAULT_OUT)
    lib = os.path.join(out_dir, "lib", "libSoundTouch.a")
    inc = os.path.join(out_dir, "include", "soundtouch", "SoundTouch.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        print("FAILED: 产物不完整, 期待", lib, "和", inc)
        sys.exit(1)
    print("OK:", out_dir)


if __name__ == "__main__":
    main()
