#!/usr/bin/env python3
"""部署 avox iOS SDK 到目标目录 (dylib/a/plugins/assets)。

顶层 *.dylib / *.a 放 lib/, plugins/ 目录原样(自包含)复制,
assets 只带 glsl/fonts/script (模型由用户用 fetch_assets.py 下载)。

用法:
  python pack_sdk_ios.py --out <dir> [--src <install目录>]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

PLATFORM = "ios"
DEFAULT_SRC = os.path.join(
    common.project_root(), "build", PLATFORM, "avox", "install", "arm64")


def pack(out="", src="", zip_path="", headers=False, abi=None, with_thirdparty=False):
    out = out or os.path.join(common.project_root(), "deploy", PLATFORM)
    src = src or DEFAULT_SRC
    if not os.path.isdir(src):
        raise SystemExit(f"找不到 SDK 构建输出: {src}\n请先构建, 或用 --src 指定 install 目录")

    print(f"== 部署 avox {PLATFORM} SDK ==")
    print(f"  来源: {src}")
    print(f"  目标: {out}")

    # 1) 顶层库文件 -> lib/
    os.makedirs(out, exist_ok=True)
    lib_dir = os.path.join(out, "lib")
    copied = 0
    for f in sorted(os.listdir(src)):
        s = os.path.join(src, f)
        if os.path.isfile(s) and (f.endswith(".dylib") or f.endswith(".a")):
            common.copy_file(s, os.path.join(lib_dir, f))
            copied += 1
    print(f"  [复制] 顶层库文件 ({copied} 个) -> lib/")

    # 2) 插件目录 (插件壳自包含; 第三方库默认不打包, 由 fetch_assets 按需下载)
    common.copy_plugins(os.path.join(src, "plugins"), os.path.join(out, "plugins"), with_thirdparty)

    # 3) assets 子集 (glsl/fonts/script; 模型由用户下载)
    common.copy_assets(os.path.join(src, "assets"), os.path.join(out, "assets"))

    # 4) 头文件 (可选)
    if headers:
        include_src = os.path.join(os.path.dirname(src), "include")
        common.copy_dir(include_src, os.path.join(out, "include"))

    # 5) 打包 zip (可选)
    if zip_path:
        if os.path.isdir(zip_path):
            zip_path = os.path.join(zip_path, f"avox-{PLATFORM}-arm64-{common.get_version()}.zip")
        common.make_zip(out, zip_path)

    print(f"== 完成: {out} ==")
    if not with_thirdparty:
        common.print_thirdparty_hint()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_common_args(ap)
    args = ap.parse_args(argv)
    pack(out=args.out, src=args.src, zip_path=args.zip, headers=args.headers,
         with_thirdparty=args.with_thirdparty)


if __name__ == "__main__":
    main()
