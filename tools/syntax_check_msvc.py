"""单文件 MSVC 语法检查驱动(纯 Python, 不调 cmd.exe).

工具环境里 cmd.exe 调不动, 故这里直接用 subprocess 设好 MSVC 环境后调 cl.exe。
用途: 改完 avox 源码后先做一次廉价编译检查, 再决定要不要跑完整构建。
"""
import os
import subprocess
import sys

VS = r"C:\Program Files\Microsoft Visual Studio\2022\Community"
VCVARS = os.path.join(VS, "VC", "Auxiliary", "Build", "vcvars64.bat")
AVOX = r"D:\Work\github\avox"
FFMPEG_INC = os.path.join(AVOX, "3rdparty", "library", "Windows", "ffmpeg", "include")

# /I 是覆盖 INCLUDE 而非追加, 故 SDK 头必须排在最前(与 vcvars 顺序一致)
BUSINESS_INCLUDES = [
    os.path.join(AVOX, "src"),
    os.path.join(AVOX, "src", "avox"),
    FFMPEG_INC,
    os.path.join(FFMPEG_INC, "libavcodec"),
    os.path.join(FFMPEG_INC, "libavutil"),
]

# vendor 子树(codec/dovi)的 .c 与 dovi_rpu_wrap.cpp 需要内部头路径。
# 注意这**只是检查用**: 真实构建里这些路径必须挂在源文件级, 绝不能进全局
# include(自带 libavutil/internal.h shim 会盖掉真的, 全库崩)。
DOVI_DIR = os.path.join(AVOX, "src", "avox", "codec", "dovi")
VENDOR_INCLUDES = [DOVI_DIR, os.path.join(DOVI_DIR, "libavcodec")] + [
    os.path.join(FFMPEG_INC, "libavcodec"),
    os.path.join(FFMPEG_INC, "libavutil"),
    FFMPEG_INC,
]


def is_vendor_file(src):
    p = os.path.abspath(src).replace("\\", "/").lower()
    return "/avox/codec/dovi/" in p


def sdk_includes():
    """SDK/CRT 头路径(WinSDK ucrt/shared/um + MSVC 自带), 必须显式给 /I。

    MSVC 的 /I 是**覆盖** INCLUDE 环境变量而不是追加: 命令行只要出现任何一个
    /I, 整条 INCLUDE 就被忽略。故这里必须自己带上, 不能指望 env["INCLUDE"]。
    """
    tools = os.path.join(VS, "VC", "Tools", "MSVC",
                         sorted(os.listdir(os.path.join(VS, "VC", "Tools", "MSVC")))[-1])
    inc = []
    kits = r"C:\Program Files (x86)\Windows Kits\10"
    inc_root = os.path.join(kits, "Include")
    if os.path.isdir(inc_root):
        ver = sorted(os.listdir(inc_root))[-1]
        for sub in ("ucrt", "shared", "um"):
            inc.append(os.path.join(inc_root, ver, sub))
    inc.append(os.path.join(tools, "include"))
    return inc


