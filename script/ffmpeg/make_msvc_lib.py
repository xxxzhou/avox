#!/usr/bin/env python3
"""MinGW 构建的 FFmpeg dll -> MSVC .lib 导入库。

原理: dumpbin /exports 提取导出表 -> 生成 .def -> lib.exe /def 产出 .lib。
用法:
  python make_msvc_lib.py <dll目录> [--out <lib输出目录>]
  (dumpbin/lib.exe 经 vswhere 从 VS 安装定位, 兜底常规安装布局; 产物命名 avcodec-63.lib 等)
  任一 dll 处理失败以非零码退出, 防半成品库被静默使用。
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

EXPORT_LINE = re.compile(r"^\s*(\d+)\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$")


def _version_key(name):
    parts = []
    for x in name.split("."):
        try:
            parts.append((0, int(x)))
        except ValueError:
            parts.append((1, x))
    return parts


def locate_msvc_bin():
    """vswhere定位最新VS的MSVC Hostx64/x64工具目录, 失败按常规布局兜底"""
    roots = []
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.exists(vswhere):
        try:
            r = subprocess.run([vswhere, "-latest", "-products", "*",
                                "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                "-property", "installationPath"],
                               capture_output=True, text=True)
            if r.returncode == 0:
                roots += [os.path.join(p.strip(), "VC", "Tools", "MSVC")
                          for p in r.stdout.splitlines() if p.strip()]
        except OSError:
            pass
    # 兜底: 常规安装布局(Community/Professional/BuildTools)
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    roots += sorted(glob.glob(os.path.join(program_files, "Microsoft Visual Studio",
                                           "*", "*", "VC", "Tools", "MSVC")), reverse=True)
    for root in roots:
        if not os.path.isdir(root):
            continue
        for ver in sorted(os.listdir(root), key=_version_key, reverse=True):
            bin_dir = os.path.join(root, ver, "bin", "Hostx64", "x64")
            if os.path.exists(os.path.join(bin_dir, "dumpbin.exe")):
                return bin_dir
    raise FileNotFoundError("未找到 MSVC 工具链(dumpbin/lib.exe), "
                            "请安装 VS2022 C++ 工具集或在 VS 命令行运行")


BIN = locate_msvc_bin()
DUMPBIN = os.path.join(BIN, "dumpbin.exe")
LIB = os.path.join(BIN, "lib.exe")
print(f"MSVC 工具链: {BIN}")


def exports_of(dll):
    r = subprocess.run([DUMPBIN, "/exports", dll], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"dumpbin 失败(rc={r.returncode}): {(r.stderr or r.stdout)[:300]}")
    names, seen = [], set()
    for line in r.stdout.splitlines():
        m = EXPORT_LINE.match(line)
        # 只匹配 "ordinal hint RVA name" 四列行, 排除 Summary 段 ("626 number of functions")
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            names.append((m.group(1), m.group(2)))
    return names


def make_lib(dll, out_dir):
    base = os.path.splitext(os.path.basename(dll))[0]  # avcodec-63
    try:
        names = exports_of(dll)
    except RuntimeError as e:
        print(f"❌ {base}: {e}")
        return False
    if not names:
        print(f"跳过 {base}: 无导出表")
        return True
    with tempfile.NamedTemporaryFile("w", suffix=".def", delete=False) as f:
        f.write(f"LIBRARY {base}\nEXPORTS\n")
        for ordinal, name in names:
            f.write(f"    {name} @{ordinal}\n")
        def_path = f.name
    lib_path = os.path.join(out_dir, base + ".lib")
    r = subprocess.run([LIB, "/nologo", "/def:" + def_path, "/machine:x64", "/out:" + lib_path],
                       capture_output=True, text=True)
    os.unlink(def_path)
    if r.returncode != 0:
        print(f"❌ {base}: {r.stderr[:300]}")
        return False
    print(f"✅ {lib_path} ({len(names)} exports)")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll_dir")
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out or args.dll_dir)
    os.makedirs(out_dir, exist_ok=True)
    ok, failed = 0, []
    for dll in sorted(glob.glob(os.path.join(args.dll_dir, "av*.dll")) +
                      glob.glob(os.path.join(args.dll_dir, "sw*.dll"))):
        if make_lib(dll, out_dir):
            ok += 1
        else:
            failed.append(os.path.basename(dll))
    print(f"完成: {ok} 个 .lib 生成于 {out_dir}")
    if failed:
        print(f"失败: {len(failed)} 个: {', '.join(failed)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
