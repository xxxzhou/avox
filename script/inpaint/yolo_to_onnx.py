#!/usr/bin/env python3
"""
YOLO 模型转 ONNX 格式脚本

将 YOLO PyTorch 模型 (.pt) 转换为 ONNX 格式 (.onnx)
用于 C++ ONNX Runtime 推理。
"""

import os
import argparse
from pathlib import Path


def get_script_dir():
    """获取脚本所在目录"""
    return Path(__file__).parent.resolve()


def get_models_dir():
    """获取模型存放目录"""
    script_dir = get_script_dir()
    return script_dir.parent.parent / "assets" / "models" / "inpaint"


def convert_yolo_to_onnx(
    input_path: str,
    output_path: str = None,
    img_size: int = 640,
    opset: int = 12,
    simplify: bool = True,
    dynamic: bool = False
):
    """
    将 YOLO 模型转换为 ONNX 格式

    Args:
        input_path: 输入的 .pt 模型路径
        output_path: 输出的 .onnx 模型路径 (默认与输入同目录同名)
        img_size: 输入图像尺寸
        opset: ONNX opset 版本
        simplify: 是否简化 ONNX 模型
        dynamic: 是否使用动态输入尺寸
    """
    try:
        import torch
        from ultralytics import YOLO
    except ImportError as e:
        print(f"缺少依赖: {e}")
        print("请安装: pip install torch ultralytics onnx onnxruntime")
        return False

    input_path = Path(input_path)
    if not input_path.exists():
        print(f"错误: 输入文件不存在: {input_path}")
        return False

    # 默认输出路径
    if output_path is None:
        output_path = input_path.with_suffix(".onnx")
    else:
        output_path = Path(output_path)

    print(f"输入: {input_path}")
    print(f"输出: {output_path}")
    print(f"图像尺寸: {img_size}")
    print(f"Opset: {opset}")
    print(f"简化: {simplify}")
    print(f"动态尺寸: {dynamic}")
    print()

    # 加载模型
    print("加载 YOLO 模型...")
    model = YOLO(str(input_path))

    # 导出为 ONNX
    print("导出为 ONNX 格式...")

    # 动态轴配置
    dynamic_axes = None
    if dynamic:
        dynamic_axes = {
            0: "batch",
            2: "height",
            3: "width"
        }

    try:
        model.export(
            format="onnx",
            imgsz=img_size,
            opset=opset,
            simplify=False,  # 禁用 simplify 避免 onnx 依赖问题
            dynamic=dynamic
        )
        print("导出成功!")
    except Exception as e:
        print(f"导出失败: {e}")
        return False

    # 移动到目标位置
    default_output = input_path.with_suffix(".onnx")
    if default_output != output_path and default_output.exists():
        import shutil
        shutil.move(str(default_output), str(output_path))
        print(f"已移动到: {output_path}")

    # 检查文件
    if output_path.exists():
        size_mb = output_path.stat().st_size / 1024 / 1024
        print(f"转换完成! 文件大小: {size_mb:.1f} MB")
        return True
    else:
        print("转换失败: 输出文件不存在")
        return False


def list_yolo_models():
    """列出 models 目录下的 YOLO 模型"""
    models_dir = get_models_dir()

    print(f"模型目录: {models_dir}")
    print()

    if not models_dir.exists():
        print("模型目录不存在")
        return

    # 查找 .pt 文件
    pt_files = list(models_dir.glob("*.pt"))

    if not pt_files:
        print("未找到 YOLO 模型 (.pt 文件)")
        return

    print("可转换的 YOLO 模型:")
    for f in pt_files:
        size_mb = f.stat().st_size / 1024 / 1024
        print(f"  {f.name:30} {size_mb:.1f} MB")


def main():
    parser = argparse.ArgumentParser(
        description="YOLO 模型转 ONNX 格式",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 转换指定模型
  python yolo_to_onnx.py yolo11x_watermark.pt

  # 指定输出路径
  python yolo_to_onnx.py yolo11x_watermark.pt -o yolo11x_watermark.onnx

  # 指定图像尺寸
  python yolo_to_onnx.py yolo11x_watermark.pt -s 1280

  # 使用动态 batch
  python yolo_to_onnx.py yolo11x_watermark.pt --dynamic

  # 列出可转换的模型
  python yolo_to_onnx.py -l
        """
    )

    parser.add_argument(
        "input",
        nargs="?",
        help="输入的 YOLO .pt 模型路径"
    )
    parser.add_argument(
        "-o", "--output",
        help="输出的 .onnx 模型路径"
    )
    parser.add_argument(
        "-s", "--size",
        type=int,
        default=640,
        help="输入图像尺寸 (默认: 640)"
    )
    parser.add_argument(
        "--opset",
        type=int,
        default=12,
        help="ONNX opset 版本 (默认: 12)"
    )
    parser.add_argument(
        "--no-simplify",
        action="store_true",
        help="不简化 ONNX 模型"
    )
    parser.add_argument(
        "--dynamic",
        action="store_true",
        help="使用动态输入尺寸 (batch, height, width)"
    )
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="列出可转换的 YOLO 模型"
    )

    args = parser.parse_args()

    if args.list:
        list_yolo_models()
        return

    if not args.input:
        parser.print_help()
        return

    # 如果只给文件名，补充完整路径
    input_path = Path(args.input)
    if not input_path.exists():
        # 尝试在 models 目录查找
        models_dir = get_models_dir()
        full_path = models_dir / args.input
        if full_path.exists():
            input_path = full_path
        else:
            print(f"错误: 文件不存在: {args.input}")
            print(f"可在此目录查找: {models_dir}")
            list_yolo_models()
            return

    convert_yolo_to_onnx(
        str(input_path),
        args.output,
        args.size,
        args.opset,
        not args.no_simplify,
        args.dynamic
    )


if __name__ == "__main__":
    main()