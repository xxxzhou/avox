#!/usr/bin/env python3
"""MinGW 构建的 FFmpeg dll -> MSVC .lib 导入库。

原理: dumpbin /exports 提取导出表 -> 生成 .def -> lib.exe /def 产出 .lib。
用法:
  python make_msvc_lib.py <dll目录> [--out <lib输出目录>]
  (dumpbin/lib.exe 自动从 VS2022 安装定位; 产物命名 avcodec-63.lib 等)
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

EXPORT_LINE = re.compile(r"^\s*(\d+)\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$")

VS_ROOT = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
BIN = os.path.join(VS_ROOT, os.listdir(VS_ROOT)[-1], "bin", "Hostx64", "x64")
DUMPBIN = os.path.join(BIN, "dumpbin.exe")
LIB = os.path.join(BIN, "lib.exe")


def exports_of(dll):
    out = subprocess.run([DUMPBIN, "/exports", dll],
                         capture_output=True, text=True).stdout
    names, seen = [], set()
    for line in out.splitlines():
        m = EXPORT_LINE.match(line)
        # 只匹配 "ordinal hint RVA name" 四列行, 排除 Summary 段 ("626 number of functions")
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            names.append((m.group(1), m.group(2)))
    return names


def make_lib(dll, out_dir):
    base = os.path.splitext(os.path.basename(dll))[0]  # avcodec-63
    names = exports_of(dll)
    if not names:
        print(f"跳过 {base}: 无导出表")
        return None
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
        return None
    print(f"✅ {lib_path} ({len(names)} exports)")
    return lib_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll_dir")
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out or args.dll_dir)
    os.makedirs(out_dir, exist_ok=True)
    ok = 0
    for dll in sorted(glob.glob(os.path.join(args.dll_dir, "av*.dll")) +
                      glob.glob(os.path.join(args.dll_dir, "sw*.dll"))):
        if make_lib(dll, out_dir):
            ok += 1
    print(f"完成: {ok} 个 .lib 生成于 {out_dir}")


if __name__ == "__main__":
    main()
