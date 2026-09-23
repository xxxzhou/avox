import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

# ASS 字幕栈预编译(fribidi/freetype/harfbuzz/libass) — Windows x64
# 产物: < sibling >/avox_library/3rdparty/library/ass/windows/{bin,lib,include}
# (与 script/webrtc 同模式: 脚本在 avox, 大件产物进 avox_library 仓;
#  可用环境变量 ASS_DEPS_PREFIX 覆盖输出位置)
# avox_ass 插件构建时默认探测该目录(AVOX_ASS_DEPS_DIR 可覆盖), 见 plugins/avox_ass/CMakeLists.txt
#
# 依赖构建体系: freetype/harfbuzz = CMake(VS 生成器); fribidi/libass = meson(两库已无 CMakeLists);
# libass 经 pkg-config 找 prefix 里的三个 .pc —— pkg-config 二进制用 pkgconf 自源码编出
# (GitHub 直连, 免第三方镜像), 缓存在 _cache/。
#
# 版本 pin(升级 = 改这里 + 重新出产物):
#   libass 0.17.3 / fribidi v1.0.16 / harfbuzz 10.2.0 / freetype VER-2-13-3
# 许可: libass ISC / harfbuzz MIT / freetype FTL / fribidi LGPL-2.1+(故 Windows 出 dll 动态链接)
#
# 用法: python script/ass/build_windows.py

TARGET = "windows"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# avox_library 位置与根 CMake 同优先级: ASS_DEPS_AVC_LIBRARY > AVOX_EXTERNAL_LIBRARY_DIR > ../avox_library
AVC_LIBRARY = (os.environ.get("ASS_DEPS_AVC_LIBRARY")
               or os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
               or os.path.join(os.path.dirname(ROOT), "avox_library"))
# avox_library 惯例: 平台在前 → 3rdparty/library/<platform>/<lib>
PREFIX = os.environ.get(
    "ASS_DEPS_PREFIX", os.path.join(AVC_LIBRARY, "3rdparty", "library", TARGET, "ass"))
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_cache")
DL = os.path.join(CACHE, "dl")
SRC = os.path.join(CACHE, "src")
TOOLS = os.path.join(CACHE, "tools")

VS_GENERATOR = os.environ.get("ASS_DEPS_VS_GENERATOR", "Visual Studio 17 2022")
VS_ARCH = os.environ.get("ASS_DEPS_VS_ARCH", "x64")

PKGCONF_URL = "https://github.com/pkgconf/pkgconf/archive/refs/tags/pkgconf-2.3.0.tar.gz"

DEPS = [
    # (name, url, build system)
    ("fribidi", "https://github.com/fribidi/fribidi/archive/refs/tags/v1.0.16.tar.gz", "meson"),
    ("freetype", "https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz", "cmake"),
    ("harfbuzz", "https://github.com/harfbuzz/harfbuzz/archive/refs/tags/10.2.0.tar.gz", "cmake"),
    ("libass", "https://github.com/libass/libass/archive/refs/tags/0.17.3.tar.gz", "meson"),
]

MESON = [sys.executable, "-m", "mesonbuild.mesonmain"]


def run(cmd, env=None):
    print("+", " ".join(cmd), flush=True)
    ret = subprocess.run(cmd, env=env).returncode
    if ret != 0:
        sys.exit(f"命令失败(exit {ret}): {' '.join(cmd)}")


def pip_ensure(*pkgs):
    sh_ret = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", *pkgs])
    if sh_ret.returncode != 0:
        sys.exit(f"pip install {' '.join(pkgs)} 失败")


def find_exe(name):
    """按 PATHEXT 找不到时, 兜底扫各 PATH 目录下的 <name>.exe"""
    p = shutil.which(name)
    if p and p.lower().endswith(".exe"):
        return p
    for d in os.environ.get("PATH", "").split(os.pathsep):
        c = os.path.join(d, name + ".exe")
        if os.path.isfile(c):
            return c
    return None


