#!/usr/bin/env python3
"""
下载 onnxruntime Android 静态库到 library/android/onnxruntime 目录
"""

import os
import sys
import urllib.request
import zipfile
import shutil

# 配置
ONNXRUNTIME_VERSION = "1.23.2"
ARCH = "arm64-v8a"
URL = f"https://github.com/csukuangfj/onnxruntime-libs/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-android-{ARCH}-static_lib-{ONNXRUNTIME_VERSION}.zip"

def get_project_root():
    """获取项目根目录"""
    # 获取脚本所在目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # script/onnx -> 项目根目录
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
    project_root = get_project_root()
    target_dir = os.path.join(project_root, "3rdparty", "library", "android", "onnxruntime")
    zip_path = os.path.join(target_dir, "onnxruntime-static.zip")
    extract_dir = os.path.join(target_dir, f"onnxruntime-android-{ARCH}-static_lib-{ONNXRUNTIME_VERSION}")

    # 创建目标目录
    os.makedirs(target_dir, exist_ok=True)

    # 检查是否已解压
    if os.path.exists(extract_dir):
        print(f"onnxruntime 已存在: {extract_dir}")
        print("如需重新下载，请删除该目录")
        print("\n编译 sherpa-onnx 时请设置以下环境变量:")
        print(f"  SHERPA_ONNXRUNTIME_LIB_DIR={extract_dir}/lib")
        print(f"  SHERPA_ONNXRUNTIME_INCLUDE_DIR={extract_dir}/include")
        return

    # 下载
    print(f"下载 onnxruntime Android 静态库...")
    print(f"URL: {URL}")
    download_with_progress(URL, zip_path)

    # 解压
    print(f"解压到: {target_dir}")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(target_dir)

    # 删除 zip 文件
    os.remove(zip_path)

    print("\n完成!")
    print(f"库路径: {extract_dir}/lib")
    print(f"头文件路径: {extract_dir}/include")
    print("\n编译 sherpa-onnx 时请设置以下环境变量:")
    print(f"  SHERPA_ONNXRUNTIME_LIB_DIR={extract_dir}/lib")
    print(f"  SHERPA_ONNXRUNTIME_INCLUDE_DIR={extract_dir}/include")

if __name__ == "__main__":
    main()