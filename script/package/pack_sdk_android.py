#!/usr/bin/env python3
"""部署 avox Android SDK 到目标目录 (so/plugins/assets)。

把顶层 .so 按 ABI 放 jni/<abi>/, plugins/ 目录原样(自包含)复制,
assets 只带 glsl/fonts/script (模型由用户用 fetch_assets.py 下载)。

用法:
  python pack_sdk_android.py --out <dir> [--abi arm64-v8a]
  python pack_sdk_android.py --out <dir> --zip avox-android-arm64-<版本>.zip
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

PLATFORM = "android"


def default_src(abi="arm64-v8a"):
    # 默认 install 目录按 CMAKE_SYSTEM_PROCESSOR 命名 (arm64 -> aarch64)
    proc = {"arm64-v8a": "aarch64", "armeabi-v7a": "armv7-a"}.get(abi, abi)
    return os.path.join(common.project_root(), "build", PLATFORM, "avplay", "install", proc)


def pack(out="", src="", zip_path="", headers=False, abi="arm64-v8a", with_thirdparty=False):
    out = out or os.path.join(common.project_root(), "deploy", PLATFORM)
    src = src or default_src(abi)
    if not os.path.isdir(src):
        raise SystemExit(f"找不到 SDK 构建输出: {src}\n请先构建, 或用 --src 指定 install 目录")

    print(f"== 部署 avox {PLATFORM} SDK (abi={abi}) ==")
    print(f"  来源: {src}")
    print(f"  目标: {out}")

    # 1) 顶层 .so -> jni/<abi>/ (Android AAR 布局)
    os.makedirs(out, exist_ok=True)
    jni_dir = os.path.join(out, "jni", abi)
    copied = 0
    for f in sorted(os.listdir(src)):
        s = os.path.join(src, f)
        if os.path.isfile(s) and f.endswith(".so"):
            common.copy_file(s, os.path.join(jni_dir, f))
            copied += 1
    print(f"  [复制] 顶层 .so ({copied} 个) -> jni/{abi}/")

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
            zip_path = os.path.join(zip_path, f"avox-{PLATFORM}-{abi}-{common.get_version()}.zip")
        common.make_zip(out, zip_path)

    print(f"== 完成: {out} ==")
    if not with_thirdparty:
        common.print_thirdparty_hint()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_common_args(ap)
    args = ap.parse_args(argv)
    pack(out=args.out, src=args.src, zip_path=args.zip, headers=args.headers, abi=args.abi,
         with_thirdparty=args.with_thirdparty)


if __name__ == "__main__":
    main()
