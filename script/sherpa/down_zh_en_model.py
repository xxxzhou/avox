#!/usr/bin/env python3
"""
下载中英双语流式 STT 模型 (sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20)
到 assets/models/stt/zh-en/
"""

import os
import sys
import urllib.request
import tarfile
import shutil

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
    target_dir = os.path.join(project_root, "assets", "models", "stt", "zh-en")

    # 模型压缩包
    model_name = "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
    tar_url = f"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/{model_name}.tar.bz2"
    tar_path = os.path.join(target_dir, f"{model_name}.tar.bz2")

    # 创建目标目录
    os.makedirs(target_dir, exist_ok=True)

    # 检查是否已解压
    expected_files = ["decoder-epoch-99-avg-1.int8.onnx", "encoder-epoch-99-avg-1.int8.onnx",
                      "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt"]
    all_exist = all(os.path.exists(os.path.join(target_dir, f)) for f in expected_files)

    if all_exist:
        print(f"[跳过] 模型文件已存在")
        print(f"模型目录: {target_dir}")
        return

    # 下载压缩包
    if not os.path.exists(tar_path):
        print(f"\n下载 {model_name}.tar.bz2...")
        print(f"URL: {tar_url}")
        download_with_progress(tar_url, tar_path)

    # 解压
    print(f"\n解压到: {target_dir}")
    with tarfile.open(tar_path, 'r:bz2') as tar:
        # 解压到临时目录
        extract_dir = os.path.join(target_dir, model_name)
        tar.extractall(target_dir)

        # 移动文件到目标目录
        if os.path.exists(extract_dir):
            for f in os.listdir(extract_dir):
                src = os.path.join(extract_dir, f)
                dst = os.path.join(target_dir, f)
                if os.path.isfile(src):
                    shutil.move(src, dst)
            os.rmdir(extract_dir)

    # 删除压缩包
    if os.path.exists(tar_path):
        os.remove(tar_path)

    print("\n完成!")
    print(f"模型目录: {target_dir}")

if __name__ == "__main__":
    main()