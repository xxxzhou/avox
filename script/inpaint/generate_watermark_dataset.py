# 水印检测/去除训练数据生成器 v4
# 一站式脚本：下载资源 + 生成数据集
#
# 运行: python generate_watermark_dataset.py
#
# 数据集配置:
#   训练集: 5000
#   验证集: 800
#   测试集: 800
#   类型分布: 文字50%(短文20%,长文15%,中文10%,版权5%), 非文字50%
#   透明度: 0.0-0.8 均匀分布
#
# 文字水印多样化:
#   - 从 watermark_vocabulary.txt 加载词汇库
#   - 动态组合生成文字内容
#   - 随机字体、颜色、效果、旋转
#   - 预估可生成 7000+ 种不同文字水印

import os
import time
import random
import platform
import requests
from PIL import Image, ImageDraw, ImageFont
from typing import List, Tuple, Dict
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

# ============================================================================
# 目录配置
# ============================================================================

if platform.system() == "Darwin":
    if os.path.exists("/Volumes/PSSD/work/data"):
        BASE_DIR = "/Volumes/PSSD/work/data/inpaint_data"
    else:
        BASE_DIR = os.path.expanduser("~/data/inpaint_data")
else:
    BASE_DIR = r"D:\Work\data\inpaint_data"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

SOURCES_DIR = os.path.join(BASE_DIR, "sources")
SOURCES_IMAGES_TRAIN_VAL = os.path.join(SOURCES_DIR, "images", "train_val")
SOURCES_IMAGES_TEST = os.path.join(SOURCES_DIR, "images", "test")
SOURCES_WATERMARKS_TRAIN_VAL = os.path.join(SOURCES_DIR, "watermarks", "train_val")
SOURCES_WATERMARKS_TEST = os.path.join(SOURCES_DIR, "watermarks", "test")

IMAGES_DIR = os.path.join(BASE_DIR, "images")
MASKS_DIR = os.path.join(BASE_DIR, "masks")
LABELS_DIR = os.path.join(BASE_DIR, "labels")

# ============================================================================
# 数据集配置
# ============================================================================

DATASET_CONFIG = {
    "train": 5000,
    "val": 800,
    "test": 800,
}

# 类型分布 (百分比) - 文字水印占50%
TYPE_DISTRIBUTION = {
    "text_short": 0.20,      # 20% 短文字
    "text_long": 0.15,       # 15% 长文字
    "text_chinese": 0.10,    # 10% 中文
    "copyright": 0.05,       # 5% 版权
    # 文字合计: 50%
    "overlay": 0.15,
    "external": 0.35,
    # 非文字合计: 50%
}

# 透明度范围: 0.0 = 不透明, 0.8 = 透明
OPACITY_RANGE = (0.0, 0.8)

# 图片划分
TRAIN_VAL_RATIO = 0.7
RANDOM_SEED = 42

# 水印类别ID
WATERMARK_CLASS_IDS = {
    "text_short": 0,
    "text_long": 1,
    "text_chinese": 2,
    "copyright": 3,
    "overlay": 4,
    "external": 5,
}

# ============================================================================
# 文字样式配置
# ============================================================================

TEXT_COLORS = [
    (255, 255, 255),  # 白色
    (0, 0, 0),        # 黑色
    (255, 0, 0),      # 红色
    (255, 255, 0),    # 黄色
    (0, 0, 255),      # 蓝色
    (128, 128, 128),  # 灰色
    (255, 128, 0),    # 橙色
    (0, 255, 0),      # 绿色
]

# 字体路径
FONT_PATHS_EN = [
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "C:/Windows/Fonts/times.ttf",
    "C:/Windows/Fonts/timesbd.ttf",
    "C:/Windows/Fonts/georgia.ttf",
    "C:/Windows/Fonts/verdana.ttf",
    "C:/Windows/Fonts/calibri.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
]

FONT_PATHS_CN = [
    "C:/Windows/Fonts/msyh.ttc",      # 微软雅黑
    "C:/Windows/Fonts/simhei.ttf",    # 黑体
    "C:/Windows/Fonts/simsun.ttc",    # 宋体
    "C:/Windows/Fonts/simkai.ttf",    # 楷体
    "C:/Windows/Fonts/STZHONGS.TTF",  # 华文中宋
    "/System/Library/Fonts/PingFang.ttc",
]

