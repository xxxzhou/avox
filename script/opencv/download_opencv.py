#!/usr/bin/env python3
"""
下载 OpenCV 库到 library/{platform}/opencv 目录

支持:
- Windows (x64)
- Android
- iOS
- Linux
"""

import os
import sys
import urllib.request
import zipfile
import tarfile
import argparse
import platform
import shutil
import re

# 配置
OPENCV_VERSION = "4.13.0"

# 下载地址
URLS = {
    "windows": {
        "x64": f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-windows.exe",
        "x86": f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-windows.exe",
    },
    "android": f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-android-sdk.zip",
    "ios": f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-ios-framework.zip",
    "linux": {
        "x64": f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-linux-x64.sh",
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
        return "x64"


def detect_platform():
    """检测当前平台"""
    if sys.platform == "win32" or os.name == "nt":
        return "windows"
    elif sys.platform == "darwin":
        return "ios"
    elif "linux" in sys.platform:
        return "linux"
    else:
        return "linux"


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


def download_github_release_file(url, dest_path, base_name):
    """从 GitHub Releases 下载文件

    GitHub Releases 不直接支持 .exe 和 .sh 文件的直链，
    需要重定向到 cdn 等镜像
    """
    try:
        # 尝试直接下载
        download_with_progress(url, dest_path)
        return True
    except Exception as e:
        print(f"\n直接下载失败: {e}")
        return False


def check_existing(target_dir, version):
    """检查是否已存在"""
    if os.path.exists(target_dir):
        # 检查版本
        readme_path = os.path.join(target_dir, "README.md.txt")
        if os.path.exists(readme_path):
            with open(readme_path, 'r') as f:
                content = f.read()
                if version in content:
                    return True
        # 如果目录存在但版本不匹配，删除
        print(f"发现已存在的 OpenCV 目录，但版本不匹配，将重新下载")
        shutil.rmtree(target_dir)
    return False


def main():
    # 自动检测
    default_platform = detect_platform()
    default_arch = detect_arch()

    parser = argparse.ArgumentParser(description="下载 OpenCV 库")
    parser.add_argument("--platform", choices=["windows", "android", "ios", "linux"],
                       default=default_platform, help=f"目标平台 (默认: {default_platform})")
    parser.add_argument("--arch", choices=["x64", "x86", "arm64", "arm64-v8a", "armeabi-v7a"],
                       default=default_arch, help=f"CPU 架构 (默认: {default_arch})")
    parser.add_argument("--force", action="store_true", help="强制重新下载")
    parser.add_argument("--skip-extract", action="store_true", help="只下载不解压")
    args = parser.parse_args()

    project_root = get_project_root()
    target_dir = os.path.join(project_root, "3rdparty", "library", args.platform, "opencv")
    os.makedirs(target_dir, exist_ok=True)

    # 下载
    if args.platform == "windows":
        url = URLS["windows"].get(args.arch)
        if not url:
            print(f"错误: 不支持的架构 {args.arch}")
            return 1

        # Windows 使用 exe 安装包，实际上是 7z 自解压
        # 这里我们直接下载预编译的 build
        # 由于 Windows exe 安装包需要交互，我们尝试使用预编译版本
        print(f"警告: Windows 建议使用预编译版本")
        print(f"可以从以下地址手动下载:")
        print(f"  https://github.com/opencv/opencv/releases/tag/{OPENCV_VERSION}")
        print(f"然后解压到: {target_dir}")

        # 尝试从 SourceForge 镜像下载
        sourceforge_url = f"https://sourceforge.net/projects/opencvlibrary/files/{OPENCV_VERSION}/opencv-{OPENCV_VERSION}-windows.exe/download"
        zip_name = f"opencv-{OPENCV_VERSION}-windows.exe"
        zip_path = os.path.join(target_dir, zip_name)

        if os.path.exists(zip_path) and not args.force:
            print(f"文件已存在: {zip_path}")
        else:
            print(f"从 SourceForge 下载...")
            try:
                download_with_progress(sourceforge_url, zip_path)
            except Exception as e:
                print(f"\n下载失败: {e}")
                print(f"\n请手动下载以下文件并放到 {target_dir}:")
                print(f"  opencv-{OPENCV_VERSION}-windows.exe")
                return 1

        if not args.skip_extract:
            print(f"解压到: {target_dir}")
            # exe 是 7z 自解压格式，需要 7z 解压
            # 这里假设用户已经手动解压
            extract_dir = os.path.join(target_dir, f"opencv-{OPENCV_VERSION}-windows")
            if os.path.exists(extract_dir):
                print(f"已解压到: {extract_dir}")
            else:
                print(f"请手动解压: {zip_path}")

    elif args.platform == "android":
        url = URLS["android"]
        zip_name = f"opencv-{OPENCV_VERSION}-android-sdk.zip"
        zip_path = os.path.join(target_dir, zip_name)

        if os.path.exists(zip_path) and not args.force:
            print(f"文件已存在: {zip_path}")
            if not args.skip_extract:
                print("跳过下载")
        else:
            print(f"下载 OpenCV Android SDK {OPENCV_VERSION}...")
            try:
                download_with_progress(url, zip_path)
            except Exception as e:
                print(f"\n下载失败: {e}")
                return 1

        if not args.skip_extract:
            print(f"解压到: {target_dir}")
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(target_dir)
            os.remove(zip_path)

    elif args.platform == "ios":
        url = URLS["ios"]
        zip_name = f"opencv-{OPENCV_VERSION}-ios-framework.zip"
        zip_path = os.path.join(target_dir, zip_name)

        if os.path.exists(zip_path) and not args.force:
            print(f"文件已存在: {zip_path}")
            if not args.skip_extract:
                print("跳过下载")
        else:
            print(f"下载 OpenCV iOS Framework {OPENCV_VERSION}...")
            try:
                download_with_progress(url, zip_path)
            except Exception as e:
                print(f"\n下载失败: {e}")
                return 1

        if not args.skip_extract:
            print(f"解压到: {target_dir}")
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(target_dir)
            os.remove(zip_path)

    elif args.platform == "linux":
        url = URLS["linux"].get(args.arch)
        if not url:
            print(f"错误: 不支持的架构 {args.arch}")
            return 1

        # Linux 使用 shell 脚本
        sh_name = f"opencv-{OPENCV_VERSION}-linux-x64.sh"
        sh_path = os.path.join(target_dir, sh_name)

        if os.path.exists(sh_path) and not args.force:
            print(f"文件已存在: {sh_path}")
            if not args.skip_extract:
                print("跳过下载")
        else:
            print(f"下载 OpenCV Linux {OPENCV_VERSION}...")
            try:
                download_with_progress(url, sh_path)
            except Exception as e:
                print(f"\n下载失败: {e}")
                return 1

        if not args.skip_extract:
            print(f"解压到: {target_dir}")
            os.chmod(sh_path, 0o755)
            import subprocess
            subprocess.run([sh_path, "--prefix", target_dir, "--exclude-subdir"], check=True)
            os.remove(sh_path)

    print("\n完成!")
    print(f"库路径: {target_dir}")
    print("\n编译时请设置以下环境变量:")
    print(f"  OpenCV_DIR={target_dir}/opencv-{OPENCV_VERSION}-windows/build")
    if args.platform == "windows":
        print(f"  (或者使用 cmake/FindOpenCV.cmake 会自动搜索)")

    return 0


if __name__ == "__main__":
    sys.exit(main())