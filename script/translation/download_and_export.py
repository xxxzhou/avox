#!/usr/bin/env python3
"""
下载 MarianMT 模型并导出为 INT8 量化 ONNX
用法: python download_and_export.py
"""
import os
import sys

os.environ["HF_ENDPOINT"] = "https://hf-mirror.com"
os.environ["TOKENIZERS_PARALLELISM"] = "false"

MODEL_NAME = "shun89/opus-mt-ja-zh"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
MODEL_DIR = os.path.join(PROJECT_ROOT, "assets", "models", "translation", "opus-mt-ja-zh")


def need_download():
    return not os.path.exists(os.path.join(MODEL_DIR, "source.spm"))


def need_quantize():
    return not os.path.exists(os.path.join(MODEL_DIR, "encoder_model_int8.onnx"))


def download_model():
    """从 HuggingFace 下载模型"""
    from transformers import MarianMTModel, MarianTokenizer

    print(f"下载模型: {MODEL_NAME}")
    os.makedirs(MODEL_DIR, exist_ok=True)

    print("下载 tokenizer...")
    tokenizer = MarianTokenizer.from_pretrained(MODEL_NAME)
    tokenizer.save_pretrained(MODEL_DIR)

    print("下载模型权重...")
    model = MarianMTModel.from_pretrained(MODEL_NAME)
    model.save_pretrained(MODEL_DIR)
    print("模型下载完成!")
    return True


def export_onnx():
    """导出为 ONNX 格式"""
    print("\n导出 ONNX 模型...")

    from optimum.exporters.onnx import main_export

    # 清理旧的 ONNX 文件
    for f in ["encoder_model.onnx", "decoder_model.onnx",
              "encoder_model_int8.onnx", "decoder_model_int8.onnx"]:
        path = os.path.join(MODEL_DIR, f)
        if os.path.exists(path):
            os.remove(path)
            print(f"删除旧文件: {f}")

    main_export(
        model_name_or_path=MODEL_NAME,
        output=MODEL_DIR,
        task="translation",
        opset=14,
        no_post_process=False,
    )
    print("ONNX 导出完成!")
    return True


def quantize_onnx():
    """使用 WSL 量化 ONNX 模型为 INT8"""
    print("\nINT8 量化 (使用 WSL)...")

    def to_wsl_path(win_path):
        # Windows 路径转 WSL 挂载路径: D:\foo\bar -> /mnt/d/foo/bar
        p = os.path.abspath(win_path).replace("\\", "/")
        return "/mnt/" + p[0].lower() + p[2:]

    model_dir_wsl = to_wsl_path(MODEL_DIR)

    # 创建量化脚本
    script_path = os.path.join(SCRIPT_DIR, "wsl_quantize.py")
    wsl_script_path = to_wsl_path(script_path)
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(f'''
import os
os.environ["TOKENIZERS_PARALLELISM"] = "false"
from onnxruntime.quantization import quantize_dynamic, QuantType

MODEL_DIR = "{model_dir_wsl}"

for name, out in [
    ("encoder_model.onnx", "encoder_model_int8.onnx"),
    ("decoder_model.onnx", "decoder_model_int8.onnx"),
]:
    inp = os.path.join(MODEL_DIR, name)
    outp = os.path.join(MODEL_DIR, out)
    if not os.path.exists(inp):
        print("跳过: " + name + " 不存在")
        continue
    print("量化 " + name + "...")
    quantize_dynamic(model_input=inp, model_output=outp, weight_type=QuantType.QInt8)
    os.remove(inp)
    print("完成: " + out)

print("量化完成!")
''')

    # 在 WSL 中运行
    cmd = f'wsl -d Ubuntu -e bash -c "export PATH=$HOME/.local/bin:$PATH; python3 {wsl_script_path}"'
    result = os.system(cmd)

    # 清理脚本
    os.remove(script_path)

    if result != 0:
        print("量化失败!")
        return False

    return True


def show_model_info():
    """显示模型信息"""
    print("\n模型文件:")
    total = 0
    for f in sorted(os.listdir(MODEL_DIR)):
        if f.endswith((".onnx", ".spm", ".json")):
            path = os.path.join(MODEL_DIR, f)
            size = os.path.getsize(path) / 1024 / 1024
            total += size
            print(f"  {f}: {size:.1f} MB")
    print(f"  总计: {total:.1f} MB")


def main():
    os.makedirs(MODEL_DIR, exist_ok=True)

    if need_download():
        if not download_model():
            print("下载失败!", file=sys.stderr)
            sys.exit(1)

    if need_quantize():
        # 导出 ONNX (如果需要)
        if not os.path.exists(os.path.join(MODEL_DIR, "encoder_model.onnx")):
            if not export_onnx():
                print("导出失败!", file=sys.stderr)
                sys.exit(1)

        # 量化
        if not quantize_onnx():
            print("量化失败!", file=sys.stderr)
            sys.exit(1)
    else:
        print("\n模型已存在，跳过下载和量化")

    show_model_info()
    print("\n完成!")


if __name__ == "__main__":
    main()