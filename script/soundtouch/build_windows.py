# SoundTouch Windows 预编译库构建脚本(avox_tempo 插件的预编译依赖源)
#
# 产物布局(与 libsmb2/libtorrent 同级):
#   <AVC_SOUNDTOUCH_OUT>/include/soundtouch/  头文件(SoundTouch.h 等)
#   <AVC_SOUNDTOUCH_OUT>/lib/SoundTouch.lib   静态库, /MT 运行时, float 采样
#
# 源码仓(不在 avox 内, 保持上游原样):
#   git clone https://codeberg.org/soundtouch/soundtouch.git <某目录>
#   git checkout 2.4.1                (2026-09 时点最新 release; codeberg 直连偶发
#                                     schannel 断连, 重试即可; 备选 SF git:
#                                     https://git.code.sf.net/p/soundtouch/code)
#
# 用法(任意目录):
#   python build_windows.py                       # 源码目录默认 ../../soundtouch(与 avox 平级)
#   python build_windows.py --src D:/Work/github/soundtouch
#   python build_windows.py --clean              # 清空源码仓内 build/ 重编
#
# 说明: 纯 C++(C++17), 无外部依赖, SIMD 自动选 SSE/NEON; LGPL-2.1;
#       /MT 与 avox 主仓静态 CRT 一致(avox_tempo.dll 也是 /MT)。
#       float 采样(SOUNDTOUCH_FLOAT_SAMPLES, 上游默认); soundstretch 工具与 DLL 不编。
import argparse
import os
import shutil
import subprocess
import sys

AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# 源码目录默认与 avox 平级; 输出进 avox_library 库仓(与 avox 平级)
DEFAULT_SRC = os.path.join(os.path.dirname(AVOX_ROOT), "soundtouch")
DEFAULT_OUT = os.path.join(os.path.dirname(AVOX_ROOT), "avox_library",
                           "3rdparty", "library", "windows", "soundtouch")
OUT_DIR = os.environ.get("AVC_SOUNDTOUCH_OUT", DEFAULT_OUT)


def run(cmd):
    print("+ " + " ".join(cmd))
    ret = subprocess.call(cmd)
    if ret != 0:
        print("FAILED:", " ".join(cmd))
        sys.exit(ret)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default=DEFAULT_SRC, help="soundtouch 源码目录(默认 %s)" % DEFAULT_SRC)
    parser.add_argument("--clean", action="store_true", help="清空源码仓内 build 目录重编")
    args = parser.parse_args()

    src_dir = os.path.abspath(args.src)
    if not os.path.isfile(os.path.join(src_dir, "CMakeLists.txt")):
        sys.exit(f"源码目录无效(缺 CMakeLists.txt): {src_dir}\n"
                 f"clone https://codeberg.org/soundtouch/soundtouch.git 后 checkout 2.4.1, 再用 --src 指定")
    build_dir = os.path.join(src_dir, "build")
    if args.clean and os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    configure = [
        "cmake", "-S", src_dir, "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Release",
        # 静态库 + /MT, 与 avox 主仓/插件静态 CRT 一致
        "-DBUILD_SHARED_LIBS=OFF",
        "-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG /fp:fast",
        # 关闭无关组件(soundstretch 命令行工具 / C wrapper DLL)
        "-DSOUNDSTRETCH=OFF",
        "-DSOUNDTOUCH_DLL=OFF",
        # 上游 cmake_minimum_required(VERSION 3.5), CMake>=4 需放行; 旧版 CMake 忽略之
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        # 安装前缀 = 库仓
        "-DCMAKE_INSTALL_PREFIX=" + OUT_DIR,
    ]
    run(configure)
    run(["cmake", "--build", build_dir, "--config", "Release"])
    run(["cmake", "--install", build_dir, "--config", "Release"])

    lib = os.path.join(OUT_DIR, "lib", "SoundTouch.lib")
    inc = os.path.join(OUT_DIR, "include", "soundtouch", "SoundTouch.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        print("FAILED: 产物不完整, 期待", lib, "和", inc)
        sys.exit(1)
    print("OK:", OUT_DIR)


if __name__ == "__main__":
    main()
