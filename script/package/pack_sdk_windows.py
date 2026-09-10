#!/usr/bin/env python3
"""部署 avox Windows SDK 到目标目录 (dll/plugins/assets)。

把运行需要的顶层 dll、自包含的 plugins/ 目录、assets/glsl|fonts|script
复制到目标目录 (模型不打包, 用户用 assets/script/fetch_assets.py 自行下载)。

用法:
  python pack_sdk_windows.py --out <dir>
  python pack_sdk_windows.py --out <dir> --zip avox-windows-x64-<版本>.zip
  python pack_sdk_windows.py --out <dir> --src <自定install目录> --headers
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

PLATFORM = "windows"
DEFAULT_SRC = os.path.join(
    common.project_root(), "build", "windows", "avox", "install", "AMD64", "Release")
# 随 SDK 一起部署的 CLI 工具
CLI_EXES = ("avox_cli.exe", "avox_agent.exe")
# 顶层剔除的 dll: fdk-aac 源许可 (Fraunhofer SIC-004) 仅限非商业, FFmpeg 归 nonfree,
# 公开分发的 SDK 不带 (播放解码走 FFmpeg 原生 AAC; 编码需求见 doc/test/发布检查清单.md)
DENY_TOP_DLLS = ("fdk-aac.dll",)


def clean_legacy_plugin_deps(dst):
    """旧布局把插件第三方依赖放顶层; 新布局它们在 plugins/ 内自包含, 清掉顶层残留避免双份。"""
    if not os.path.isdir(dst):
        return 0
    removed = 0
    for f in os.listdir(dst):
        if not (f.endswith((".dll", ".node")) and common.is_plugin_dep(f)):
            continue
        p = os.path.join(dst, f)
        try:
            os.remove(p)
            print(f"  [清理] 顶层残留(已随插件进 plugins/): {f}")
            removed += 1
        except OSError as e:
            print(f"  [警告] 清理 {f} 失败: {e}")
    return removed


def pack(out="", src="", zip_path="", headers=False, abi=None, with_thirdparty=False):
    out = out or os.path.join(common.project_root(), "deploy", PLATFORM)
    src = src or DEFAULT_SRC
    if not os.path.isdir(src):
        raise SystemExit(f"找不到 SDK 构建输出: {src}\n请先构建, 或用 --src 指定 install 目录")

    print(f"== 部署 avox {PLATFORM} SDK ==")
    print(f"  来源: {src}")
    print(f"  目标: {out}")

    # 1) 顶层运行文件: *.dll / *.node / AvoxWrapper.py, 剔除插件第三方依赖
    os.makedirs(out, exist_ok=True)
    copied = 0
    for f in sorted(os.listdir(src)):
        s = os.path.join(src, f)
        if not os.path.isfile(s):
            continue
        if f.endswith((".dll", ".node")) or f == "AvoxWrapper.py":
            if common.is_plugin_dep(f):
                continue
            if f in DENY_TOP_DLLS:
                print(f"  [跳过] {f} (非商业许可, 公开 SDK 不分发)")
                continue
            common.copy_file(s, os.path.join(out, f))
            copied += 1
    print(f"  [复制] 顶层运行文件 ({copied} 个)")

    # 1b) CLI 工具 (avox_cli / avox_agent)
    for f in CLI_EXES:
        s = os.path.join(src, f)
        if os.path.isfile(s):
            common.copy_file(s, os.path.join(out, f))
            print(f"  [复制] CLI: {f}")

    # 2) 插件目录 (插件壳自包含; 第三方库默认不打包, 由 fetch_assets 按需下载)
    common.copy_plugins(os.path.join(src, "plugins"), os.path.join(out, "plugins"), with_thirdparty)

    # 3) assets 子集 (glsl/fonts/script; 模型由用户下载)
    common.copy_assets(os.path.join(src, "assets"), os.path.join(out, "assets"))

    # 4) 头文件 (可选): install/include (Release 目录上溯两级; 兼容旧布局 install/<arch>/include)
    if headers:
        include_src = os.path.join(os.path.dirname(os.path.dirname(src)), "include")
        if not os.path.isdir(include_src):
            include_src = os.path.join(os.path.dirname(src), "include")
        common.copy_dir(include_src, os.path.join(out, "include"))

    # 5) 清理旧布局顶层残留
    clean_legacy_plugin_deps(out)

    # 6) 打包 zip (可选)
    if zip_path:
        if os.path.isdir(zip_path):
            zip_path = os.path.join(zip_path, f"avox-{PLATFORM}-x64-{common.get_version()}.zip")
        common.make_zip(out, zip_path)

    print(f"== 完成: {out} ==")
    if not with_thirdparty:
        common.print_thirdparty_hint()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_common_args(ap)
    args = ap.parse_args(argv)
    pack(out=args.out, src=args.src, zip_path=args.zip, headers=args.headers, with_thirdparty=args.with_thirdparty)


if __name__ == "__main__":
    main()
