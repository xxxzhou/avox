import os
import shutil
import subprocess
import sys

# libsmb2 macOS arm64 / iOS arm64 静态库构建(与 build_windows.py 同源, libsmb2-6.2)
# 纯 C, 无外部依赖(加密走内置实现), LGPL
# 产物布局(与 windows/libsmb2 同构):
#   <avox_library>/3rdparty/library/<darwin|ios>/libsmb2/{include/smb2, lib/libsmb2.a}
#
# 源码仓不在 avox 内(上游保持原样): 脚本自动 shallow clone libsmb2-6.2 到 _cache;
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
AVC_LIBRARY = (os.environ.get("ASS_DEPS_AVC_LIBRARY")
               or os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
               or os.path.join(os.path.dirname(ROOT), "avox_library"))
OUT_DIR = os.path.join(AVC_LIBRARY, "3rdparty", "library", LIB_PLATFORM, "libsmb2")
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")
SRC_DEFAULT = os.path.join(CACHE, "libsmb2")

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
    run(["git", "clone", "--depth", "1", "--branch", "libsmb2-6.2",
         "https://github.com/sahlberg/libsmb2.git", src])
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
            "-DCMAKE_INSTALL_PREFIX=" + OUT_DIR,
            "-DENABLE_EXAMPLES=OFF",
            "-DENABLE_GSSAPI=OFF",
            # 非 Windows 分支默认 ON 的 Kerberos 支持会引入系统 GSS/krb5 依赖,
            # 插件链接就要 -framework GSS; SMB 认证走 NTLM 内置实现即可
            "-DENABLE_LIBKRB5=OFF"]
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

    lib = os.path.join(OUT_DIR, "lib", "libsmb2.a")
    inc = os.path.join(OUT_DIR, "include", "smb2", "libsmb2.h")
    if not (os.path.isfile(lib) and os.path.isfile(inc)):
        sys.exit(f"产物不完整, 期待 {lib} 和 {inc}")
    print("OK:", OUT_DIR)


if __name__ == "__main__":
    main()