# ============================================================================
# 词汇库加载
# ============================================================================

def load_vocabulary() -> Dict[str, List[str]]:
    """加载词汇库"""
    vocab = {
        "action_en": [],
        "noun_en": [],
        "status_en": [],
        "action_cn": [],
        "noun_cn": [],
        "status_cn": [],
        "copyright_name": [],
        "template_short": [],
        "template_long": [],
        "template_copyright": [],
        "year": [],
    }

    vocab_file = os.path.join(SCRIPT_DIR, "watermark_vocabulary.txt")
    if not os.path.exists(vocab_file):
        print(f"警告: 未找到词汇库 {vocab_file}")
        return vocab

    with open(vocab_file, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(maxsplit=1)
            if len(parts) >= 2:
                category, content = parts
                if category in vocab:
                    vocab[category].append(content)

    return vocab


def generate_dynamic_text(wm_type: str, vocab: Dict[str, List[str]]) -> str:
    """动态生成水印文字"""
    if wm_type == "text_short":
        # 50% 英文状态词，50% 中文组合
        if random.random() < 0.5 and vocab["status_en"]:
            return random.choice(vocab["status_en"])
        elif vocab["action_cn"] and vocab["noun_cn"]:
            return f"{random.choice(vocab['action_cn'])}{random.choice(vocab['noun_cn'])}"
        elif vocab["status_cn"]:
            return random.choice(vocab["status_cn"])
        return random.choice(["HD", "PREVIEW", "测试"])

    elif wm_type == "text_long":
        # 60% 使用模板，40% 动态组合
        if random.random() < 0.6 and vocab["template_long"]:
            text = random.choice(vocab["template_long"])
            # 替换模板变量（小写）
            if vocab["noun_en"]:
                text = text.replace("{noun_en}", random.choice(vocab["noun_en"]))
            if vocab["action_en"]:
                text = text.replace("{action_en}", random.choice(vocab["action_en"]))
            if vocab["status_en"]:
                text = text.replace("{status_en}", random.choice(vocab["status_en"]))
            if vocab["action_cn"]:
                text = text.replace("{action_cn}", random.choice(vocab["action_cn"]))
            if vocab["noun_cn"]:
                text = text.replace("{noun_cn}", random.choice(vocab["noun_cn"]))
            # 英文模板转大写
            if not any('\u4e00' <= c <= '\u9fff' for c in text):
                text = text.upper()
            return text
        else:
            # 动态组合
            templates = [
                "NOT FOR {noun}",
                "FOR {noun} ONLY",
                "DO NOT {action}",
                "{action} {noun}",
                "STRICTLY FOR {noun}",
                "{status} VERSION",
                "{status} CONTENT",
            ]
            template = random.choice(templates)
            text = template.replace("{noun}", random.choice(vocab["noun_en"] or ["RESALE"]))
            text = text.replace("{action}", random.choice(vocab["action_en"] or ["DISTRIBUTE"]))
            text = text.replace("{status}", random.choice(vocab["status_en"] or ["PREVIEW"]))
            return text.upper()

    elif wm_type == "text_chinese":
        # 动态组合中文
        templates = [
            "{action}{noun}",
            "{action}{noun}使用",
            "仅限{noun}{action}",
            "{status}内容",
            "{status}资源",
        ]
        template = random.choice(templates)
        text = template.replace("{action}", random.choice(vocab["action_cn"] or ["仅供"]))
        text = text.replace("{noun}", random.choice(vocab["noun_cn"] or ["预览"]))
        text = text.replace("{status}", random.choice(vocab["status_cn"] or ["会员"]))
        return text

    else:  # copyright
        if vocab["template_copyright"]:
            text = random.choice(vocab["template_copyright"])
            if vocab["year"]:
                text = text.replace("{year}", random.choice(vocab["year"]))
            if vocab["copyright_name"]:
                text = text.replace("{copyright_name}", random.choice(vocab["copyright_name"]))
            return text
        year = random.choice(["2024", "2025", "2026", "2027", "2028"])
        name = random.choice(vocab["copyright_name"] or ["Company"])
        return f"© {year} {name}"


# ============================================================================
# 核心函数
# ============================================================================

def create_directories():
    """创建目录结构"""
    dirs = [
        SOURCES_IMAGES_TRAIN_VAL,
        SOURCES_IMAGES_TEST,
        IMAGES_DIR,
        MASKS_DIR,
        LABELS_DIR,
    ]

    for subset in ["train", "val", "test"]:
        dirs.extend([
            os.path.join(IMAGES_DIR, subset),
            os.path.join(MASKS_DIR, subset),
            os.path.join(LABELS_DIR, subset),
        ])

    for split in ["train_val", "test"]:
        for wm_type in WATERMARK_CLASS_IDS.keys():
            dirs.append(os.path.join(SOURCES_DIR, "watermarks", split, wm_type))

    for d in dirs:
        os.makedirs(d, exist_ok=True)

    print(f"目录已创建: {BASE_DIR}")


def download_image(url: str, filepath: str) -> bool:
    """下载图片"""
    if os.path.exists(filepath) and os.path.getsize(filepath) > 500:
        return True

    try:
        headers = {"User-Agent": "Mozilla/5.0"}
        response = requests.get(url, headers=headers, timeout=30, stream=True)
        if response.status_code == 200:
            with open(filepath, "wb") as f:
                for chunk in response.iter_content(chunk_size=8192):
                    f.write(chunk)
            return True
    except:
        pass
    return False


def load_url_list(filepath: str) -> List[Tuple[str, str, str]]:
    """加载 URL 列表"""
    urls = []
    if not os.path.exists(filepath):
        return urls

    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 2:
                urls.append((parts[0], parts[1], parts[2] if len(parts) >= 3 else ""))

    return urls


def download_resources():
    """下载原始资源 (多线程)"""
    print("\n" + "=" * 60)
    print("[步骤1] 下载原始资源 (16线程)")
    print("=" * 60)

    # 下载图片
    url_file = os.path.join(SCRIPT_DIR, "real_images_urls.txt")
    urls = load_url_list(url_file)

    if not urls:
        print(f"错误: 未找到 {url_file}")
        return False

    random.seed(RANDOM_SEED)
    random.shuffle(urls)

    split_idx = int(len(urls) * TRAIN_VAL_RATIO)
    train_urls = urls[:split_idx]
    test_urls = urls[split_idx:]

    print(f"图片: {len(urls)} (训练 {len(train_urls)}, 测试 {len(test_urls)})")

    progress = {"train": 0, "test": 0, "train_wm": 0, "test_wm": 0}
    progress_lock = threading.Lock()

    def download_task(item):
        url, filename, extra, task_type = item
        if task_type == "train":
            filepath = os.path.join(SOURCES_IMAGES_TRAIN_VAL, filename)
        elif task_type == "test":
            filepath = os.path.join(SOURCES_IMAGES_TEST, filename)
        elif task_type == "train_wm":
            filepath = os.path.join(SOURCES_WATERMARKS_TRAIN_VAL, extra or "overlay", filename)
        else:
            filepath = os.path.join(SOURCES_WATERMARKS_TEST, extra or "overlay", filename)

        success = download_image(url, filepath)
        if success:
            with progress_lock:
                progress[task_type] += 1
        return success

    # 准备图片下载任务
    tasks = []
    for url, filename, _ in train_urls:
        tasks.append((url, filename, None, "train"))
    for url, filename, _ in test_urls:
        tasks.append((url, filename, None, "test"))

    print(f"  下载图片...", flush=True)
    with ThreadPoolExecutor(max_workers=16) as executor:
        futures = [executor.submit(download_task, task) for task in tasks]
        for i, f in enumerate(as_completed(futures)):
            if (i + 1) % 100 == 0:
                print(f"    进度: {i + 1}/{len(tasks)}", flush=True)

    print(f"  训练图片: {progress['train']}/{len(train_urls)}")
    print(f"  测试图片: {progress['test']}/{len(test_urls)}")

    # 下载水印
    url_file = os.path.join(SCRIPT_DIR, "watermark_images_urls.txt")
    urls = load_url_list(url_file)

    if urls:
        random.seed(RANDOM_SEED + 1)
        random.shuffle(urls)

        train_wm = urls[:int(len(urls) * 0.7)]
        test_wm = urls[int(len(urls) * 0.7):]

        wm_tasks = []
        for url, filename, wm_type in train_wm:
            wm_tasks.append((url, filename, wm_type, "train_wm"))
        for url, filename, wm_type in test_wm:
            wm_tasks.append((url, filename, wm_type, "test_wm"))

        print(f"  下载水印...", flush=True)
        with ThreadPoolExecutor(max_workers=16) as executor:
            futures = [executor.submit(download_task, task) for task in wm_tasks]
            for f in as_completed(futures):
                pass

        print(f"  训练水印: {progress['train_wm']}/{len(train_wm)}")
        print(f"  测试水印: {progress['test_wm']}/{len(test_wm)}")

        # 如果测试水印太少，复制部分训练水印
        import shutil
        for wm_type in ["overlay", "external"]:
            test_dir = os.path.join(SOURCES_WATERMARKS_TEST, wm_type)
            train_dir = os.path.join(SOURCES_WATERMARKS_TRAIN_VAL, wm_type)
            if os.path.exists(train_dir):
                test_count = len(os.listdir(test_dir)) if os.path.exists(test_dir) else 0
                if test_count < 5:
                    for f in os.listdir(train_dir)[:10]:
                        src = os.path.join(train_dir, f)
                        dst = os.path.join(test_dir, f)
                        if not os.path.exists(dst):
                            shutil.copy2(src, dst)

    return True


def create_text_watermark(text: str, width: int, height: int,
                          font_size: int, opacity: int,
                          is_chinese: bool = False) -> Image.Image:
    """创建文字水印（带随机样式）"""
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    # 随机选择字体
    font_paths = FONT_PATHS_CN if is_chinese else FONT_PATHS_EN
    font = None
    shuffled_paths = font_paths.copy()
    random.shuffle(shuffled_paths)

    for fp in shuffled_paths:
        if os.path.exists(fp):
            try:
                font = ImageFont.truetype(fp, font_size)
                break
            except:
                continue

    if font is None:
        try:
            font = ImageFont.truetype("arial.ttf", font_size)
        except:
            font = ImageFont.load_default()

    # 随机颜色
    color = random.choice(TEXT_COLORS)

    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x, y = (width - tw) // 2, (height - th) // 2

    # 随机效果
    effect_type = random.choice(["shadow", "outline", "glow", "none"])

    if effect_type == "shadow":
        shadow_color = tuple(max(0, c - 100) for c in color[:3])
        shadow_offset = random.randint(2, 5)
        draw.text((x + shadow_offset, y + shadow_offset), text, font=font,
                  fill=(*shadow_color, opacity // 2))
        draw.text((x, y), text, font=font, fill=(*color, opacity))

    elif effect_type == "outline":
        outline_color = tuple(max(0, c - 80) for c in color[:3])
        outline_width = random.randint(1, 3)
        for ox in [-outline_width, 0, outline_width]:
            for oy in [-outline_width, 0, outline_width]:
                if ox != 0 or oy != 0:
                    draw.text((x + ox, y + oy), text, font=font,
                              fill=(*outline_color, opacity))
        draw.text((x, y), text, font=font, fill=(*color, opacity))

    elif effect_type == "glow":
        glow_color = tuple(min(255, c + 50) for c in color[:3])
        for offset in range(3, 0, -1):
            alpha = opacity // (offset + 1)
            for ox in [-offset, 0, offset]:
                for oy in [-offset, 0, offset]:
                    draw.text((x + ox, y + oy), text, font=font,
                              fill=(*glow_color, alpha))
        draw.text((x, y), text, font=font, fill=(*color, opacity))

    else:
        draw.text((x, y), text, font=font, fill=(*color, opacity))

    return layer


def generate_text_watermarks(vocab: Dict[str, List[str]]):
    """生成文字水印模板（动态生成内容）"""
    print("\n" + "=" * 60)
    print("[步骤2] 生成文字水印模板 (动态组合)")
    print("=" * 60)

    wm_w, wm_h = 1200, 800
    total = 0

    # 每种类型生成数量
    counts_per_type = {
        "text_short": 60,
        "text_long": 50,
        "text_chinese": 40,
        "copyright": 30,
    }

    for split in ["train_val", "test"]:
        count = 0
        for wm_type in ["text_short", "text_long", "text_chinese", "copyright"]:
            type_dir = os.path.join(SOURCES_DIR, "watermarks", split, wm_type)
            os.makedirs(type_dir, exist_ok=True)

            num_to_generate = counts_per_type.get(wm_type, 30)

            for i in range(num_to_generate):
                # 动态生成文字
                text = generate_dynamic_text(wm_type, vocab)

                # 检测文字是否包含中文，自动选择字体
                is_chinese = any('\u4e00' <= c <= '\u9fff' for c in text)

                if wm_type == "text_short":
                    font_size = random.randint(40, 120)
                    base_opacity = random.randint(150, 255)
                elif wm_type == "text_long":
                    font_size = random.randint(25, 70)
                    base_opacity = random.randint(120, 255)
                elif wm_type == "text_chinese":
                    font_size = random.randint(35, 100)
                    base_opacity = random.randint(150, 255)
                else:
                    font_size = random.randint(25, 60)
                    base_opacity = random.randint(150, 255)

                wm = create_text_watermark(text, wm_w, wm_h, font_size, base_opacity, is_chinese)
                filepath = os.path.join(type_dir, f"{wm_type}_{i:03d}.png")
                wm.save(filepath)
                count += 1

        print(f"  {split}: {count} 个文字水印")
        total += count

    print(f"共生成 {total} 个文字水印")
    return total


def apply_watermark(real_path: str, wm_path: str, output_path: str, mask_path: str,
                    opacity_mult: float, class_id: int) -> bool:
    """应用水印并生成掩码"""
    try:
        real_img = Image.open(real_path).convert("RGBA")
        wm_img = Image.open(wm_path).convert("RGBA")

        real_w, real_h = real_img.size
        wm_w, wm_h = wm_img.size

        # 随机缩放 (0.4 ~ 4.0) - 提高最小缩放，避免水印太小
        scale_factor = random.uniform(0.4, 4.0) * (real_w / 1200)
        new_w = max(10, int(wm_w * scale_factor))
        new_h = max(10, int(wm_h * scale_factor))
        wm_resized = wm_img.resize((new_w, new_h), Image.LANCZOS)

        # 大水印透明度限制
        wm_area = new_w * new_h
        img_area = real_w * real_h
        if wm_area / img_area > 0.2:
            opacity_mult = max(opacity_mult, 0.5)

        # 应用透明度
        alpha_factor = 1.0 - opacity_mult
        bands = list(wm_resized.split())
        if len(bands) >= 4:
            alpha = bands[3].point(lambda x: int(x * alpha_factor))
            bands[3] = alpha
            wm_resized = Image.merge("RGBA", bands)

        # 随机旋转（先旋转，再计算位置）
        rotation_angle = random.uniform(-30, 30)
        wm_resized = wm_resized.rotate(rotation_angle, expand=True, resample=Image.BICUBIC)
        new_w, new_h = wm_resized.size

        # 随机位置（旋转后重新约束，确保大部分可见）
        # 允许部分超出边界，但至少保留60%在图内
        margin_x = int(new_w * 0.4)
        margin_y = int(new_h * 0.4)
        x = random.randint(-margin_x, real_w - new_w + margin_x)
        y = random.randint(-margin_y, real_h - new_h + margin_y)

        # 合成
        result = real_img.copy()
        result.paste(wm_resized, (x, y), wm_resized)
        result = result.convert("RGB")

        # 生成掩码
        mask = Image.new("L", (real_w, real_h), 0)
        wm_bands = wm_resized.split()
        if len(wm_bands) >= 4:
            wm_alpha = wm_bands[3]
            wm_binary = wm_alpha.point(lambda p: 255 if p > 10 else 0)
            mask.paste(wm_binary, (x, y))

        # 检查mask是否有效（至少有50个非零像素）
        mask_data = list(mask.getdata())
        if sum(mask_data) < 50:  # 太少有效像素
            return False

        # 只有检查通过才保存文件
        result.save(output_path, "JPEG", quality=92)
        mask.save(mask_path, "PNG")
        return True

    except Exception as e:
        # 失败时清理可能已创建的文件
        if os.path.exists(output_path):
            os.remove(output_path)
        if os.path.exists(mask_path):
            os.remove(mask_path)
        return False


def get_opacity_bin() -> float:
    """获取透明度值 (均匀分布 0.0-0.8)"""
    bin_idx = random.randint(0, 7)
    opacity = bin_idx * 0.1 + random.uniform(0, 0.1)
    return min(opacity, 0.8)


def generate_dataset_subset(subset: str, count: int,
                            image_paths: List[str],
                            watermark_paths: List[Tuple[str, str, int]],
                            num_workers: int = 8):
    """生成单个数据集（多线程）- 持续生成直到达到目标数量"""
    print(f"\n生成 {subset} 集 (目标: {count}, {num_workers}线程)...")

    output_img_dir = os.path.join(IMAGES_DIR, subset)
    output_mask_dir = os.path.join(MASKS_DIR, subset)

    # 清空目标目录
    import shutil
    for d in [output_img_dir, output_mask_dir]:
        if os.path.exists(d):
            shutil.rmtree(d)
        os.makedirs(d, exist_ok=True)

    # 计算每种类型的目标数量
    type_counts = {t: int(count * ratio) for t, ratio in TYPE_DISTRIBUTION.items()}
    diff = count - sum(type_counts.values())
    if diff > 0:
        type_counts["external"] += diff

    # 线程安全的计数器
    generated_count = [0]
    type_generated = {t: 0 for t in TYPE_DISTRIBUTION.keys()}
    generated_lock = threading.Lock()

    def process_task(args):
        """处理单个任务，返回 (success, wm_type)"""
        real_path, wm_info, opacity = args
        wm_type, wm_path, class_id = wm_info

        real_name = os.path.splitext(os.path.basename(real_path))[0]
        wm_name = os.path.splitext(os.path.basename(wm_path))[0]

        with generated_lock:
            idx = generated_count[0]
            generated_count[0] += 1
            out_name = f"{real_name}_{wm_name}_{idx:05d}.jpg"

        img_path = os.path.join(output_img_dir, out_name)
        mask_path = os.path.join(output_mask_dir, out_name.replace('.jpg', '.png'))

        success = apply_watermark(real_path, wm_path, img_path, mask_path, opacity, class_id)
        return success, wm_type

    # 批次生成，直到达到目标
    batch_size = count * 2  # 每批生成2倍任务，补偿失败率
    total_success = 0

    while total_success < count:
        # 准备本批次任务
        tasks = []
        for wm_type, target_count in type_counts.items():
            remaining = target_count - type_generated[wm_type]
            if remaining <= 0:
                continue

            type_watermarks = [w for w in watermark_paths if w[0] == wm_type]
            if not type_watermarks:
                type_watermarks = watermark_paths

            for _ in range(remaining):
                real_path = random.choice(image_paths)
                wm_info = random.choice(type_watermarks)
                tasks.append((real_path, wm_info, get_opacity_bin()))

        if not tasks:
            break

        # 多线程处理
        with ThreadPoolExecutor(max_workers=num_workers) as executor:
            futures = [executor.submit(process_task, task) for task in tasks]
            for future in as_completed(futures):
                success, wm_type = future.result()
                if success:
                    total_success += 1
                    type_generated[wm_type] += 1

        print(f"  进度: {total_success}/{count}", flush=True)

        # 如果成功率达到瓶颈，调整策略
        if total_success < count:
            # 放宽mask验证阈值，提高成功率
            print(f"  继续生成... (还需 {count - total_success} 张)")

    print(f"  完成: {total_success} 张")
    return total_success


def generate_datasets():
    """生成所有数据集"""
    print("\n" + "=" * 60)
    print("[步骤3] 生成数据集")
    print("=" * 60)

    # 获取图片
    train_val_images = [
        os.path.join(SOURCES_IMAGES_TRAIN_VAL, f)
        for f in os.listdir(SOURCES_IMAGES_TRAIN_VAL)
        if f.endswith(('.jpg', '.png', '.jpeg'))
    ]
    test_images = [
        os.path.join(SOURCES_IMAGES_TEST, f)
        for f in os.listdir(SOURCES_IMAGES_TEST)
        if f.endswith(('.jpg', '.png', '.jpeg'))
    ]

    if not train_val_images:
        print("错误: 没有找到图片")
        return

    if not test_images:
        test_images = train_val_images

    # 获取水印
    train_val_wms = []
    test_wms = []

    for split, wm_list in [("train_val", train_val_wms), ("test", test_wms)]:
        wm_dir = os.path.join(SOURCES_DIR, "watermarks", split)
        if not os.path.exists(wm_dir):
            continue

        for wm_type in os.listdir(wm_dir):
            type_dir = os.path.join(wm_dir, wm_type)
            if os.path.isdir(type_dir):
                class_id = WATERMARK_CLASS_IDS.get(wm_type, 0)
                for wm_file in os.listdir(type_dir):
                    if wm_file.endswith('.png'):
                        wm_list.append((wm_type, os.path.join(type_dir, wm_file), class_id))

    print(f"训练/验证图片: {len(train_val_images)}")
    print(f"测试图片: {len(test_images)}")
    print(f"训练/验证水印: {len(train_val_wms)}")
    print(f"测试水印: {len(test_wms)}")

    if not train_val_wms:
        print("错误: 没有找到水印")
        return

    if not test_wms:
        test_wms = train_val_wms

    # 生成
    generate_dataset_subset("train", DATASET_CONFIG["train"], train_val_images, train_val_wms)
    generate_dataset_subset("val", DATASET_CONFIG["val"], train_val_images, train_val_wms)
    generate_dataset_subset("test", DATASET_CONFIG["test"], test_images, test_wms)


def generate_dataset_yaml():
    """生成 dataset.yaml"""
    yaml_content = f"""# 水印检测数据集配置
path: {BASE_DIR}
train: images/train
val: images/val
test: images/test

nc: 6
names:
  0: text_short
  1: text_long
  2: text_chinese
  3: copyright
  4: overlay
  5: external
"""

    with open(os.path.join(BASE_DIR, "dataset.yaml"), "w", encoding="utf-8") as f:
        f.write(yaml_content)

    print(f"配置文件: {BASE_DIR}/dataset.yaml")


def print_summary():
    """打印总结"""
    print("\n" + "=" * 60)
    print("数据集生成完成!")
    print("=" * 60)
    print(f"数据目录: {BASE_DIR}")
    print(f"\n数据集规模:")
    print(f"  训练集: {DATASET_CONFIG['train']}")
    print(f"  验证集: {DATASET_CONFIG['val']}")
    print(f"  测试集: {DATASET_CONFIG['test']}")
    print(f"  总计:   {sum(DATASET_CONFIG.values())}")
    print(f"\n类型分布 (文字水印50%):")
    for t, r in TYPE_DISTRIBUTION.items():
        print(f"  {t}: {r*100:.0f}%")
    print(f"\n透明度分布: 0.0-0.8 均匀分布")
    print(f"\n文字水印多样化:")
    print(f"  - 动态组合生成，覆盖7000+种可能")
    print(f"  - 随机字体、颜色、效果、旋转")
    print("=" * 60)


def main():
    print("=" * 60)
    print("水印检测训练数据生成器 v4")
    print("文字水印占比50%，动态组合生成")
    print("=" * 60)

    # 加载词汇库
    vocab = load_vocabulary()
    print(f"词汇库加载完成:")
    for k, v in vocab.items():
        if v:
            print(f"  {k}: {len(v)} 个")

    # 创建目录
    create_directories()

    # 下载资源
    download_resources()

    # 生成文字水印
    generate_text_watermarks(vocab)

    # 生成数据集
    generate_datasets()

    # 生成配置
    generate_dataset_yaml()

    # 总结
    print_summary()


if __name__ == "__main__":
    main()