def vcvars_env():
    """不调 cmd.exe(cmd 在本工具环境被拦): 直接手工拼 MSVC 环境变量。

    只设 cl.exe 起得来所需的几个: PATH(含 cl 与 link 目录)、INCLUDE、LIB。
    """
    env = dict(os.environ)
    # 取最新的 MSVC 工具集版本
    msvc_root = os.path.join(VS, "VC", "Tools", "MSVC")
    versions = sorted(os.listdir(msvc_root)) if os.path.isdir(msvc_root) else []
    if not versions:
        raise RuntimeError(f"未找到 MSVC 工具集: {msvc_root}")
    tools = os.path.join(msvc_root, versions[-1])
    host = os.path.join(tools, "bin", "Hostx64", "x64")
    # Windows SDK
    kits = r"C:\Program Files (x86)\Windows Kits\10"
    kit_ver = None
    inc_root = os.path.join(kits, "Include")
    if os.path.isdir(inc_root):
        vs = sorted(os.listdir(inc_root))
        kit_ver = vs[-1] if vs else None
    paths = [host]
    includes = []
    libs = []
    if kit_ver:
        # WinRT 不设(纯 C 端口不需要); 顺序与 vcvars 一致: ucrt → shared → um
        for sub in ("ucrt", "shared", "um"):
            includes.append(os.path.join(inc_root, kit_ver, sub))
        for sub in ("ucrt", "um"):
            libs.append(os.path.join(kits, "Lib", kit_ver, sub))
    # MSVC 自带头目录必须在最后(它的 <ctime> 等要能 include 到 ucrt 的 time.h)
    includes.append(os.path.join(tools, "include"))
    libs.append(os.path.join(tools, "lib", "x64"))
    # MSVC 的 cl 依赖其 bin 目录, 也依赖 VS 的 Common7(部分工具)
    paths.append(os.path.join(VS, "Common7", "IDE", "VC", "VCPackages"))
    env["PATH"] = os.pathsep.join(paths + [env.get("PATH", "")])
    env["INCLUDE"] = os.pathsep.join(includes)
    env["LIB"] = os.pathsep.join(libs)
    env["VCINSTALLDIR"] = os.path.join(VS, "VC")
    env["VCToolsInstallDir"] = tools + os.sep
    # 这几个 vcvars 会设, 缺了 ucrt 的 <ctime>/<corecrt.h> 解析会失败
    env["WindowsSdkDir"] = kits + os.sep
    if kit_ver:
        env["WindowsSDKVersion"] = kit_ver + "\\"
        env["UCRTVersion"] = kit_ver
        env["UniversalCRTSdkDir"] = kits + os.sep
    env["VCToolsVersion"] = versions[-1]
    env["VisualStudioVersion"] = "17.0"
    return env


def compile_file(src, extra_defines=None, std="c++17", language=None):
    env = vcvars_env()
    if language == "c":
        cl = os.path.join(env["VCToolsInstallDir"], "bin", "Hostx64", "x64", "cl.exe")
        args = [cl, "/nologo", "/c", "/TC"]
    else:
        cl = os.path.join(env["VCToolsInstallDir"], "bin", "Hostx64", "x64", "cl.exe")
        args = [cl, "/nologo", "/c", "/TP", f"/std:{std}"]
    args += ["/EHsc", "/Zc:__cplusplus", "/D_CRT_SECURE_NO_WARNINGS",
             "/DAVOX_EXPORT_DEFINE", "/DNOMINMAX=1", "/DAVOX_DEBUG=0",
             "/DAVOX_PLATFORM_X64",
             # 平台宏: Window.hpp 用 WIN32 选 AvoxSurfaceType(HWND), 缺了会
             # 在业务头里报一堆「未知重写说明符」的假错
             "/DWIN32", "/D_WIN32", "/D_WINDOWS"]
    for inc in (sdk_includes() + BUSINESS_INCLUDES +
                (VENDOR_INCLUDES if is_vendor_file(src) else [])):
        args.append("/I" + inc)
    for d in (extra_defines or []):
        args.append("/D" + d)
    # /Fo 必须是 Windows 绝对路径: Bash 里 TEMP 可能是 /tmp, 那样 cl 会把
    # 路径当参数解析, 整个参数表错乱(实测表现为 <ctime> 里 clock_t 找不到)
    obj_dir = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    if not os.path.isabs(obj_dir) or obj_dir.startswith("/"):
        obj_dir = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
    obj_dir = os.path.normpath(obj_dir)
    args += ["/Fo" + os.path.join(obj_dir, "syntax_check.obj"),
             os.path.abspath(src)]
    r = subprocess.run(args, env=env, cwd=AVOX, capture_output=True, text=True)
    return r


if __name__ == "__main__":
    files = sys.argv[1:]
    if not files:
        print("usage: python syntax_check_msvc.py <file.cpp> [file2.c ...]")
        sys.exit(2)
    bad = 0
    for f in files:
        lang = "c" if f.endswith(".c") else None
        r = compile_file(f, language=lang)
        tag = "OK " if r.returncode == 0 else "ERR"
        print(f"[{tag}] {f} (exit={r.returncode})")
        if r.returncode != 0:
            bad += 1
            out = (r.stdout or "") + (r.stderr or "")
            # 只挑 error / fatal, 忽略第三方头的 warning 噪声
            lines = [l for l in out.splitlines()
                     if ("error" in l.lower() or "fatal" in l.lower()
                         or "错误" in l)]
            if not lines:
                lines = out.splitlines()[-30:]
            for line in lines[:40]:
                print("   ", line)
    sys.exit(1 if bad else 0)
