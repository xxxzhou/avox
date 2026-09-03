#!/usr/bin/env python3
"""
avox SDK 打包/部署工具 (按平台分派)。

把运行需要的 dll/.so、自包含的 plugins/、assets/glsl|fonts|script
部署到目标目录 —— 模型不打包, 用户用部署目录里的 assets/script/fetch_assets.py 自行下载。
目标目录直接给路径即可。

用法:
  python pack_sdk.py --platform windows --out <dir>
  python pack_sdk.py --platform android  --out <dir> --abi arm64-v8a
  python pack_sdk.py --platform ios      --out <dir>
  python pack_sdk.py --platform linux    --out <dir> [--src <install目录>]

通用参数:
  --out <dir>     部署目标目录
  --src <dir>     SDK 构建输出 (install) 目录; 默认按平台自动查找
  --zip <path>    额外把部署结果打包成 zip
  --headers       连同 include/ 头文件一起部署
  --abi <name>    Android 目标 ABI (arm64-v8a / armeabi-v7a)

每个平台另有独立脚本, 效果相同:
  pack_sdk_windows.py / pack_sdk_android.py / pack_sdk_ios.py / pack_sdk_linux.py
"""
import argparse
import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PLATFORM_MODULES = {
    "windows": "pack_sdk_windows",
    "android": "pack_sdk_android",
    "ios": "pack_sdk_ios",
    "linux": "pack_sdk_linux",
}


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", required=True, choices=sorted(PLATFORM_MODULES))
    ap.add_argument("--out", default="", help="部署目标目录")
    ap.add_argument("--src", default="", help="SDK install 目录; 默认自动查找")
    ap.add_argument("--zip", default="", help="额外打 zip (zip 文件路径)")
    ap.add_argument("--headers", action="store_true", help="连头文件一起部署")
    ap.add_argument("--with-thirdparty", action="store_true",
                    help="连 opencv/openvino/onnxruntime/tbb 等第三方库一起打包 (默认不打包, 用户用 fetch_assets.py 按需下载)")
    ap.add_argument("--abi", default="arm64-v8a", help="Android ABI")
    args = ap.parse_args(argv)

    mod = importlib.import_module(PLATFORM_MODULES[args.platform])
    mod.pack(out=args.out, src=args.src, zip_path=args.zip, headers=args.headers,
             abi=args.abi, with_thirdparty=args.with_thirdparty)


if __name__ == "__main__":
    main()
