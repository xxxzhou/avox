import os
import re
import shutil
import subprocess
import sys
import tarfile

# ASS 字幕栈预编译(fribidi/freetype/harfbuzz/libass) — macOS arm64 / iOS arm64
# 与 build_windows.py 同源同版本 pin, 差异:
#   - 四库全部静态: 产物为自包含 libass.a(内嵌 fribidi/freetype/harfbuzz),
#     对齐 plugins/avox_ass/CMakeLists.txt 非 Windows 分支"静态 .a 自包含"的约定
#   - iOS 走 meson cross file + CMAKE_SYSTEM_NAME=iOS 交叉, arm64 真机 SDK
#   - fontconfig 显式关(走 CoreText, 免 host brew 字体库混入)
# 产物: <avox_library>/3rdparty/library/<darwin|ios>/ass/{include/ass, lib/libass.a}
# (布局对齐 windows/ass 的 include+lib; 依赖许可见 build_windows.py 头注:
#  libass ISC / harfbuzz MIT / freetype FTL / fribidi LGPL-2.1+ 静态内嵌)
#
# 用法:
#   python3 build_apple.py macos
#   python3 build_apple.py ios
# 前置: meson + ninja + pkg-config (brew install meson pkg-config)

TARGET = sys.argv[1] if len(sys.argv) > 1 else "macos"
if TARGET not in ("macos", "ios"):
    sys.exit("用法: python3 build_apple.py macos|ios")

# 库仓平台目录: macOS 用 darwin(与 ffmpeg 静态库目录一致), iOS 用 ios
LIB_PLATFORM = "darwin" if TARGET == "macos" else "ios"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AVC_LIBRARY = (os.environ.get("ASS_DEPS_AVC_LIBRARY")
               or os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
               or os.path.join(os.path.dirname(ROOT), "avox_library"))
PREFIX = os.path.join(AVC_LIBRARY, "3rdparty", "library", LIB_PLATFORM, "ass")
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")
DL = os.path.join(CACHE, "dl")
SRC = os.path.join(CACHE, "src")
BUILD_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", TARGET)

MESON = ["/opt/homebrew/bin/meson"]

IOS_MIN = "13.0"
MACOS_MIN = "11.0"

DEPS = [
    ("fribidi", "https://github.com/fribidi/fribidi/archive/refs/tags/v1.0.16.tar.gz", "meson"),
    ("freetype", "https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz", "cmake"),
    ("harfbuzz", "https://github.com/harfbuzz/harfbuzz/archive/refs/tags/10.2.0.tar.gz", "cmake"),
    ("libass", "https://github.com/libass/libass/archive/refs/tags/0.17.3.tar.gz", "meson"),
]


def run(cmd, env=None):
    print("+", " ".join(cmd), flush=True)
    ret = subprocess.run(cmd, env=env).returncode
    if ret != 0:
        sys.exit(f"命令失败(exit {ret}): {' '.join(cmd)}")


def download(url, dest):
    if os.path.isfile(dest) and os.path.getsize(dest) > 4096:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    run(["curl", "-sL", "--retry", "3", "-o", dest, url])
    if not (os.path.isfile(dest) and os.path.getsize(dest) > 4096):
        sys.exit(f"下载失败: {url}")


def fetch(name, url):
    tgz = os.path.join(DL, f"{name}.tar.gz")
    download(url, tgz)
    dest = os.path.join(SRC, name)
    if not os.path.isdir(dest):
        os.makedirs(SRC, exist_ok=True)
        with tarfile.open(tgz) as t:
            t.extractall(SRC)
        top = [e for e in os.listdir(SRC)
               if e.startswith(name) and os.path.isdir(os.path.join(SRC, e))][0]
        shutil.move(os.path.join(SRC, top), dest)
    return dest


def ios_sdk_path():
    ret = subprocess.run(["xcrun", "-sdk", "iphoneos", "--show-sdk-path"],
                         capture_output=True, text=True)
    return ret.stdout.strip()


def meson_cross_file():
    """iOS 交叉文件: clang 直接指定 -isysroot/-arch, pkg-config 用本机二进制"""
    sdk = ios_sdk_path()
    if not os.path.isdir(sdk):
        sys.exit(f"iphoneos SDK 不存在: {sdk}")
    args = ["-arch", "arm64", "-isysroot", sdk, f"-miphoneos-version-min={IOS_MIN}"]
    path = os.path.join(CACHE, "ios-cross.ini")
    with open(path, "w") as f:
        f.write("[binaries]\n")
        f.write("c = '/usr/bin/clang'\n")
        f.write("cpp = '/usr/bin/clang++'\n")
        f.write("ar = '/usr/bin/ar'\n")
        f.write("strip = '/usr/bin/strip'\n")
        f.write("pkg-config = '/opt/homebrew/bin/pkg-config'\n")
        f.write("\n[built-in options]\n")
        f.write("c_args = [" + ", ".join(f"'{a}'" for a in args) + "]\n")
        f.write("\n[host_machine]\n")
        f.write("system = 'darwin'\n")
        f.write("cpu_family = 'aarch64'\n")
        f.write("cpu = 'aarch64'\n")
        f.write("endian = 'little'\n")
    return path


def meson_setup(name, src, extra=None, cross=None, pkg_path=None):
    b = os.path.join(BUILD_ROOT, name)
    if os.path.isdir(b):
        shutil.rmtree(b)
    args = [*MESON, "setup", b, src,
            "--prefix", PREFIX,
            "--buildtype", "release",
            "-Ddefault_library=static"]
    if extra:
        args += extra
    if cross:
        args += [f"--cross-file={cross}"]
    env = dict(os.environ)
    if pkg_path:
        env["PKG_CONFIG_PATH"] = pkg_path
    run(args, env=env)
    run([*MESON, "install", "-C", b], env=env)


