#!/usr/bin/env python3
"""
对比真实mask vs YOLO检测mask的修复效果

功能:
1. 自动清空result目录
2. 随机选10张有真实mask的图片
3. 分别用YOLO检测mask和真实mask进行LaMa修复
4. 生成六合一组合图: 上排(YOLO) 下排(GT)
"""

import os
import sys
import random
import shutil
import subprocess
import time
import argparse
from datetime import datetime
import cv2
import numpy as np

# ============================================================================
# 配置
# ============================================================================
import platform

if platform.system() == "Darwin":
    if os.path.exists("/Volumes/PSSD/work/data"):
        BASE_DIR = "/Volumes/PSSD/work/data/inpaint_data"
    else:
        BASE_DIR = os.path.expanduser("~/data/inpaint_data")
else:
    BASE_DIR = r"D:\Work\data\inpaint_data"

IMAGES_VAL = os.path.join(BASE_DIR, "images", "test")
MASKS_VAL = os.path.join(BASE_DIR, "masks", "test")
RESULT_DIR = os.path.join(BASE_DIR, "result_lama_vs_aotgan")  # 单独的对比结果目录
INPAINT_TEST = r"D:\Work\github\avplay\build\windows\avplay\install\AMD64\Release\inpainttest.exe"
NUM_IMAGES = 10

# 模型类型: 0=lama, 1=aotgan (全局)
INPAINT_MODEL_TYPE = 0

# 对比模式: None=原来的GT vs YOLO, "lama_vs_aotgan"=LaMa vs AOTGAN
COMPARE_MODE = None


def run_inpaint_with_mask(img_path, mask, output_path):
    """直接用mask运行修复（不经过YOLO检测）"""
    start_time = time.time()

    # 创建临时目录
    temp_dir = os.path.join(RESULT_DIR, "_temp")
    os.makedirs(temp_dir, exist_ok=True)

    # 保存mask到临时文件
    temp_mask = os.path.join(temp_dir, "gt_mask.png")
    cv2.imwrite(temp_mask, mask)

    # 使用 -k 参数传递 mask 文件, -t 指定模型类型
    cmd = [INPAINT_TEST, "-i", img_path, "-k", temp_mask, "-t", str(INPAINT_MODEL_TYPE), "-v"]


def find_images_with_masks():
    """找到有真实mask的图片"""
    images = []
    if os.path.exists(IMAGES_VAL) and os.path.exists(MASKS_VAL):
        for f in os.listdir(IMAGES_VAL):
            if f.endswith(('.jpg', '.png', '.jpeg')):
                mask_name = os.path.splitext(f)[0] + ".png"
                mask_path = os.path.join(MASKS_VAL, mask_name)
                if os.path.exists(mask_path):
                    images.append(f)
    return images


def run_inpaint_with_mask(img_path, mask, output_path, model_type=None):
    """直接用mask运行修复（不经过YOLO检测）"""
    if model_type is None:
        model_type = INPAINT_MODEL_TYPE

    start_time = time.time()

    # 创建临时目录
    temp_dir = os.path.join(RESULT_DIR, "_temp")
    os.makedirs(temp_dir, exist_ok=True)

    # 保存mask到临时文件
    temp_mask = os.path.join(temp_dir, "gt_mask.png")
    cv2.imwrite(temp_mask, mask)

    # 使用 -k 参数传递 mask 文件, -t 指定模型类型
    cmd = [INPAINT_TEST, "-i", img_path, "-k", temp_mask, "-t", str(model_type), "-v"]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except:
        pass

    # 读取结果
    base = os.path.splitext(img_path)[0]
    # 命令行工具生成 _inpainted.jpg 或 _clean.png
    result_path_jpg = base + "_inpainted.jpg"
    result_path_png = base + "_clean.png"

    result_path = result_path_jpg if os.path.exists(result_path_jpg) else result_path_png

    elapsed = time.time() - start_time

    if os.path.exists(result_path):
        result_img = cv2.imread(result_path)
        # 删除结果文件
        try:
            os.remove(result_path)
        except:
            pass
        shutil.rmtree(temp_dir, ignore_errors=True)
        return result_img, elapsed

    shutil.rmtree(temp_dir, ignore_errors=True)
    return None, elapsed


