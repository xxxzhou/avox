#!/usr/bin/env python3
"""
下载 onnxruntime Linux 库到 library/linux/onnxruntime 目录

支持:
- CPU 版本 (默认)
- GPU 版本 (--gpu)
- x64 架构
"""

import os
import sys
import urllib.request
import tarfile
import argparse
import platform
import shutil

# 配置
ONNXRUNTIME_VERSION = "1.23.2"

# 下载地址
URLS = {
    "x64": {
        "cpu": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-{ONNXRUNTIME_VERSION}.tgz",
        "gpu": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-gpu-{ONNXRUNTIME_VERSION}.tgz",
    },
}

def get_project_root():
    """获取项目根目录"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(os.path.dirname(script_dir))

def detect_arch():
    """自动检测当前系统架构"""
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        return "x64"
    elif machine in ("aarch64", "arm64"):
        return "arm64"
    else:
        return "x64"

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

def get_extract_name(arch, gpu):
    """获取解压后的目录名"""
    gpu_suffix = "-gpu" if gpu else ""
    return f"onnxruntime-linux-{arch}{gpu_suffix}-{ONNXRUNTIME_VERSION}"

def main():
    # 自动检测架构
    default_arch = detect_arch()

    parser = argparse.ArgumentParser(description="下载 onnxruntime Linux 库")
    parser.add_argument("--gpu", action="store_true", help="下载 GPU 版本 (需要 CUDA)")
    parser.add_argument("--arch", choices=["x64", "auto"], default="auto",
                        help=f"CPU 架构 (默认: auto 检测为 {default_arch})")
    parser.add_argument("--force", action="store_true", help="强制重新下载")
    args = parser.parse_args()

    # 确定架构
    arch = detect_arch() if args.arch == "auto" else args.arch
    gpu = args.gpu

    # 检查是否支持
    if arch not in URLS:
        print(f"错误: 不支持的架构 {arch}")
        print("当前仅支持: x64")
        return 1

    url = URLS[arch]["gpu" if gpu else "cpu"]
    if url is None:
        print(f"错误: 不支持的组合 {arch} + {'GPU' if gpu else 'CPU'}")
        return 1

    project_root = get_project_root()
    target_dir = os.path.join(project_root, "3rdparty", "library", "linux", "onnxruntime")
    extract_name = get_extract_name(arch, gpu)
    tgz_path = os.path.join(target_dir, f"{extract_name}.tgz")

    # 创建目标目录
    os.makedirs(target_dir, exist_ok=True)

    # 检查是否已存在
    final_dir = os.path.join(target_dir, extract_name)
    if os.path.exists(final_dir) and not args.force:
        print(f"onnxruntime 已存在: {final_dir}")
        print("如需重新下载，请使用 --force 参数或删除该目录")
        print("\n编译时请设置以下环境变量:")
        print(f"  ONNXRUNTIME_DIR={final_dir}")
        return 0

    # 删除旧文件
    if os.path.exists(final_dir):
        shutil.rmtree(final_dir)

    # 下载
    version_type = "GPU" if gpu else "CPU"
    arch_info = f"{arch} (自动检测)" if args.arch == "auto" else arch
    print(f"下载 onnxruntime Linux {version_type} 版本 ({arch_info})...")
    print(f"版本: {ONNXRUNTIME_VERSION}")
    print(f"URL: {url}")

    try:
        download_with_progress(url, tgz_path)
    except Exception as e:
        print(f"\n下载失败: {e}")
        print("\n如果网络不稳定，可以手动下载:")
        print(f"  {url}")
        print(f"然后解压到: {target_dir}")
        return 1

    # 解压
    print(f"解压到: {target_dir}")
    with tarfile.open(tgz_path, 'r:gz') as tar_ref:
        tar_ref.extractall(target_dir)

    # 删除 tgz 文件
    os.remove(tgz_path)

    print("\n完成!")
    print(f"库路径: {final_dir}/lib")
    print(f"头文件路径: {final_dir}/include")
    print("\n编译时请设置以下环境变量:")
    print(f"  ONNXRUNTIME_DIR={final_dir}")

    if gpu:
        print("\n注意: GPU 版本需要 CUDA 运行时库:")
        print("  - libcudart.so")
        print("  - libcublas.so")
        print("  - libcublasLt.so")

    return 0

if __name__ == "__main__":
    sys.exit(main())
