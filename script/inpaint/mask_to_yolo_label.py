#!/usr/bin/env python3
"""
Convert binary masks to YOLO segmentation labels (multi-threaded).
Usage: python mask_to_yolo_label.py <images_dir> <masks_dir> <labels_dir> [class_id] [workers]
"""

import cv2
import numpy as np
import os
import sys
import threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed


def mask_to_yolo_polygon(mask, class_id=0, min_area=100):
    """Convert binary mask to YOLO segmentation format."""
    h, w = mask.shape[:2]

    # Find contours
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    labels = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area < min_area:
            continue

        # Simplify contour
        epsilon = 0.005 * cv2.arcLength(contour, True)
        approx = cv2.approxPolyDP(contour, epsilon, True)

        # Need at least 3 points for a polygon
        if len(approx) < 3:
            continue

        # Normalize coordinates
        points = []
        for point in approx:
            x = point[0][0] / w
            y = point[0][1] / h
            points.extend([x, y])

        labels.append(f"{class_id} " + " ".join(f"{p:.6f}" for p in points))

    return labels


def process_single_mask(args):
    """Process a single mask file. Returns (success, labels_str, stem)."""
    mask_file, images_path, labels_path, class_id = args
    stem = mask_file.stem

    # Find corresponding image
    image_file = images_path / f"{stem}.jpg"
    if not image_file.exists():
        image_file = images_path / f"{stem}.png"
    if not image_file.exists():
        return False, None, stem

    # Load mask
    mask = cv2.imread(str(mask_file), cv2.IMREAD_GRAYSCALE)
    if mask is None:
        return False, None, stem

    # Convert to binary
    _, binary = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)

    # Get labels
    labels = mask_to_yolo_polygon(binary, class_id)

    return True, "\n".join(labels) if labels else "", stem


def convert_dataset(images_dir, masks_dir, labels_dir, class_id=0, workers=8):
    """Convert all masks in directory to YOLO labels (multi-threaded)."""
    images_path = Path(images_dir)
    masks_path = Path(masks_dir)
    labels_path = Path(labels_dir)

    labels_path.mkdir(parents=True, exist_ok=True)

    # Find all mask files
    mask_files = list(masks_path.glob("*.png")) + list(masks_path.glob("*.jpg"))
    print(f"Found {len(mask_files)} mask files")

    # Prepare tasks
    tasks = [(mf, images_path, labels_path, class_id) for mf in mask_files]

    converted = 0
    skipped = 0

    # Multi-threaded processing
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(process_single_mask, task) for task in tasks]

        for i, future in enumerate(as_completed(futures)):
            success, labels_str, stem = future.result()

            if success and labels_str is not None:
                label_file = labels_path / f"{stem}.txt"
                with open(label_file, "w") as f:
                    f.write(labels_str)
                if labels_str:
                    converted += 1
                else:
                    skipped += 1
            else:
                skipped += 1

            if (i + 1) % 500 == 0:
                print(f"  Progress: {i + 1}/{len(tasks)}, Converted: {converted}")

    return converted, skipped


def main():
    if len(sys.argv) < 4:
        print("Usage: python mask_to_yolo_label.py <images_dir> <masks_dir> <labels_dir> [class_id] [workers]")
        sys.exit(1)

    images_dir = sys.argv[1]
    masks_dir = sys.argv[2]
    labels_dir = sys.argv[3]
    class_id = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    workers = int(sys.argv[5]) if len(sys.argv) > 5 else 8

    print(f"Converting masks to YOLO labels ({workers} threads)...")
    print(f"  Images: {images_dir}")
    print(f"  Masks: {masks_dir}")
    print(f"  Labels: {labels_dir}")
    print(f"  Class ID: {class_id}")

    converted, skipped = convert_dataset(images_dir, masks_dir, labels_dir, class_id, workers)

    print(f"Done! Converted: {converted}, Skipped: {skipped}")


if __name__ == "__main__":
    main()