def run_yolo_detect_and_inpaint(img_path):
    """运行YOLO检测 + 修复"""
    start_time = time.time()
    detect_time = 0
    inpaint_time = 0

    # 第一步: 检测并保存 mask (使用 -m 参数)
    detect_start = time.time()
    cmd = [INPAINT_TEST, "-i", img_path, "-l", "1", "-m", "-v"]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except:
        pass
    detect_time = time.time() - detect_start

    # 第二步: 读取 YOLO mask (已在 C++ 中完成膨胀处理)
    base = os.path.splitext(img_path)[0]
    mask_path = base + "_mask.png"

    yolo_mask = None
    if os.path.exists(mask_path):
        yolo_mask = cv2.imread(mask_path, cv2.IMREAD_GRAYSCALE)

    # 第三步: 使用检测到的 mask 进行修复
    inpaint_start = time.time()
    if yolo_mask is not None:
        cmd = [INPAINT_TEST, "-i", img_path, "-k", mask_path, "-t", str(INPAINT_MODEL_TYPE), "-v"]
        try:
            subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except:
            pass
    inpaint_time = time.time() - inpaint_start

    # 读取结果
    result_path = base + "_inpainted.jpg"

    result_img = cv2.imread(result_path) if os.path.exists(result_path) else None

    total_time = time.time() - start_time
    return yolo_mask, result_img, detect_time, inpaint_time, total_time


