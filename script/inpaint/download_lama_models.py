#!/usr/bin/env python3
"""
水印去除模型下载脚本

只需要下载 LaMa 修复模型，检测使用自动算法（无需模型）

模型来源: Carve/LaMa-ONNX (https://huggingface.co/Carve/LaMa-ONNX)
输入节点: image [N,3,512,512], mask [N,1,512,512]
输出节点: output [N,3,H,W]
"""

import os
import urllib.request
import hashlib
from pathlib import Path

# 模型配置
MODELS = {
    # LaMa 修复模型 (必需) - Carve/LaMa-ONNX fp32 版本
    "lama_base.onnx": {
        "url": "https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx",
        "size": "~198M",
        "md5": None,
        "desc": "LaMa 图像修复模型 (标准, 双输入, fp32)",
        "required": True
    },
    "lama_mini.onnx": {
        "url": "https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx",
        "size": "~198M",
        "md5": None,
        "desc": "LaMa 图像修复模型 (轻量, 同上)",
        "required": False
    },
    "lama_high.onnx": {
        "url": "https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx",
        "size": "~198M",
        "md5": None,
        "desc": "LaMa 图像修复模型 (高质量, 同上)",
        "required": False
    },

    # YOLO 检测模型 (可选，仅 YOLO 模式需要)
    "yolo_watermark_base.onnx": {
        "url": "",  # 需要自己训练
        "size": "~50M",
        "md5": None,
        "desc": "YOLO 水印检测模型 (可选)",
        "required": False
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
        return False

def verify_md5(filepath: Path, expected_md5: str) -> bool:
    """验证文件 MD5"""
    if not expected_md5:
        return True

    print(f"  验证 MD5...")
    with open(filepath, "rb") as f:
        actual_md5 = hashlib.md5(f.read()).hexdigest()

    if actual_md5 == expected_md5:
        print(f"  MD5 匹配")
        return True
    else:
        print(f"  MD5 不匹配!")
        print(f"    期望: {expected_md5}")
        print(f"    实际: {actual_md5}")
        return False

def download_models(model_names=None, force=False):
    """
    下载模型

    Args:
        model_names: 要下载的模型名称列表，None 表示全部下载
        force: 是否强制重新下载
    """
    models_dir = get_models_dir()
    models_dir.mkdir(parents=True, exist_ok=True)

    print(f"模型目录: {models_dir}")
    print()

    if model_names is None:
        model_names = list(MODELS.keys())

    success_count = 0
    for name in model_names:
        if name not in MODELS:
            print(f"未知模型: {name}")
            continue

        info = MODELS[name]
        filepath = models_dir / name

        # 检查是否已存在
        if filepath.exists() and not force:
            print(f"已存在: {name} ({info['desc']})")
            if info["md5"]:
                if verify_md5(filepath, info["md5"]):
                    success_count += 1
                    continue
                else:
                    print("  重新下载...")
            else:
                success_count += 1
                continue

        # 下载
        if download_file(info["url"], filepath, f"{name} ({info['desc']})"):
            if info["md5"]:
                if verify_md5(filepath, info["md5"]):
                    success_count += 1
                else:
                    filepath.unlink()
            else:
                success_count += 1

    print()
    print(f"完成: {success_count}/{len(model_names)} 个模型")

def main():
    import argparse

    parser = argparse.ArgumentParser(description="水印去除模型下载脚本")
    parser.add_argument(
        "models",
        nargs="*",
        help="要下载的模型名称 (不指定则下载全部)"
    )
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="列出所有可用模型"
    )
    parser.add_argument(
        "-f", "--force",
        action="store_true",
        help="强制重新下载"
    )

    args = parser.parse_args()

    if args.list:
        print("可用模型:")
        for name, info in MODELS.items():
            print(f"  {name:25} {info['size']:6} {info['desc']}")
        return

    if args.models:
        download_models(args.models, args.force)
    else:
        # 默认只下载 LaMa 修复模型 (检测不需要模型)
        print("下载默认模型 (LaMa 修复模型)...")
        print("注意: 自动检测模式不需要下载检测模型")
        download_models(["lama_base.onnx"], args.force)

if __name__ == "__main__":
    main()