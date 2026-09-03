#!/usr/bin/env python3
"""pack_sdk 各平台共享工具: 路径、版本、复制/过滤、打包 zip。"""
import argparse
import datetime
import os
import shutil
import subprocess
import zipfile

# ── 复制过滤 ──────────────────────────────────────────────
# 插件目录中剔除的构建产物 (运行时不需要 .lib/.exp/.obj/.pdb)
SKIP_EXTS = {".lib", ".exp", ".obj", ".pdb", ".ipdb", ".iobj", ".pyc"}
SKIP_NAMES = {"__pycache__", "cache.json", "build_attempt.log"}
# 顶层这些第三方依赖已随各自插件进 plugins/ 自包含, 不允许再出现于顶层
PLUGIN_DEP_PREFIX = ("onnxruntime", "opencv_world", "sherpa-onnx-", "openvino", "tbb")
# 部署默认只带 assets 子集, 模型由用户用 assets/script/fetch_assets.py 下载
ASSET_DIRS = ("agent", "glsl", "fonts", "script")
# assets_manifest.json 里有对应 library 项、可由 fetch_assets.py 按需下载的第三方库,
# 默认不随 SDK 打包 (用户按功能/插件自己下); --with-thirdparty 时从构建产物一并复制。
# 注意 sherpa-onnx-* 是 build 产物 (manifest 不可下载), 必须随包走, 不在排除之列。
DOWNLOADABLE_DEPS_PREFIX = ("onnxruntime", "opencv_world", "opencv_videoio_ffmpeg", "openvino", "tbb")


def project_root():
    # script/package/sdk_common.py -> 上溯三层到项目根
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def get_version(root=None):
    """取最近 git tag 作为版本号, 失败退回日期。"""
    root = root or project_root()
    try:
        r = subprocess.run(["git", "describe", "--tags", "--abbrev=0"],
                           capture_output=True, text=True, cwd=root)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except Exception:
        pass
    return datetime.datetime.now().strftime("%Y%m%d")


def is_plugin_dep(name):
    """名字是否属于插件第三方依赖 (应放 plugins/, 不在顶层)。"""
    return any(name.startswith(p) for p in PLUGIN_DEP_PREFIX)


def is_downloadable_dep(name):
    """名字是否属于 manifest 可下载的第三方库 (默认不打包, 用户按需 fetch_assets 下载)。"""
    return any(name.startswith(p) for p in DOWNLOADABLE_DEPS_PREFIX)


def copy_file(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def copy_dir(src, dst, skip_prefixes=()):
    """复制目录, 剔除构建产物 (SKIP_EXTS / SKIP_NAMES) 与 skip_prefixes 前缀文件。src 不存在时仅提示。"""
    if not os.path.isdir(src):
        print(f"  [跳过] 源目录不存在: {src}")
        return 0
    copied = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_NAMES]
        for f in files:
            if f in SKIP_NAMES or os.path.splitext(f)[1] in SKIP_EXTS:
                continue
            if skip_prefixes and any(f.startswith(p) for p in skip_prefixes):
                continue
            s = os.path.join(root, f)
            copy_file(s, os.path.join(dst, os.path.relpath(s, src)))
            copied += 1
    print(f"  [复制] {src}")
    print(f"         -> {dst}  ({copied} 个文件)")
    return copied


def copy_plugins(src, dst, with_thirdparty=False):
    """复制插件目录 (插件壳 avox_*.dll 等始终带); 第三方库默认不打包, 由 fetch_assets 按需下载。"""
    prefixes = () if with_thirdparty else DOWNLOADABLE_DEPS_PREFIX
    return copy_dir(src, dst, skip_prefixes=prefixes)


def copy_assets(src_assets, dst_assets):
    """只复制运行所需 assets 子集 (glsl/fonts/script), 模型留给用户下载。"""
    if not os.path.isdir(src_assets):
        print(f"  [跳过] assets 目录不存在: {src_assets}")
        return
    for d in ASSET_DIRS:
        copy_dir(os.path.join(src_assets, d), os.path.join(dst_assets, d))


def make_zip(src_dir, zip_path):
    """把部署结果目录打包成 zip。"""
    if os.path.isdir(zip_path):
        raise ValueError(f"--zip 给了目录, 需要 zip 文件路径: {zip_path}")
    os.makedirs(os.path.dirname(os.path.abspath(zip_path)), exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(src_dir):
            dirs[:] = [d for d in dirs if d not in SKIP_NAMES]
            for f in files:
                s = os.path.join(root, f)
                zf.write(s, os.path.relpath(s, src_dir))
    print(f"  [ZIP] {zip_path}")
    return zip_path


def add_common_args(p):
    p.add_argument("--out", default="", help="部署目标目录 (默认 deploy/<platform>)")
    p.add_argument("--src", default="", help="SDK 构建输出 (install) 目录; 默认自动查找")
    p.add_argument("--zip", default="", help="额外把部署结果打包成 zip (zip 文件路径)")
    p.add_argument("--headers", action="store_true", help="连同 include/ 头文件一起部署")
    p.add_argument("--with-thirdparty", action="store_true",
                   help="连 opencv/openvino/onnxruntime/tbb 等第三方库一起打包 (默认不打包, 用户用 fetch_assets.py 按需下载)")
    p.add_argument("--abi", default="arm64-v8a", help="Android 目标 ABI (仅 android 生效)")


def print_thirdparty_hint():
    """部署完成后提示如何按需下载第三方库/模型。"""
    print("  ─────────────────────────────────────────────────────────────")
    print("  第三方库/模型未打包。用部署目录里的脚本按需下载:")
    print("    cd <部署目录>")
    print("    python assets/script/fetch_assets.py check                # 看缺什么 (按已装插件)")
    print("    python assets/script/fetch_assets.py --interactive        # 交互式勾选下载")
    print("    python assets/script/fetch_assets.py --plugin avox_ocr --all   # 按插件/功能下载")
    print("  ─────────────────────────────────────────────────────────────")
