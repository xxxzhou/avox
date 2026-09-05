#!/usr/bin/env python3
"""商业分发包 GPL 合规自检: 扫描目录下 dll/so 二进制里的 GPL 构建标记。

查什么:
  - FFmpeg configure 串里的 --enable-gpl / --enable-nonfree / --enable-libx264(x265)
    (avcodec_configuration() 的配置串会编进 avcodec dll 的 .rdata)
  - x264/x265 等GPL-only 组件的 dll 文件名
为什么: 分发需满足 LGPL 合规 (动态链接路径), 混入 GPL-only 组件即与
合规分发冲突。

用法:
  python check_licenses.py                                  # 查默认构建安装树
  python check_licenses.py --dir deploy/godot_pro           # 查打包产物 (推荐, 出包后必跑)
  python check_licenses.py --dir deploy/godot_pro --quiet   # 只在有问题时输出

退出码: 0=干净 / 1=发现 GPL 标记 / 2=目录不可用
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

DEFAULT_DIR = os.path.join(
    common.project_root(), "build", "windows", "avplay", "install", "AMD64", "Release")
SCAN_EXTS = {".dll", ".so", ".node"}
# 二进制内容标记 (ascii 与 utf-16le 各扫一遍; configure 串实际为 ascii)
CONTENT_MARKS = [
    b"--enable-gpl", b"--enable-nonfree",
    b"--enable-libx264", b"--enable-libx265", b"--enable-libdav1d",  # dav1d 无罪但列出便于人工确认
]
GPL_MARKS = {b"--enable-gpl", b"--enable-nonfree", b"--enable-libx264", b"--enable-libx265"}
# GPL/Nonfree 组件的文件名前缀 (出现即违规)
GPL_FILE_PREFIXES = ("x264", "x265", "libx264", "libx265", "faad", "fdk-aac")


def contains(data, mark):
    if mark in data:
        return True
    return mark.decode("ascii").encode("utf-16-le") in data


def scan_dir(root, quiet):
    hits, files = [], 0
    for dirpath, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in common.SKIP_NAMES]
        for n in names:
            ext = os.path.splitext(n)[1].lower()
            if ext not in SCAN_EXTS:
                continue
            path = os.path.join(dirpath, n)
            files += 1
            low = n.lower()
            if any(low.startswith(p) for p in GPL_FILE_PREFIXES):
                hits.append((path, "文件名: GPL/Nonfree 组件 (x264/x265/faad/fdk-aac)"))
                continue
            try:
                with open(path, "rb") as f:
                    data = f.read()
            except OSError as e:
                print(f"  [警告] 读不了 {path}: {e}")
                continue
            for mark in CONTENT_MARKS:
                if contains(data, mark):
                    kind = "违规" if mark in GPL_MARKS else "提示"
                    hits.append((path, f"{kind}: 配置串含 {mark.decode('ascii')}"))
                    break
    print(f"== 扫描 {root} ==")
    print(f"  二进制文件: {files} 个 (.dll/.so/.node)")
    if not hits:
        print("  ✅ 未发现 GPL-only 构建标记 (x264/x265/--enable-gpl/--enable-nonfree)")
        if not quiet:
            print("  注: 本检查覆盖二进制标记, 完整合规要求以 LICENSE 与法务审阅为准")
        return 0
    print(f"  ❌ {len(hits)} 处 GPL 相关标记:")
    for path, why in hits:
        print(f"    - {os.path.relpath(path, root)}\n      {why}")
    print("  处置: 分发构建须禁用 GPL 组件 (换硬件编码器/OpenH264)")
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="", help="要扫描的目录 (默认构建安装树; 出包后请扫产物)")
    ap.add_argument("--quiet", action="store_true", help="通过时少输出")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.dir or DEFAULT_DIR)
    if not os.path.isdir(root):
        print(f"目录不存在: {root}")
        return 2
    return scan_dir(root, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
