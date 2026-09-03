#!/usr/bin/env python3
"""
YOLO26-Seg 水印分割模型下载脚本

下载 YOLO26-Seg 预训练模型用于水印分割训练
支持 ModelLevel: mini (n), base (m), high (x)
"""

import os
import urllib.request
from pathlib import Path


# 模型配置 (按 ModelLevel)
MODELS = {
    "mini": {
        "name": "yolo26n-seg.pt",
        "url": "https://huggingface.co/openvision/yolo26-n-seg/resolve/main/model.pt",
        "size": "~6MB",
        "desc": "YOLO26n-Seg 分割模型 (快速)",
    },
    "base": {
        "name": "yolo26m-seg.pt",
        "url": "https://huggingface.co/openvision/yolo26-m-seg/resolve/main/model.pt",
        "size": "~50MB",
        "desc": "YOLO26m-Seg 分割模型 (推荐)",
    },
    "high": {
        "name": "yolo26x-seg.pt",
        "url": "https://huggingface.co/openvision/yolo26-x-seg/resolve/main/model.pt",
        "size": "~85MB",
        "desc": "YOLO26x-Seg 分割模型 (高精度)",
    },
}


def get_script_dir():
    """获取脚本所在目录"""
    return Path(__file__).parent.resolve()


def get_models_dir():
    """获取模型存放目录"""
    script_dir = get_script_dir()
    return script_dir.parent.parent / "assets" / "models" / "inpaint"


def download_file(url: str, filepath: Path, desc: str):
    """下载文件，显示进度"""
    print(f"下载: {desc}")
    print(f"  URL: {url}")
    print(f"  保存到: {filepath}")

    def progress_hook(block_num, block_size, total_size):
        downloaded = block_num * block_size
        if total_size > 0:
            percent = min(100, downloaded * 100 / total_size)
            print(f"\r  进度: {percent:.1f}% ({downloaded / 1024 / 1024:.1f}MB)", end="")

    try:
        urllib.request.urlretrieve(url, filepath, progress_hook)
        print("\n  完成!")
        return True
    except Exception as e:
        print(f"\n  错误: {e}")
        # 如果下载失败，尝试使用 ultralytics 自动下载
        print("  尝试使用 ultralytics 自动下载...")
        try:
            from ultralytics import YOLO
            model = YOLO(filepath.name)  # 这会自动下载
            print("  完成!")
            return True
        except Exception as e2:
            print(f"  错误: {e2}")
            return False


def download_model(level: str = "base", force: bool = False):
    """下载模型"""
    if level not in MODELS:
        print(f"错误: 未知的模型等级 '{level}'")
        print(f"可用等级: {', '.join(MODELS.keys())}")
        return False

    models_dir = get_models_dir()
    models_dir.mkdir(parents=True, exist_ok=True)

    model = MODELS[level]
    filepath = models_dir / model["name"]

    print(f"模型目录: {models_dir}")
    print(f"ModelLevel: {level}")
    print()

    # 检查是否已存在
    if filepath.exists() and not force:
        print(f"已存在: {model['name']}")
        print(f"  使用 --force 强制重新下载")
        return True

    # 下载
    if download_file(model["url"], filepath, model["desc"]):
        print("\n完成!")
        return True
    return False


def download_all(force: bool = False):
    """下载所有模型"""
    for level in MODELS:
        print(f"\n{'='*50}")
        download_model(level, force)
    print(f"\n{'='*50}")
    print("全部下载完成!")


def main():
    import argparse

    parser = argparse.ArgumentParser(description="YOLO26-Seg 水印分割模型下载脚本")
    parser.add_argument(
        "level",
        nargs="?",
        default="base",
        choices=["mini", "base", "high", "all"],
        help="模型等级: mini/base/high/all (默认: base)",
    )
    parser.add_argument(
        "-f", "--force",
        action="store_true",
        help="强制重新下载"
    )

    args = parser.parse_args()

    if args.level == "all":
        download_all(args.force)
    else:
        download_model(args.level, args.force)


if __name__ == "__main__":
    main()
