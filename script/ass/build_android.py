# ASS 字幕栈 Android 预编译(aarch64) — 全静态 .a, 供 avox_ass 插件 android
# STATIC 模式连同插件源码链进 libavox.so(设计见 doc/plan/player/ASS字幕渲染计划.md §3.8)
#
# 产物: <avc_library>/3rdparty/library/android/ass/{include,lib}
#   libass.a + libfribidi.a + libharfbuzz.a + libfreetype.a + 各自 .pc/头文件
#
# 关键决策(对齐 §3.8):
#   - freetype 用 avox 核心 3rdparty/freetype 源码直接构建(当前 2.14.1),
#     与核心 avox_freetype 同版本同产物, 规避同符号双版本 ODR;
#   - fribidi LGPL 静态链: 记再链义务, 上架前复核 third-party-notices;
#   - meson 交叉: NDK clang wrapper + cross file, libass 用 --pkg-config-path
#     (PKG_CONFIG_PATH)指目标前缀; pkgconf 宿主工具与 windows 脚本同源复用。
#
# 用法: python script/ass/build_android.py [--abi arm64-v8a|armeabi-v7a]
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

TARGET = "android"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AVC_LIBRARY = (os.environ.get("ASS_DEPS_AVC_LIBRARY")
               or os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
               or os.path.join(os.path.dirname(ROOT), "avc_library"))
PREFIX = os.environ.get(
    "ASS_DEPS_PREFIX", os.path.join(AVC_LIBRARY, "3rdparty", "library", TARGET, "ass"))
# 与 windows 脚本共享源码缓存(dl/src), 减少重复下载
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")
DL = os.path.join(CACHE, "dl")
SRC = os.path.join(CACHE, "src")
TOOLS = os.path.join(CACHE, "tools")
BUILD_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")

DEFAULT_NDK = os.path.join(os.environ.get("ANDROID_SDK_ROOT",
                                          r"C:\Users\mfjt5\AppData\Local\Android\Sdk"),
                           "ndk", "26.1.10909125")
PKGCONF_URL = "https://github.com/pkgconf/pkgconf/archive/refs/tags/pkgconf-2.3.0.tar.gz"

DEPS = [
    # (name, url or None, build system); freetype 用核心源码(url=None)
    ("fribidi", "https://github.com/fribidi/fribidi/archive/refs/tags/v1.0.16.tar.gz", "meson"),
    ("harfbuzz", "https://github.com/harfbuzz/harfbuzz/archive/refs/tags/10.2.0.tar.gz", "cmake"),
    ("libass", "https://github.com/libass/libass/archive/refs/tags/0.17.3.tar.gz", "meson"),
]

MESON = [sys.executable, "-m", "mesonbuild.mesonmain"]


def run(cmd, env=None):
    print("+ " + " ".join(cmd), flush=True)
    e = dict(os.environ)
    if env:
        e.update(env)
    if subprocess.run(cmd, env=e).returncode != 0:
        sys.exit(f"命令失败: {' '.join(cmd)}")


def find_exe(name):
    p = shutil.which(name)
    if p and p.lower().endswith(".exe"):
        return p
    for d in os.environ.get("PATH", "").split(os.pathsep):
        c = os.path.join(d, name + ".exe")
        if os.path.isfile(c):
            return c
    return None


def find_ninja():
    exe = find_exe("ninja")
    if exe:
        return exe
    try:
        import ninja
        cand = os.path.join(ninja.BIN_DIR, "ninja.exe")
        if os.path.isfile(cand):
            return cand
    except ImportError:
        pass
    cand = os.path.join(os.environ.get("ANDROID_SDK_ROOT",
                                       r"C:\Users\mfjt5\AppData\Local\Android\Sdk"),
                        "cmake", "3.22.1", "bin", "ninja.exe")
    if os.path.isfile(cand):
        return cand
    sys.exit("缺少 ninja.exe")


