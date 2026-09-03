#!/usr/bin/env python3
"""
下载 onnxruntime Windows 库到 library/windows/onnxruntime 目录

支持:
- CPU 版本 (默认)
- GPU 版本 (--gpu)
- 自动检测当前系统架构
"""

import os
import sys
import urllib.request
import zipfile
import argparse
import platform

# 配置
ONNXRUNTIME_VERSION = "1.23.2"

# 下载地址
# 注意: 微软官方 release (microsoft/onnxruntime) 是 MD (动态 CRT) 版, 依赖 MSVCP140.dll,
# 在 plugins/ 目录下 DllMain 初始化失败 (err=1114)。
# 改用 k2-fsa/sherpa-onnx 团队构建的 MT (静态 CRT) 版, 无 MSVCP140 依赖, DllMain 稳定。
ONNXRUNTIME_MT_REPO = "csukuangfj/onnxruntime-libs"

URLS = {
    "x64": {
        "cpu": f"https://github.com/{ONNXRUNTIME_MT_REPO}/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-win-x64-MT-Release-{ONNXRUNTIME_VERSION}.tar.bz2",
        "gpu": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-win-x64-gpu-{ONNXRUNTIME_VERSION}.zip",
    },
    "x86": {
        "cpu": f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNXRUNTIME_VERSION}/onnxruntime-win-x86-{ONNXRUNTIME_VERSION}.zip",
        "gpu": None,  # x86 没有 GPU 版本
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
    elif machine in ("x86", "i386", "i686"):
        return "x86"
    else:
        # 默认 x64
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
    # x64 CPU 使用 MT 版, 解压后目录名不同
    if not gpu and arch == "x64":
        return f"onnxruntime-win-{arch}-MT-Release-{ONNXRUNTIME_VERSION}"
    return f"onnxruntime-win-{arch}{gpu_suffix}-{ONNXRUNTIME_VERSION}"

def main():
    # 自动检测架构
    default_arch = detect_arch()

    parser = argparse.ArgumentParser(description="下载 onnxruntime Windows 库")
    parser.add_argument("--gpu", action="store_true", help="下载 GPU 版本 (需要 CUDA)")
    parser.add_argument("--arch", choices=["x64", "x86", "auto"], default="auto",
                        help=f"CPU 架构 (默认: auto 检测为 {default_arch})")
    parser.add_argument("--force", action="store_true", help="强制重新下载")
    args = parser.parse_args()

    # 确定架构
    arch = detect_arch() if args.arch == "auto" else args.arch
    gpu = args.gpu

    # 检查 GPU + x86 组合
    if gpu and arch == "x86":
        print("错误: x86 架构不支持 GPU 版本")
        return 1

    url = URLS[arch]["gpu" if gpu else "cpu"]
    if url is None:
        print(f"错误: 不支持的组合 {arch} + {'GPU' if gpu else 'CPU'}")
        return 1

    project_root = get_project_root()
    target_dir = os.path.join(project_root, "3rdparty", "library", "windows", "onnxruntime")
    extract_name = get_extract_name(arch, gpu)
    # MT 版是 .tar.bz2, 微软官方版是 .zip
    is_mt = (not gpu and arch == "x64")
    archive_ext = ".tar.bz2" if is_mt else ".zip"
    archive_path = os.path.join(target_dir, f"{extract_name}{archive_ext}")

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
        import shutil
        shutil.rmtree(final_dir)

    # 下载
    version_type = "GPU" if gpu else "CPU (MT static CRT)"
    arch_info = f"{arch} (自动检测)" if args.arch == "auto" else arch
    print(f"下载 onnxruntime Windows {version_type} 版本 ({arch_info})...")
    print(f"版本: {ONNXRUNTIME_VERSION}")
    print(f"URL: {url}")

    try:
        download_with_progress(url, archive_path)
    except Exception as e:
        print(f"\n下载失败: {e}")
        print("\n如果网络不稳定，可以手动下载:")
        print(f"  {url}")
        print(f"然后解压到: {target_dir}")
        return 1

    # 解压
    print(f"解压到: {target_dir}")
    if is_mt:
        import tarfile
        with tarfile.open(archive_path, 'r:bz2') as tar:
            tar.extractall(target_dir)
    else:
        with zipfile.ZipFile(archive_path, 'r') as zip_ref:
            zip_ref.extractall(target_dir)

    # 删除压缩包
    os.remove(archive_path)

    print("\n完成!")
    print(f"库路径: {final_dir}/lib")
    print(f"头文件路径: {final_dir}/include")
    print(f"DLL路径: {final_dir}/lib")
    print("\n编译时请设置以下环境变量:")
    print(f"  ONNXRUNTIME_DIR={final_dir}")

    if gpu:
        print("\n注意: GPU 版本需要 CUDA 运行时库:")
        print("  - cudart.dll")
        print("  - cublas.dll")
        print("  - cublasLt.dll")

    return 0

if __name__ == "__main__":
    sys.exit(main())
