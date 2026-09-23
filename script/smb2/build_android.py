# libsmb2 Android 预编译库构建脚本(与 build_windows.py 同源, 目标平台 android)
#
# 产物布局(FindLibsmb2.cmake android 分支约定: android/libsmb2/<abi>):
#   <OUT>/<abi>/include/smb2/  头文件
#   <OUT>/<abi>/lib/libsmb2.a  静态库
#
# 源码仓(不在 avox 内, 保持上游原样):
#   git clone https://github.com/sahlberg/libsmb2.git <某目录>
#   git checkout libsmb2-6.2          (2026-09 时点最新 release)
#
# 用法(任意目录):
#   python build_android.py                       # 源码目录默认 ../../libsmb2(与 avox 平级), arm64-v8a
#   python build_android.py --src D:/Work/github/libsmb2
#   python build_android.py --abi armeabi-v7a     # 32 位
#
# 说明: 纯 C, 无外部依赖(加密走内置实现), LGPL; 静态库自包含,
#       socket API 由 NDK sysroot 提供(ANDROID_PLATFORM=android-24 起)。
import argparse
import os
import shutil
import subprocess
import sys

AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SRC = os.path.join(os.path.dirname(AVOX_ROOT), "libsmb2")
DEFAULT_OUT = os.path.join(os.path.dirname(AVOX_ROOT), "avox_library",
                           "3rdparty", "library", "android", "libsmb2")
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
    parser.add_argument("--src", default=DEFAULT_SRC, help="libsmb2 源码目录(默认 %s)" % DEFAULT_SRC)
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
                 f"clone https://github.com/sahlberg/libsmb2.git 后 checkout libsmb2-6.2, 再用 --src 指定")
    out_dir = os.path.join(os.environ.get("AVC_LIBSMB2_OUT", DEFAULT_OUT), args.abi)
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
        # 静态库自包含, 与 avox android 产物(/MT 等价 c++_static 语境)一致
        "-DBUILD_SHARED_LIBS=OFF",
        "-DENABLE_EXAMPLES=OFF",
        "-DENABLE_GSSAPI=OFF",
        "-DCMAKE_INSTALL_PREFIX=" + out_dir,
    ]
    run(configure)
    run(["cmake", "--build", build_dir])
    run(["cmake", "--install", build_dir])

    lib = os.path.join(out_dir, "lib", "libsmb2.a")
    inc = os.path.join(out_dir, "include", "smb2", "libsmb2.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        print("FAILED: 产物不完整, 期待", lib, "和", inc)
        sys.exit(1)
    print("OK:", out_dir)


if __name__ == "__main__":
    main()
