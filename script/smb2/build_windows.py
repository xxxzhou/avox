# libsmb2 Windows 预编译库构建脚本(avox_remote 插件 smb 源的预编译依赖源)
#
# 产物布局(与 libtorrent 同级):
#   <AVC_LIBSMB2_OUT>/include/smb2/   头文件
#   <AVC_LIBSMB2_OUT>/lib/smb2.lib    静态库, /MT 运行时
#
# 源码仓(不在 avox 内, 保持上游原样):
#   git clone https://github.com/sahlberg/libsmb2.git <某目录>
#   git checkout libsmb2-6.2          (2026-09 时点最新 release)
#
# 用法(任意目录):
#   python build_windows.py                       # 源码目录默认 ../../libsmb2(与 avox 平级)
#   python build_windows.py --src D:/Work/github/libsmb2
#   python build_windows.py --clean              # 清空源码仓内 build/ 重编
#
# 说明: 纯 C, 无外部依赖(winsock 自足, 加密走内置实现), LGPL;
#       /MT 与 avox 主仓静态 CRT 一致(avox_remote.dll 也是 /MT)。
import argparse
import os
import shutil
import subprocess
import sys

AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# 源码目录默认与 avox 平级; 输出进 avc_library 库仓(与 avox 平级)
DEFAULT_SRC = os.path.join(os.path.dirname(AVOX_ROOT), "libsmb2")
DEFAULT_OUT = os.path.join(os.path.dirname(AVOX_ROOT), "avc_library",
                           "3rdparty", "library", "windows", "libsmb2")
OUT_DIR = os.environ.get("AVC_LIBSMB2_OUT", DEFAULT_OUT)


def run(cmd):
    print("+ " + " ".join(cmd))
    ret = subprocess.call(cmd)
    if ret != 0:
        print("FAILED:", " ".join(cmd))
        sys.exit(ret)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default=DEFAULT_SRC, help="libsmb2 源码目录(默认 %s)" % DEFAULT_SRC)
    parser.add_argument("--clean", action="store_true", help="清空源码仓内 build 目录重编")
    args = parser.parse_args()

    src_dir = os.path.abspath(args.src)
    if not os.path.isfile(os.path.join(src_dir, "CMakeLists.txt")):
        sys.exit(f"源码目录无效(缺 CMakeLists.txt): {src_dir}\n"
                 f"clone https://github.com/sahlberg/libsmb2.git 后 checkout libsmb2-6.2, 再用 --src 指定")
    build_dir = os.path.join(src_dir, "build")
    if args.clean and os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    configure = [
        "cmake", "-S", src_dir, "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Release",
        # 静态库 + /MT, 与 avox 主仓/插件静态 CRT 一致
        "-DBUILD_SHARED_LIBS=OFF",
        '-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG',
        # 关闭无关组件(默认已 OFF, 显式声明防上游改动)
        "-DENABLE_EXAMPLES=OFF",
        "-DENABLE_GSSAPI=OFF",
        # 安装前缀 = 库仓
        "-DCMAKE_INSTALL_PREFIX=" + OUT_DIR,
    ]
    run(configure)
    run(["cmake", "--build", build_dir, "--config", "Release"])
    run(["cmake", "--install", build_dir, "--config", "Release"])

    lib = os.path.join(OUT_DIR, "lib", "smb2.lib")
    inc = os.path.join(OUT_DIR, "include", "smb2", "libsmb2.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        print("FAILED: 产物不完整, 期待", lib, "和", inc)
        sys.exit(1)
    print("OK:", OUT_DIR)


if __name__ == "__main__":
    main()
