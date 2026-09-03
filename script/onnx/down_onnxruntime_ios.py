#!/usr/bin/env python3
"""
下载 onnxruntime iOS 库到 library/ios/onnxruntime 目录

支持:
- arm64 (真机)
- x64 (模拟器)
"""

import os
import sys
import urllib.request
import zipfile
import argparse

# 配置
ONNXRUNTIME_VERSION = "1.23.2"

# 下载地址 (官方 Microsoft releases)
URLS = {
    "arm64": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-ios-arm64-{ONNXRUNTIME_VERSION}.zip",
    "x64": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-ios-x64-{ONNXRUNTIME_VERSION}.zip",
}

def get_project_root():
    """获取项目根目录"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(os.path.dirname(script_dir))

def download_with_progress(url, dest_path):
    """带进度显示的下载"""
    def reporthook(block_num, block_size, total_size):
        downloaded = block_num * block_size
        percent = min(100, downloaded * 100 // total_size) if total_size > 0 else 0
        bar_len = 40
        filled = int(bar_len * downloaded / total_size) if total_size > 0 else 0
        bar = '=' * filled + '-' * (bar_len - filled)
        sys.stdout.write(f'\r[{bar}] {percent}% ({downloaded // 1024 // 1024}MB / {total_size // 1024 // 1024}MB)')
        sys.stdout.flush()

    urllib.request.urlretrieve(url, dest_path, reporthook)
    print()

def main():
    parser = argparse.ArgumentParser(description="下载 onnxruntime iOS 库")
    parser.add_argument("--arch", choices=["arm64", "x64", "both"], default="arm64",
                        help="CPU 架构: arm64(真机), x64(模拟器), both(两者) (默认: arm64)")
    parser.add_argument("--force", action="store_true", help="强制重新下载")
    args = parser.parse_args()

    archs = ["arm64", "x64"] if args.arch == "both" else [args.arch]
    project_root = get_project_root()
    target_dir = os.path.join(project_root, "3rdparty", "library", "ios", "onnxruntime")

    # 创建目标目录
    os.makedirs(target_dir, exist_ok=True)

    for arch in archs:
        url = URLS[arch]
        extract_name = f"onnxruntime-ios-{arch}-{ONNXRUNTIME_VERSION}"
        zip_path = os.path.join(target_dir, f"{extract_name}.zip")
        final_dir = os.path.join(target_dir, extract_name)

        # 检查是否已存在
        if os.path.exists(final_dir) and not args.force:
            print(f"onnxruntime {arch} 已存在: {final_dir}")
            print("如需重新下载，请使用 --force 参数或删除该目录")
            continue

        # 删除旧文件
        if os.path.exists(final_dir):
            import shutil
            shutil.rmtree(final_dir)

        # 下载
        device_type = "真机" if arch == "arm64" else "模拟器"
        print(f"\n下载 onnxruntime iOS {arch} ({device_type})...")
        print(f"版本: {ONNXRUNTIME_VERSION}")
        print(f"URL: {url}")

        try:
            download_with_progress(url, zip_path)
        except Exception as e:
            print(f"\n下载失败: {e}")
            print("\n如果网络不稳定，可以手动下载:")
            print(f"  {url}")
            print(f"然后解压到: {target_dir}")
            continue

        # 解压
        print(f"解压到: {target_dir}")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(target_dir)

        # 删除 zip 文件
        os.remove(zip_path)

        print(f"完成! {arch} 库已下载")
        print(f"  Framework: {final_dir}/onnxruntime.framework")

    print("\n" + "="*50)
    print("iOS ONNX Runtime 下载完成!")
    print("="*50)
    print("\n编译时请在 CMake 中设置:")
    print(f"  ONNXRUNTIME_DIR={target_dir}")

    if "arm64" in archs and "x64" in archs:
        print("\n已下载双架构版本，可创建 XCFramework:")
        print(f"  xcodebuild -create-xcframework \\")
        print(f"    -framework {target_dir}/onnxruntime-ios-arm64-{ONNXRUNTIME_VERSION}/onnxruntime.framework \\")
        print(f"    -framework {target_dir}/onnxruntime-ios-x64-{ONNXRUNTIME_VERSION}/onnxruntime.framework \\")
        print(f"    -output {target_dir}/onnxruntime.xcframework")

    return 0

if __name__ == "__main__":
    sys.exit(main())
