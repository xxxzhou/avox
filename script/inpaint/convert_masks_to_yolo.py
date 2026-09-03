#!/usr/bin/env python3
"""
将二值 mask 转换为 YOLO 分割标签格式
"""
import os
import numpy as np
from PIL import Image
import cv2
from pathlib import Path

def mask_to_yolo_seg(mask, image_shape):
    """
    将二值 mask 转换为 YOLO 分割格式的多边形坐标
    返回归一化的多边形顶点列表
    """
    h, w = image_shape

    # 查找轮廓
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    polygons = []
    for contour in contours:
        if len(contour) < 3:
            continue
        # 归一化坐标 (x/w, y/h)
        polygon = []
        for point in contour:
            x, y = point[0]
            polygon.append(f"{x/w:.6f}")
            polygon.append(f"{y/h:.6f}")
        polygons.append(" ".join(polygon))

    return polygons

def convert_dataset(data_dir, split='train'):
    images_dir = Path(data_dir) / 'images' / split
    masks_dir = Path(data_dir) / 'masks' / split
    labels_dir = Path(data_dir) / 'labels' / split

    labels_dir.mkdir(parents=True, exist_ok=True)

    image_files = sorted(images_dir.glob('*.jpg')) + sorted(images_dir.glob('*.png'))
    print(f"处理 {split} 集: {len(image_files)} 张图片")

    converted = 0
    empty = 0

    for img_path in image_files:
        # 对应的 mask 文件
        mask_path = masks_dir / (img_path.stem + '.png')
        if not mask_path.exists():
            print(f"警告: 找不到 mask {mask_path}")
            continue

        # 读取 mask
        mask = cv2.imread(str(mask_path), 0)  # 0 = grayscale
        if mask is None:
            continue

        # 读取图像获取尺寸
        img = cv2.imread(str(img_path), 1)  # 1 = color
        img_h, img_w = img.shape[:2]

        # 转换为 YOLO 格式
        polygons = mask_to_yolo_seg(mask, (img_h, img_w))

        # 写入标签文件
        label_path = labels_dir / (img_path.stem + '.txt')

        if polygons:
            with open(label_path, 'w') as f:
                # 类别 0 (水印), 多边形坐标
                for poly in polygons:
                    f.write(f"0 {poly}\n")
            converted += 1
        else:
            empty += 1
            # 空标签文件 (背景)
            label_path.touch()

    print(f"  转换完成: {converted} 个, 空标签: {empty}")

if __name__ == '__main__':
    import sys
    import platform

    # 默认数据目录
    if platform.system() == "Darwin":
        if os.path.exists("/Volumes/PSSD/work/data"):
            default_dir = "/Volumes/PSSD/work/data/inpaint_data"
        else:
            default_dir = os.path.expanduser("~/data/inpaint_data")
    else:
        default_dir = r"D:\Work\data\inpaint_data"

    data_dir = sys.argv[1] if len(sys.argv) > 1 else default_dir

    print("转换数据集为 YOLO 分割格式...")
    convert_dataset(data_dir, 'train')
    convert_dataset(data_dir, 'val')
    convert_dataset(data_dir, 'test')
    print("完成!")