def vcvars_path():
    v = os.environ.get("ASS_DEPS_VCVARS")
    if v:
        return v
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    ret = subprocess.run([vswhere, "-latest", "-products", "*",
                          "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                          "-property", "installationPath"], capture_output=True, text=True)
    vs = ret.stdout.strip().splitlines()[0] if ret.stdout.strip() else ""
    if not vs:
        sys.exit("找不到 Visual Studio(VC 工具集); 可用 ASS_DEPS_VCVARS 指定 vcvars64.bat")
    return os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars64.bat")


def run_vcvars(args, env_extra=None):
    """宿主工具(pkgconf)构建走 MSVC 环境; 临时 .cmd 批处理绕开引号二次转义"""
    lines = ["@echo off", f'call "{vcvars_path()}" >nul 2>&1']
    if env_extra:
        lines += [f"set {k}={v}" for k, v in env_extra.items()]
    quoted = " ".join(f'"{a}"' if (" " in a or "\\" in a) else a for a in args)
    lines.append(quoted)
    bat = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_step.cmd")
    with open(bat, "w", encoding="utf-8") as f:
        f.write("\r\n".join(lines) + "\r\n")
    print("+ bat:", quoted, flush=True)
    if subprocess.run(["cmd", "/c", bat]).returncode != 0:
        sys.exit(f"命令失败(exit): {quoted}")


def download(url, dest):
    if os.path.isfile(dest) and os.path.getsize(dest) > 4096:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    for _ in range(3):
        try:
            print("下载", url, flush=True)
            urllib.request.urlretrieve(url, dest)
            if os.path.getsize(dest) > 4096:
                return
        except Exception as e:
            print(f"urllib 失败({e}), 改用 curl...", flush=True)
        if subprocess.run(["curl", "-sL", "--retry", "3", "-o", dest, url]).returncode == 0 \
                and os.path.isfile(dest) and os.path.getsize(dest) > 4096:
            return
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


def ensure_meson():
    probe = subprocess.run([*MESON, "--version"], capture_output=True, text=True)
    if probe.returncode != 0 or not probe.stdout.strip():
        run([sys.executable, "-m", "pip", "install", "--quiet",
             "-i", "https://pypi.tuna.tsinghua.edu.cn/simple", "meson", "ninja"])
    print("meson:", subprocess.run([*MESON, "--version"], capture_output=True,
                                   text=True).stdout.strip(), flush=True)


def ensure_pkg_config():
    """pkg-config 宿主二进制: 优先系统, 否则 pkgconf 源码自编(纯 C + meson, MSVC)"""
    pc = find_exe("pkg-config")
    if pc:
        ret = subprocess.run([pc, "--version"], capture_output=True, text=True)
        if ret.returncode == 0 and ret.stdout.strip():
            print("pkg-config:", ret.stdout.strip(), flush=True)
            return pc
    tools_bin = os.path.join(TOOLS, "bin")
    exe = os.path.join(tools_bin, "pkgconf.exe")
    if not os.path.isfile(exe):
        print("自编 pkgconf(宿主工具, MSVC)...", flush=True)
        tgz = os.path.join(DL, "pkgconf.tar.gz")
        download(PKGCONF_URL, tgz)
        psrc = os.path.join(SRC, "pkgconf")
        if not os.path.isdir(psrc):
            os.makedirs(SRC, exist_ok=True)
            with tarfile.open(tgz) as t:
                t.extractall(SRC)
            top = [e for e in os.listdir(SRC)
                   if e.startswith("pkgconf") and os.path.isdir(os.path.join(SRC, e))][0]
            shutil.move(os.path.join(SRC, top), psrc)
        pb = os.path.join(BUILD_ROOT, "pkgconf")
        if os.path.isdir(pb):
            shutil.rmtree(pb)
        run_vcvars([*MESON, "setup", pb, psrc, "--prefix", TOOLS,
                    "--buildtype", "release",
                    "-Ddefault_library=static", "-Dtests=disabled"])
        run_vcvars([*MESON, "install", "-C", pb])
    # 无条件暴露成 pkg-config 名(meson 依赖查找认这个名字)
    shutil.copy(exe, os.path.join(tools_bin, "pkg-config.exe"))
    os.environ["PATH"] = tools_bin + os.pathsep + os.environ["PATH"]
    ret = subprocess.run(["pkg-config", "--version"], capture_output=True, text=True)
    print("pkg-config(自编):", ret.stdout.strip(), flush=True)
    return os.path.join(tools_bin, "pkg-config.exe")


def write_cross_file(path, ndk, abi, api, pkgconf):
    """NDK clang wrapper 自带 --target=<abi>-linux-android<api>, 无需额外 flags。
    路径统一正斜杠 —— meson 交叉模式下 prefix/工具路径不认反斜杠盘符。"""
    triple = {"arm64-v8a": "aarch64-linux-android",
              "armeabi-v7a": "armv7a-linux-androideabi"}[abi]
    cpu_family = "aarch64" if abi == "arm64-v8a" else "arm"
    fwd = lambda p: p.replace("\\", "/")
    bin_ = fwd(os.path.join(ndk, "toolchains", "llvm", "prebuilt", "windows-x86_64", "bin"))
    content = f"""[binaries]
c = '{bin_}/{triple}{api}-clang.cmd'
cpp = '{bin_}/{triple}{api}-clang++.cmd'
ar = '{bin_}/llvm-ar.exe'
strip = '{bin_}/llvm-strip.exe'
pkg-config = '{fwd(pkgconf)}'

[host_machine]
system = 'android'
cpu_family = '{cpu_family}'
cpu = '{abi}'
endian = 'little'
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return path


def write_native_file(path, pkgconf):
    """gen.tab 等构建期宿主工具用 MSVC 编(build 主机 = Windows);
    meson setup/compile 都要在 vcvars 环境里跑, cl 才有 INCLUDE/LIB。
    shim/: MSVC 缺的 POSIX 头兜底(strings.h 等), 经 c_args 前置 include"""
    fwd = lambda p: p.replace("\\", "/")
    shim = os.path.join(os.path.dirname(path), "shim")
    os.makedirs(shim, exist_ok=True)
    with open(os.path.join(shim, "strings.h"), "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <string.h>\n")
    content = f"""[binaries]
c = 'cl'
cpp = 'cl'
pkg-config = '{fwd(pkgconf)}'

[built-in options]
c_args = ['/I{fwd(shim)}']
cpp_args = ['/I{fwd(shim)}']
"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return path


def meson_setup(build, src, cross, native, extra=None, env=None):
    """交叉 android 时 meson 按 POSIX 语义校验 prefix(盘符路径报 not absolute),
    惯用解法: configure 用 --prefix=/, install 时 --destdir 落到真实目录,
    依赖解析用 PKG_CONFIG_SYSROOT_DIR 重定位 .pc 内的 / 前缀路径。
    宿主侧(gen.tab)走 native file 的 cl, 所以 setup 在 vcvars 里执行。"""
    if os.path.isdir(build):
        shutil.rmtree(build)
    fwd = lambda p: p.replace("\\", "/")
    args = [*MESON, "setup", fwd(build), fwd(src),
            "--prefix", "/",
            "--buildtype", "release",
            "-Ddefault_library=static",
            f"--cross-file={fwd(cross)}",
            f"--native-file={fwd(native)}"]
    if extra:
        args += extra
    run_vcvars(args, env_extra=env)


def meson_install(build, env=None):
    fwd = lambda p: p.replace("\\", "/")
    run_vcvars([*MESON, "compile", "-C", fwd(build)], env_extra=env)
    run_vcvars([*MESON, "install", "-C", fwd(build), "--destdir", fwd(PREFIX)],
               env_extra=env)


def cmake_dep(name, src, toolchain, abi, api, extra):
    b = os.path.join(BUILD_ROOT, f"{name}-android-{abi}")
    if os.path.isdir(b):
        shutil.rmtree(b)
    run(["cmake", "-S", src, "-B", b, "-G", "Ninja",
         f"-DCMAKE_MAKE_PROGRAM={find_ninja()}",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
         # ANDROID_ABI 必须显式给: 缺省是 armeabi-v7a, 会混出 32 位产物
         f"-DANDROID_ABI={abi}",
         f"-DANDROID_PLATFORM=android-{api}",
         f"-DCMAKE_INSTALL_PREFIX={PREFIX}",
         "-DCMAKE_BUILD_TYPE=Release",
         "-DBUILD_SHARED_LIBS=OFF",
         *extra])
    run(["cmake", "--build", b])
    run(["cmake", "--install", b])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--abi", default="arm64-v8a", choices=["arm64-v8a", "armeabi-v7a"])
    parser.add_argument("--api", default="24")
    parser.add_argument("--ndk", default=DEFAULT_NDK)
    args = parser.parse_args()

    toolchain = os.path.join(args.ndk, "build", "cmake", "android.toolchain.cmake")
    if not os.path.isfile(toolchain):
        sys.exit(f"NDK toolchain 不存在: {toolchain}")
    if args.abi == "armeabi-v7a":
        sys.exit("armeabi-v7a 暂未启用(avox android 产物仅 aarch64), 需要时去掉此限制")

    ensure_meson()
    pkgconf = ensure_pkg_config()
    os.makedirs(PREFIX, exist_ok=True)
    cross = os.path.join(BUILD_ROOT, f"cross-{args.abi}.ini")
    native = os.path.join(BUILD_ROOT, "native.ini")
    os.makedirs(BUILD_ROOT, exist_ok=True)
    write_cross_file(cross, args.ndk, args.abi, args.api, pkgconf)
    write_native_file(native, pkgconf)
    pc_dir = os.path.join(PREFIX, "lib", "pkgconfig")

    # 1. fribidi(meson 交叉, 静态; -Ddocs=false 免 c2man, LGPL 静态再链义务见文件头)
    fb_src = fetch("fribidi", DEPS[0][1])
    fb_build = os.path.join(BUILD_ROOT, "fribidi-android")
    meson_setup(fb_build, fb_src, cross, native, extra=["-Ddocs=false"])
    meson_install(fb_build)

    # 2. freetype(CMake 交叉, 静态) —— 用核心 3rdparty/freetype 源码(§3.8 防双版本 ODR)
    ft_src = os.path.join(ROOT, "3rdparty", "freetype")
    cmake_dep("freetype", ft_src, toolchain, args.abi, args.api,
              ["-DFT_DISABLE_ZLIB=TRUE", "-DFT_DISABLE_BZIP2=TRUE",
               "-DFT_DISABLE_PNG=TRUE", "-DFT_DISABLE_HARFBUZZ=TRUE",
               "-DFT_DISABLE_BROTLI=TRUE"])

    # 3. harfbuzz(CMake 交叉, 静态; 全仓仅 ass 栈使用, 无冲突)
    hb_src = fetch("harfbuzz", DEPS[1][1])
    cmake_dep("harfbuzz", hb_src, toolchain, args.abi, args.api,
              ["-DHB_BUILD_SUBSET=OFF", "-DHB_BUILD_UTILS=OFF",
               "-DHB_BUILD_TESTS=OFF", "-DHB_HAVE_GLIB=OFF",
               "-DHB_HAVE_FREETYPE=OFF"])

    # 4. libass(meson 交叉, 静态; 经 pkg-config 找三件套: fribidi 的 .pc prefix=/,
    # 靠 PKG_CONFIG_SYSROOT_DIR 重定位到真实前缀; freetype 的 .pc 是真实路径无需处理)
    la_src = fetch("libass", DEPS[2][1])
    la_build = os.path.join(BUILD_ROOT, "libass-android")
    fwd = lambda p: p.replace("\\", "/")
    la_env = {"PKG_CONFIG_PATH": fwd(pc_dir), "PKG_CONFIG_SYSROOT_DIR": fwd(PREFIX)}
    # Android 无系统字体提供器(§3.8): 关强制要求, avox 侧 setFontsDir 兜底
    meson_setup(la_build, la_src, cross, native,
                extra=["-Drequire-system-font-provider=false"], env=la_env)
    meson_install(la_build, env=la_env)

    print(f"\n产物: {PREFIX}")
    lib = os.path.join(PREFIX, "lib")
    print("  lib/:", ", ".join(sorted(os.listdir(lib))), flush=True)
    for want in ("libass.a", "libfribidi.a", "libharfbuzz.a", "libfreetype.a"):
        if not os.path.isfile(os.path.join(lib, want)):
            sys.exit(f"FAILED: 产物缺 {want}")
    print("OK")


if __name__ == "__main__":
    main()