def create_combined_image(orig, yolo_mask, yolo_result, gt_mask, gt_result):
    """
    生成六合一组合图:
    上排: 原图 | YOLO Mask | YOLO Result
    下排: 原图 | GT Mask | GT Result
    """
    h, w = orig.shape[:2]

    # 创建黑白mask图
    def mask_to_bw(mask):
        if mask is None:
            return np.zeros((h, w), dtype=np.uint8)
        bw = np.zeros_like(mask)
        bw[mask > 0] = 255
        return bw

    # 转换为3通道
    def to_3ch(img):
        if img is None:
            return np.zeros((h, w, 3), dtype=np.uint8)
        if len(img.shape) == 2:
            return cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        return img

    # 统一高度缩放
    def resize(img, target_h):
        if img is None:
            return np.zeros((target_h, int(target_h * 1.5), 3), dtype=np.uint8)
        ratio = target_h / img.shape[0]
        new_w = int(img.shape[1] * ratio)
        return cv2.resize(img, (new_w, target_h))

    # 添加标签
    def add_label(img, text):
        canvas = np.zeros((img.shape[0] + 35, img.shape[1], 3), dtype=np.uint8)
        canvas[:img.shape[0], :] = img
        cv2.putText(canvas, text, (10, img.shape[0] + 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        return canvas

    # 上排: YOLO
    top_parts = [
        add_label(resize(orig, h), "Original"),
        add_label(resize(to_3ch(mask_to_bw(yolo_mask)), h), "YOLO Mask"),
        add_label(resize(yolo_result if yolo_result is not None else orig, h), "YOLO Result"),
    ]

    # 下排: GT
    bottom_parts = [
        add_label(resize(orig, h), "Original"),
        add_label(resize(to_3ch(mask_to_bw(gt_mask)), h), "GT Mask"),
        add_label(resize(gt_result if gt_result is not None else orig, h), "GT Result"),
    ]

    # 拼接
    top_row = np.hstack(top_parts)
    bottom_row = np.hstack(bottom_parts)

    # 统一宽度
    max_w = max(top_row.shape[1], bottom_row.shape[1])
    def pad_width(img, target_w):
        if img.shape[1] < target_w:
            pad = np.zeros((img.shape[0], target_w - img.shape[1], 3), dtype=np.uint8)
            return np.hstack([img, pad])
        return img

    top_row = pad_width(top_row, max_w)
    bottom_row = pad_width(bottom_row, max_w)

    return np.vstack([top_row, bottom_row])


def main():
    global INPAINT_MODEL_TYPE

    # 解析命令行参数
    parser = argparse.ArgumentParser(description="对比真实mask vs YOLO检测mask的修复效果")
    parser.add_argument("-t", "--type", type=int, default=0, choices=[0, 1],
                        help="修复模型类型: 0=LaMa (默认), 1=AOT-GAN")
    parser.add_argument("-n", "--num", type=int, default=10,
                        help="测试图片数量 (默认: 10)")
    parser.add_argument("--compare", action="store_true",
                        help="对比模式: LaMa vs AOT-GAN 对比 (需要先用GT mask修复)")
    args = parser.parse_args()

    INPAINT_MODEL_TYPE = args.type
    model_name = "LaMa" if args.type == 0 else "AOT-GAN"

    if args.compare:
        COMPARE_MODE = "lama_vs_aotgan"
        run_compare_mode()
        return

    print("=" * 60)
    print(f"真实mask vs YOLO mask 对比测试 (模型: {model_name})")
    print(f"时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 60)

    # 清空result目录
    print("\n清空result目录...")
    if os.path.exists(RESULT_DIR):
        shutil.rmtree(RESULT_DIR)
    os.makedirs(RESULT_DIR)
    print("result目录已清空")

    # 找有真实mask的图片
    images = find_images_with_masks()
    if not images:
        print("错误: 没有找到有真实mask的图片")
        return

    # 随机选择
    random.seed()
    selected = random.sample(images, min(args.num, len(images)))
    print(f"\n随机选择 {len(selected)} 张图片")

    # 时间统计
    total_yolo_detect = 0
    total_yolo_inpaint = 0
    total_yolo_time = 0
    total_gt_time = 0
    times_detail = []

    # 处理每张图片
    for i, img_name in enumerate(selected):
        base = os.path.splitext(img_name)[0]
        print(f"\n[{i+1}/{len(selected)}] 处理: {img_name}")

        img_path = os.path.join(IMAGES_VAL, img_name)
        gt_mask_path = os.path.join(MASKS_VAL, base + ".png")

        # 读取原图
        orig = cv2.imread(img_path)
        if orig is None:
            print(f"  无法读取图片")
            continue

        # --- YOLO mask ---
        print("  运行 YOLO 检测...")
        yolo_mask, yolo_result, detect_t, inpaint_t, yolo_t = run_yolo_detect_and_inpaint(img_path)
        total_yolo_detect += detect_t
        total_yolo_inpaint += inpaint_t
        total_yolo_time += yolo_t
        print(f"  YOLO mask: {'有' if yolo_mask is not None else '无'} (检测: {detect_t:.2f}s, 修复: {inpaint_t:.2f}s)")

        # --- GT mask ---
        print("  准备真实mask...")
        gt_mask = cv2.imread(gt_mask_path, cv2.IMREAD_GRAYSCALE)

        gt_t = 0
        if gt_mask is not None:
            # 二值化 + 膨胀 (与 C++ 一致: 16 像素膨胀)
            _, gt_binary = cv2.threshold(gt_mask, 127, 255, cv2.THRESH_BINARY)
            kernel = np.ones((33, 33), np.uint8)  # 16px 半径 = 33x33 核
            gt_dilated = cv2.dilate(gt_binary, kernel, iterations=1)

            # 用GT mask修复
            print("  用GT mask运行LaMa修复...")
            gt_result, gt_t = run_inpaint_with_mask(img_path, gt_dilated, None)
            total_gt_time += gt_t
            print(f"  GT修复: {'完成' if gt_result is not None else '失败'} ({gt_t:.2f}s)")
        else:
            gt_dilated = None
            gt_result = None

        times_detail.append({
            'name': img_name,
            'yolo_detect': detect_t,
            'yolo_inpaint': inpaint_t,
            'yolo_total': yolo_t,
            'gt_total': gt_t
        })

        # 生成组合图
        print("  生成组合图...")
        combined = create_combined_image(orig, yolo_mask, yolo_result, gt_dilated, gt_result)
        output_path = os.path.join(RESULT_DIR, base + "_compare.jpg")
        cv2.imwrite(output_path, combined)
        print(f"  完成: {base}_compare.jpg")

    # 显示结果
    num_processed = len(times_detail)
    print("\n" + "=" * 60)
    print("测试完成!")
    print(f"结果目录: {RESULT_DIR}")

    # 时间统计
    print("\n" + "-" * 60)
    print("耗时统计:")
    print("-" * 60)
    print(f"{'图片':<40} {'YOLO检测':>10} {'YOLO修复':>10} {'YOLO总':>10} {'GT修复':>10}")
    print("-" * 60)
    for t in times_detail:
        print(f"{t['name']:<40} {t['yolo_detect']:>10.2f}s {t['yolo_inpaint']:>10.2f}s {t['yolo_total']:>10.2f}s {t['gt_total']:>10.2f}s")
    print("-" * 60)

    if num_processed > 0:
        avg_detect = total_yolo_detect / num_processed
        avg_inpaint = total_yolo_inpaint / num_processed
        avg_yolo = total_yolo_time / num_processed
        avg_gt = total_gt_time / num_processed
        print(f"{'平均耗时':<40} {avg_detect:>10.2f}s {avg_inpaint:>10.2f}s {avg_yolo:>10.2f}s {avg_gt:>10.2f}s")
        print(f"{'总计耗时':<40} {total_yolo_detect:>10.2f}s {total_yolo_inpaint:>10.2f}s {total_yolo_time:>10.2f}s {total_gt_time:>10.2f}s")

    print("\n生成的组合图:")
    for f in sorted(os.listdir(RESULT_DIR)):
        if f.endswith("_compare.jpg"):
            size = os.path.getsize(os.path.join(RESULT_DIR, f)) / 1024
            print(f"  {f:40} {size:.1f} KB")
    print("=" * 60)


def run_lama_vs_aotgan(img_path, mask, output_path):
    """分别用 LaMa 和 AOT-GAN 修复，返回两个结果"""
    start_time = time.time()

    # 创建临时目录
    temp_dir = os.path.join(RESULT_DIR, "_temp")
    os.makedirs(temp_dir, exist_ok=True)

    # 保存mask到临时文件
    temp_mask = os.path.join(temp_dir, "gt_mask.png")
    cv2.imwrite(temp_mask, mask)

    # 1. 用 LaMa 修复
    cmd_lama = [INPAINT_TEST, "-i", img_path, "-k", temp_mask, "-t", "0", "-v"]
    try:
        subprocess.run(cmd_lama, capture_output=True, text=True, timeout=120)
    except:
        pass

    base = os.path.splitext(img_path)[0]
    result_path_jpg = base + "_inpainted.jpg"
    result_path_png = base + "_clean.png"

    lama_result_path = result_path_jpg if os.path.exists(result_path_jpg) else result_path_png
    lama_result = cv2.imread(lama_result_path) if os.path.exists(lama_result_path) else None
    if os.path.exists(lama_result_path):
        try:
            os.remove(lama_result_path)
        except:
            pass

    # 2. 用 AOT-GAN 修复
    cmd_aotgan = [INPAINT_TEST, "-i", img_path, "-k", temp_mask, "-t", "1", "-v"]
    try:
        subprocess.run(cmd_aotgan, capture_output=True, text=True, timeout=120)
    except:
        pass

    aotgan_result_path = result_path_jpg if os.path.exists(result_path_jpg) else result_path_png
    aotgan_result = cv2.imread(aotgan_result_path) if os.path.exists(aotgan_result_path) else None
    if os.path.exists(aotgan_result_path):
        try:
            os.remove(aotgan_result_path)
        except:
            pass

    # 清理临时目录
    shutil.rmtree(temp_dir, ignore_errors=True)

    elapsed = time.time() - start_time
    return lama_result, aotgan_result, elapsed


def create_lama_vs_aotgan_image(orig, mask, lama_result, aotgan_result):
    """生成 LaMa vs AOT-GAN 对比图:
    上排: 原图 | Mask | LaMa Result
    下排: 原图 | Mask | AOT-GAN Result
    """
    h, w = orig.shape[:2]

    # 创建黑白mask图
    def mask_to_bw(mask):
        if mask is None:
            return np.zeros((h, w), dtype=np.uint8)
        bw = np.zeros_like(mask)
        bw[mask > 0] = 255
        return bw

    # 转换为3通道
    def to_3ch(img):
        if img is None:
            return np.zeros((h, w, 3), dtype=np.uint8)
        if len(img.shape) == 2:
            return cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        return img

    # 统一高度缩放
    def resize(img, target_h):
        if img is None:
            return np.zeros((target_h, int(target_h * 1.5), 3), dtype=np.uint8)
        ratio = target_h / img.shape[0]
        new_w = int(img.shape[1] * ratio)
        return cv2.resize(img, (new_w, target_h))

    # 添加标签
    def add_label(img, text):
        canvas = np.zeros((img.shape[0] + 35, img.shape[1], 3), dtype=np.uint8)
        canvas[:img.shape[0], :] = img
        cv2.putText(canvas, text, (10, img.shape[0] + 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        return canvas

    # 上排: LaMa
    top_parts = [
        add_label(resize(orig, h), "Original"),
        add_label(resize(to_3ch(mask_to_bw(mask)), h), "GT Mask"),
        add_label(resize(lama_result if lama_result is not None else orig, h), "LaMa Result"),
    ]

    # 下排: AOT-GAN
    bottom_parts = [
        add_label(resize(orig, h), "Original"),
        add_label(resize(to_3ch(mask_to_bw(mask)), h), "GT Mask"),
        add_label(resize(aotgan_result if aotgan_result is not None else orig, h), "AOT-GAN Result"),
    ]

    # 拼接
    top_row = np.hstack(top_parts)
    bottom_row = np.hstack(bottom_parts)

    # 统一宽度
    max_w = max(top_row.shape[1], bottom_row.shape[1])
    def pad_width(img, target_w):
        if img.shape[1] < target_w:
            pad = np.zeros((img.shape[0], target_w - img.shape[1], 3), dtype=np.uint8)
            return np.hstack([img, pad])
        return img

    top_row = pad_width(top_row, max_w)
    bottom_row = pad_width(bottom_row, max_w)

    return np.vstack([top_row, bottom_row])


def run_compare_mode():
    """运行 LaMa vs AOT-GAN 对比模式"""
    print("\n清空result目录...")
    if os.path.exists(RESULT_DIR):
        shutil.rmtree(RESULT_DIR)
    os.makedirs(RESULT_DIR)
    print("result目录已清空")

    # 找有真实mask的图片
    images = find_images_with_masks()
    if not images:
        print("错误: 没有找到有真实mask的图片")
        return

    # 随机选择
    random.seed()
    selected = random.sample(images, min(NUM_IMAGES, len(images)))
    print(f"\n随机选择 {len(selected)} 张图片")

    times_detail = []

    for i, img_name in enumerate(selected):
        base = os.path.splitext(img_name)[0]
        print(f"\n[{i+1}/{len(selected)}] 处理: {img_name}")

        img_path = os.path.join(IMAGES_VAL, img_name)
        gt_mask_path = os.path.join(MASKS_VAL, base + ".png")

        # 读取原图
        orig = cv2.imread(img_path)
        if orig is None:
            print(f"  无法读取图片")
            continue

        # 读取 GT mask 并膨胀
        gt_mask = cv2.imread(gt_mask_path, cv2.IMREAD_GRAYSCALE)
        if gt_mask is None:
            print(f"  无法读取mask")
            continue

        _, gt_binary = cv2.threshold(gt_mask, 127, 255, cv2.THRESH_BINARY)
        kernel = np.ones((33, 33), np.uint8)  # 16px 半径
        gt_dilated = cv2.dilate(gt_binary, kernel, iterations=1)

        # 分别用 LaMa 和 AOT-GAN 修复
        print("  运行 LaMa vs AOT-GAN 修复...")
        lama_result, aotgan_result, elapsed = run_lama_vs_aotgan(img_path, gt_dilated, None)
        total_gt_time = elapsed

        print(f"  修复完成 ({elapsed:.2f}s)")

        times_detail.append({
            'name': img_name,
            'total': elapsed
        })

        # 生成对比图
        print("  生成对比图...")
        combined = create_lama_vs_aotgan_image(orig, gt_dilated, lama_result, aotgan_result)
        output_path = os.path.join(RESULT_DIR, base + "_compare.jpg")
        cv2.imwrite(output_path, combined)
        print(f"  完成: {base}_compare.jpg")

    # 显示结果
    print("\n" + "=" * 60)
    print("LaMa vs AOT-GAN 对比测试完成!")
    print(f"结果目录: {RESULT_DIR}")

    print("\n" + "-" * 60)
    print("耗时统计:")
    print("-" * 60)
    print(f"{'图片':<50} {'总耗时':>10}")
    print("-" * 60)
    for t in times_detail:
        print(f"{t['name']:<50} {t['total']:>10.2f}s")
    print("-" * 60)

    avg_time = sum(t['total'] for t in times_detail) / len(times_detail) if times_detail else 0
    print(f"{'平均耗时':<50} {avg_time:>10.2f}s")

    print("\n生成的对比图:")
    for f in sorted(os.listdir(RESULT_DIR)):
        if f.endswith("_compare.jpg"):
            size = os.path.getsize(os.path.join(RESULT_DIR, f)) / 1024
            print(f"  {f:50} {size:.1f} KB")
    print("=" * 60)


if __name__ == "__main__":
    main()