def vcvars_path():
    v = os.environ.get("ASS_DEPS_VCVARS")
    if v:
        return v
    vswhere = rf"{os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')}\Microsoft Visual Studio\Installer\vswhere.exe"
    ret = subprocess.run(
        [vswhere, "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True)
    vs = ret.stdout.strip().splitlines()[0] if ret.stdout.strip() else ""
    if not vs:
        sys.exit("找不到 Visual Studio(VC 工具集); 可用 ASS_DEPS_VCVARS 指定 vcvars64.bat")
    return os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars64.bat")


def run_vcvars(args, env_extra=None):
    """在 vcvars64 环境里跑命令。写成临时 .cmd 批处理再执行 —— 绕开 cmd /c
    对内嵌引号的二次转义问题。meson 系必须走这里。"""
    ninja = find_exe("ninja")
    lines = ["@echo off", f'call "{vcvars_path()}" >nul 2>&1']
    if env_extra:
        lines += [f"set {k}={v}" for k, v in env_extra.items()]
    if ninja:
        lines.append(f"set PATH={os.path.dirname(ninja)};%PATH%")
    quoted = " ".join(f'"{a}"' if (" " in a or "\\" in a) else a for a in args)
    lines.append(quoted)
    bat = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_step.cmd")
    with open(bat, "w", encoding="utf-8") as f:
        f.write("\r\n".join(lines) + "\r\n")
    print("+ bat:", quoted, flush=True)
    ret = subprocess.run(["cmd", "/c", bat]).returncode
    if ret != 0:
        sys.exit(f"命令失败(exit {ret}): {quoted}")


def ensure_tools():
    probe = subprocess.run([*MESON, "--version"], capture_output=True, text=True)
    if probe.returncode != 0 or not probe.stdout.strip():
        pip_ensure("meson", "ninja")
    if not find_exe("ninja"):
        pip_ensure("ninja")
    if not find_exe("ninja"):
        sys.exit("缺少 ninja.exe")
    ver = subprocess.run([*MESON, "--version"], capture_output=True, text=True)
    print("meson:", ver.stdout.strip(), flush=True)


def ensure_pkg_config():
    """pkg-config 二进制: 优先系统, 否则 pkgconf 源码自编(纯 C + meson)"""
    pc = find_exe("pkg-config")
    if pc:
        ret = subprocess.run([pc, "--version"], capture_output=True, text=True)
        if ret.returncode == 0 and ret.stdout.strip():
            print("pkg-config:", ret.stdout.strip(), flush=True)
            return
    tools_bin = os.path.join(TOOLS, "bin")
    exe = os.path.join(tools_bin, "pkgconf.exe")
    if not os.path.isfile(exe):
        print("自编 pkgconf ...", flush=True)
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
        pb = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "pkgconf")
        if os.path.isdir(pb):
            shutil.rmtree(pb)
        run_vcvars([*MESON, "setup", pb, psrc, "--prefix", TOOLS,
                    "--buildtype", "release",
                    "-Ddefault_library=static", "-Dtests=disabled"])
        run_vcvars([*MESON, "install", "-C", pb])
    # 无条件暴露成 pkg-config 名(meson 找的就是这个名字; PATH 里可能存在
    # 其他来源的坏 shim, 本目录永远排最前)
    shutil.copy(exe, os.path.join(tools_bin, "pkg-config.exe"))
    os.environ["PATH"] = tools_bin + os.pathsep + os.environ["PATH"]
    ret = subprocess.run(["pkg-config", "--version"], capture_output=True, text=True)
    print("pkg-config(自编):", ret.stdout.strip(), flush=True)


def download(url, dest):
    """下载(带重试); urllib 被 SSL 打断时退回 curl"""
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
        ret = subprocess.run(["curl", "-sL", "--retry", "3", "-o", dest, url]).returncode
        if ret == 0 and os.path.isfile(dest) and os.path.getsize(dest) > 4096:
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


def meson_setup(build, src, pkg_path=None, extra=None):
    if os.path.isdir(build):
        shutil.rmtree(build)
    args = [*MESON, "setup", build, src,
            "--prefix", PREFIX,
            "--buildtype", "release",
            "-Ddefault_library=shared",
            "-Db_vscrt=mt"]
    if extra:
        args += extra
    env = {"PKG_CONFIG_PATH": pkg_path} if pkg_path else None
    run_vcvars(args, env_extra=env)


def meson_install(build):
    run_vcvars([*MESON, "compile", "-C", build])
    run_vcvars([*MESON, "install", "-C", build])


def cmake_dep(name, src, extra):
    b = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", name)
    run(["cmake", "-S", src, "-B", b,
         "-G", VS_GENERATOR, "-A", VS_ARCH,
         f"-DCMAKE_INSTALL_PREFIX={PREFIX}",
         "-DBUILD_SHARED_LIBS=ON",
         # CMP0091=NEW: 让 CMAKE_MSVC_RUNTIME_LIBRARY 真正生效
         # (harfbuzz 等老 min_required 工程默认 OLD, /MD 烧进 flags 致静态链 __imp_ 悬空)
         "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
         "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
         *extra])
    run(["cmake", "--build", b, "--config", "Release"])
    run(["cmake", "--install", b, "--config", "Release"])


def main():
    ensure_tools()
    ensure_pkg_config()
    os.makedirs(PREFIX, exist_ok=True)
    srcs = {name: fetch(name, url) for name, url, _ in DEPS}
    pc_dir = os.path.join(PREFIX, "lib", "pkgconfig")
    build_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")

    # 1. fribidi(meson, 出 dll —— LGPL 动态链接; -Ddocs=false 免 c2man)
    fb = os.path.join(build_root, "fribidi")
    meson_setup(fb, srcs["fribidi"], extra=["-Ddocs=false"])
    meson_install(fb)

    # 2. freetype(CMake, 静态链入 ass-9.dll: 关 zlib/png/bzip2/brotli/harfbuzz 集成。
    #    FT 无进程级全局状态, 但为避免与核心 avox_freetype 的静态 FT 共存产生
    #    双副本/版本漂移, 干脆静态链进 libass —— 许可 FTL 允许)
    cmake_dep("freetype", srcs["freetype"],
              ["-DBUILD_SHARED_LIBS=OFF",
               "-DFT_DISABLE_ZLIB=TRUE", "-DFT_DISABLE_BZIP2=TRUE",
               "-DFT_DISABLE_PNG=TRUE", "-DFT_DISABLE_HARFBUZZ=TRUE",
               "-DFT_DISABLE_BROTLI=TRUE"])

    # 3. harfbuzz(CMake, 同样静态链入 —— MIT 允许)
    cmake_dep("harfbuzz", srcs["harfbuzz"],
              ["-DBUILD_SHARED_LIBS=OFF",
               "-DHB_BUILD_SUBSET=OFF", "-DHB_BUILD_UTILS=OFF",
               "-DHB_BUILD_TESTS=OFF", "-DHB_HAVE_GLIB=OFF",
               "-DHB_HAVE_FREETYPE=OFF"])

    # 4. libass(meson, 出 dll; freetype/harfbuzz 静态链入, fribidi 保持动态 LGPL)
    la = os.path.join(build_root, "libass")
    meson_setup(la, srcs["libass"], pkg_path=pc_dir)
    meson_install(la)

    print(f"\n产物: {PREFIX}")
    for sub in ("bin", "lib", "include"):
        d = os.path.join(PREFIX, sub)
        if os.path.isdir(d):
            print(f"  {sub}/:", ", ".join(sorted(os.listdir(d))[:12]), flush=True)


if __name__ == "__main__":
    main()
