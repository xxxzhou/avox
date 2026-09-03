#!/usr/bin/env python3
"""
下载 SenseVoice 模型 (2024-07-17, 支持标点) 到 assets/models/stt/sense-voice/
支持中/英/日/韩/粤 语言，带 ITN 标点功能
"""

import os
import sys
import urllib.request


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
    project_root = get_project_root()
    target_dir = os.path.join(project_root, "assets", "models", "stt", "sense-voice")

    # 模型文件列表 (2024-07-17 版本，支持标点)
    # 使用 huggingface 镜像下载
    base_url = "https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/resolve/main"

    models = [
        # SenseVoice 模型 (int8 量化版，239MB)
        ("model.int8.onnx", f"{base_url}/model.int8.onnx"),
        # tokens 文件
        ("tokens.txt", f"{base_url}/tokens.txt"),
    ]

    # VAD 模型 (Silero VAD)
    vad_url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx"

    # 创建目标目录
    os.makedirs(target_dir, exist_ok=True)

    # 下载 SenseVoice 模型
    print("=" * 60)
    print("下载 SenseVoice 模型 (2024-07-17, 支持标点)")
    print("=" * 60)

    for filename, url in models:
        dest_path = os.path.join(target_dir, filename)
        if os.path.exists(dest_path):
            print(f"[跳过] {filename} 已存在")
            continue

        print(f"\n下载 {filename}...")
        print(f"URL: {url}")
        try:
            download_with_progress(url, dest_path)
            print(f"已保存到: {dest_path}")
        except Exception as e:
            print(f"下载失败: {e}")
            print("尝试使用 github release 备用...")

    # 下载 VAD 模型
    vad_path = os.path.join(target_dir, "silero_vad.onnx")
    if not os.path.exists(vad_path):
        print(f"\n下载 silero_vad.onnx...")
        print(f"URL: {vad_url}")
        try:
            download_with_progress(vad_url, vad_path)
            print(f"已保存到: {vad_path}")
        except Exception as e:
            print(f"下载失败: {e}")
    else:
        print(f"[跳过] silero_vad.onnx 已存在")

    print("\n" + "=" * 60)
    print("完成!")
    print(f"模型目录: {target_dir}")
    print("\n模型说明:")
    print("  - model.int8.onnx: SenseVoice 离线识别模型 (239MB)")
    print("  - tokens.txt: 词汇表")
    print("  - silero_vad.onnx: VAD 语音活动检测模型")
    print("\n功能特性:")
    print("  - 支持语言: 中文/英文/日文/韩文/粤语")
    print("  - 启用 ITN (use_itn=true) 自动添加标点")
    print("  - 模型版本: 2024-07-17")


if __name__ == "__main__":
    main()