def cmake_dep(name, src, extra):
    b = os.path.join(BUILD_ROOT, name)
    if os.path.isdir(b):
        shutil.rmtree(b)
    base = ["cmake", "-S", src, "-B", b,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_INSTALL_PREFIX=" + PREFIX,
            "-DBUILD_SHARED_LIBS=OFF"]
    if TARGET == "ios":
        base += ["-DCMAKE_SYSTEM_NAME=iOS",
                 "-DCMAKE_OSX_SYSROOT=iphoneos",
                 "-DCMAKE_OSX_ARCHITECTURES=arm64",
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={IOS_MIN}"]
    else:
        base += ["-DCMAKE_OSX_ARCHITECTURES=arm64",
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MIN}"]
    run(base + extra)
    run(["cmake", "--build", b, "--config", "Release", "-j",
         str(os.cpu_count() or 4)])
    run(["cmake", "--install", b, "--config", "Release"])


def merge_static():
    """meson/cmake 的静态库不吸收依赖 .a, 用 libtool 把
    fribidi/freetype/harfbuzz 并进 libass.a 成自包含单文件(插件只链 ass)"""
    lib = os.path.join(PREFIX, "lib")
    merged = os.path.join(BUILD_ROOT, "libass-merged.a")
    deps = [os.path.join(lib, n) for n in
            ("libfribidi.a", "libfreetype.a", "libharfbuzz.a")]
    for d in deps:
        if not os.path.isfile(d):
            sys.exit(f"依赖归档缺失, 无法合并: {d}")
    run(["libtool", "-static", "-o", merged,
         os.path.join(lib, "libass.a")] + deps)
    shutil.move(merged, os.path.join(lib, "libass.a"))


def prune():
    """产品只留 include/ass + lib/libass.a(对齐 windows/ass 布局; 依赖已内嵌)"""
    lib = os.path.join(PREFIX, "lib")
    for f in os.listdir(lib):
        p = os.path.join(lib, f)
        if f != "libass.a" and os.path.isfile(p):
            os.remove(p)
        elif os.path.isdir(p):
            shutil.rmtree(p)
    inc = os.path.join(PREFIX, "include")
    for d in os.listdir(inc):
        if d != "ass":
            shutil.rmtree(os.path.join(inc, d), ignore_errors=True)
    for sub in ("bin", "share"):
        shutil.rmtree(os.path.join(PREFIX, sub), ignore_errors=True)


def main():
    os.makedirs(PREFIX, exist_ok=True)
    srcs = {name: fetch(name, url) for name, url, _ in DEPS}
    cross = meson_cross_file() if TARGET == "ios" else None
    pc_dir = os.path.join(PREFIX, "lib", "pkgconfig")

    # 1. fribidi(静态; -Ddocs=false 免 c2man 文档工具链, -Dbin=false 免 CLI
    #    工具 —— iOS 交叉时工具没法在 host 链, 产物目录也用不到)
    meson_setup("fribidi", srcs["fribidi"],
                extra=["-Ddocs=false", "-Dbin=false", "-Dtests=false"],
                cross=cross)

    # 2. freetype(静态, 关 zlib/png/bzip2/brotli/harfbuzz 集成, 同 windows)
    cmake_dep("freetype", srcs["freetype"],
              ["-DFT_DISABLE_ZLIB=TRUE", "-DFT_DISABLE_BZIP2=TRUE",
               "-DFT_DISABLE_PNG=TRUE", "-DFT_DISABLE_HARFBUZZ=TRUE",
               "-DFT_DISABLE_BROTLI=TRUE"])

    # 3. harfbuzz(静态, 同 windows 全关外围)
    cmake_dep("harfbuzz", srcs["harfbuzz"],
              ["-DHB_BUILD_SUBSET=OFF", "-DHB_BUILD_UTILS=OFF",
               "-DHB_BUILD_TESTS=OFF", "-DHB_HAVE_GLIB=OFF",
               "-DHB_HAVE_FREETYPE=OFF"])

    # 4. libass(静态; CoreText 后端, fontconfig 显式关防 host 库混入;
    #    harfbuzz/fribidi 在 0.17.x 是必选依赖, 非可选 feature)
    meson_setup("libass", srcs["libass"],
                extra=["-Dtest=false", "-Dfontconfig=disabled",
                       "-Dcoretext=enabled"],
                cross=cross, pkg_path=pc_dir)

    merge_static()
    prune()
    print(f"\n产物: {PREFIX}")
    for sub in ("include/ass", "lib"):
        d = os.path.join(PREFIX, sub)
        print(f"  {sub}/:", ", ".join(sorted(os.listdir(d))[:12]), flush=True)
    ret = subprocess.run(["nm", "-g", os.path.join(PREFIX, "lib", "libass.a")],
                         capture_output=True, text=True)
    if "ass_library_init" not in ret.stdout:
        sys.exit("自检失败: libass.a 无 ass_library_init 符号")
    ret = subprocess.run(["nm", "-u", os.path.join(PREFIX, "lib", "libass.a")],
                         capture_output=True, text=True)
    leak = [l.strip() for l in ret.stdout.splitlines()
            if re.match(r"^_(FT|hb_|fribidi)", l.strip())]
    if leak:
        sys.exit(f"自包含校验失败, 仍有未吸收依赖符号: {leak[:5]}")
    print("自检 OK: ass_library_init 存在, 依赖符号零外泄", flush=True)


if __name__ == "__main__":
    main()
