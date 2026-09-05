#!/usr/bin/env python3
"""打包 avox_godot 插件分发包, 面向 Godot 用户的即用产物。

源 = avox 构建安装树 (默认 build/.../Release, 只读不动);
产物 = --out 目录, 布局与开发期 junction 部署一致 (bin=精简 Release 树),
GDExtension 依赖解析 (LOAD_WITH_ALTERED_SEARCH_PATH) 与 plugins 查找都成立:
  avox-godot-plugin/
  ├── README.txt                     快速开始 (示例代码/模型下载/授权摘要)
  ├── LICENSES/AGPL-3.0.txt          开源协议全文 (仓库根 LICENSE)
  ├── LICENSES/THIRD-PARTY-NOTICES.txt  第三方组件与许可 (静态清单)
  └── samples/                       官方示例工程 (bin 为真实 dll, 非 junction)

用法:
  python pack_godot_plugin.py                                # -> deploy/godot_plugin
  python pack_godot_plugin.py --out D:\\avox_plugin
  python pack_godot_plugin.py --zip D:\\rels\\               # 打 zip 到目录 (自动命名)
  python pack_godot_plugin.py --android <android构建树bin_android>  # 附 Android so
前置: python build_windows.py (AVOX_ENABLE_GODOT 默认 ON)
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

PLATFORM = "windows"
DEFAULT_SRC = os.path.join(
    common.project_root(), "build", "windows", "avplay", "install", "AMD64", "Release")
DEFAULT_SAMPLES = os.path.join(common.project_root(), "platform", "godot", "samples")
REPO_URL = "https://github.com/xxxzhou/avox"          # 已按 git remote 核实

# 拷 samples 工程时排除: godot 缓存 / 日志 / bin junction 与安卓 so (bin 由本脚本重建为真实 dll)
SAMPLES_IGNORE_DIRS = {".godot", "bin_android"}

# 顶层 dll 黑名单: 其他引擎的壳 (Unity/Godot 无关) + GPL/Nonfree 授权雷 (faad=GPL, fdk-aac=nonfree),
# 合规分发必须剔除; AAC 解码用 ffmpeg 原生, 不受影响
RUNTIME_DLL_DENY = ("avox_unity", "AvoxWrapper", "faad-", "fdk-aac")


def samples_ignore(directory, names):
    """copytree ignore: .godot 缓存、日志、bin junction (整个 Release 树, 绝不能跟) 。"""
    kept = []
    for n in names:
        if n in SAMPLES_IGNORE_DIRS:
            kept.append(n)
        elif n == "bin" and os.path.basename(directory) == "avox_godot":
            kept.append(n)                               # junction -> Release, 不能跟
        elif n.endswith(".log"):
            kept.append(n)
    return kept


def reset_out(out):
    """清空产物目录; 顶层被占用 (索引器/shell 句柄) 时清空内容复用目录。"""
    if not os.path.exists(out):
        os.makedirs(out)
        return
    try:
        shutil.rmtree(out)
        os.makedirs(out)
    except PermissionError:
        for root, dirs, files in os.walk(out, topdown=False):
            for f in files:
                try:
                    os.remove(os.path.join(root, f))
                except OSError:
                    pass
            for d in dirs:
                try:
                    os.rmdir(os.path.join(root, d))
                except OSError:
                    pass
        print(f"  [提示] 产物目录被占用, 已清空内容复用: {out}")


def copy_runtime(src, bin_dir):
    """铺运行时到 addons/avox_godot/bin: 精简 Release 树 (与开发期 junction 等价)。
    - 顶层 dll (剔第三方 AI 库、其他引擎壳与 GPL/Nonfree 授权雷)
    - plugins/ 壳 (sherpa-onnx 等构建产物随包, onnx/opencv 等按需下载)
    - onnxruntime.dll 显式补进 plugins/ (sherpa 硬依赖, 语音识别必需)
    - assets 子集 (glsl/fonts/script, 模型由 fetch_assets 按需下载)
    """
    copied = 0
    for f in sorted(os.listdir(src)):
        s = os.path.join(src, f)
        if not os.path.isfile(s) or not f.endswith(".dll"):
            continue
        if f.startswith("~") or common.is_plugin_dep(f):
            continue
        if any(f.startswith(p) for p in RUNTIME_DLL_DENY):
            continue
        common.copy_file(s, os.path.join(bin_dir, f))
        copied += 1
    print(f"  [复制] 顶层运行 dll ({copied} 个)")
    common.copy_plugins(os.path.join(src, "plugins"), os.path.join(bin_dir, "plugins"))
    onnx = os.path.join(src, "onnxruntime.dll")
    if os.path.isfile(onnx):
        common.copy_file(onnx, os.path.join(bin_dir, "plugins", "onnxruntime.dll"))
    else:
        print("  [警告] 未见 onnxruntime.dll, 语音识别/ONNX 推理将不可用")
    common.copy_assets(os.path.join(src, "assets"), os.path.join(bin_dir, "assets"))


def copy_android(android_dir, bin_android):
    """--android: 附 Android arm64 so (构建树 bin_android); gdextension 已引用该路径。"""
    if not os.path.isdir(android_dir):
        print(f"  [跳过] Android 构建树不存在: {android_dir}")
        return
    n = common.copy_dir(android_dir, bin_android)
    if n:
        print("  [提示] Android so 已附; avox 运行时 so 需按 platform/godot/docs/新机器Android打包.md 一并交付")


THIRD_PARTY_NOTICES = """\
avox_godot 分发包第三方组件清单
================================

本分发包包含 avox 项目 (AGPL-3.0) 的原创代码, 以及以下第三方组件。
各组件遵循其自身许可证; 源码可通过对应上游仓库或 avox 仓库 3rdparty/ 构建脚本获取。

FFmpeg (libavcodec/libavformat/libavutil/libswresample/libswscale)
  许可: LGPL-2.1-or-later (动态链接, 需使用未启用 GPL 组件的构建)
  源码: https://ffmpeg.org/download.html

ZLMediaKit
  许可: MIT
  源码: https://github.com/ZLMediaKit/ZLMediaKit

godot-cpp (Godot 4 GDExtension 绑定)
  许可: MIT (Godot Engine, (c) Juan Linietsky, Ariel Manzur, Godot Engine contributors)
  源码: https://github.com/godotengine/godot-cpp

volk (Vulkan meta-loader)
  许可: MIT
  源码: https://github.com/zeux/volk

sherpa-onnx (语音识别/合成)
  许可: Apache-2.0
  源码: https://github.com/k2-fsa/sherpa-onnx

onnxruntime (推理运行时, 未随包, 可经 fetch_assets.py 下载)
  许可: MIT
  源码: https://github.com/microsoft/onnxruntime

OpenCV (未随包, 可经 fetch_assets.py 下载)
  许可: Apache-2.0
  源码: https://github.com/opencv/opencv

运行时模型 (如 STT/TTS 模型) 各自遵循其发布页许可, 用 assets/script/fetch_assets.py
下载时请一并查看对应条目说明。
"""


def write_readme(out, has_android):
    lines = [
        "avox_godot 插件分发包",
        "",
        "什么是它: Godot 4 硬解视频/直播流/采集录制/语音AI 的 GDExtension 插件。",
        "本包按 AGPL-3.0 授权, 源码仓库: " + REPO_URL,
        "",
        "快速开始:",
        "  1. Godot 4.3+ 新建或打开工程 (渲染后端选 Forward+/Vulkan)",
        "  2. 把 samples/addons/ 整个目录复制进你的工程根目录",
        "     (或只取 addons/avox_godot/: .gdextension + bin/ 必须同路径)",
        "  3. 编辑器里启用插件后运行 samples 场景, 或自己写:",
        "",
        "         var player = MediaPlayer.new()",
        "         add_child(player)",
        '         player.play("res://movie.mp4")   # 或 rtmp://... 直播流',
        "         $TextureRect.texture = player.get_texture()",
        "",
        "  4. 语音识别等 AI 功能需要下载模型 (首次):",
        "     python addons/avox_godot/bin/assets/script/fetch_assets.py --interactive",
        "",
        "示例工程: samples/ (本地播放/直播流/相机采集/语音字幕 4 个场景)",
    ]
    if has_android:
        lines += ["", "Android: addons/avox_godot/bin_android/ 已附 arm64 so (导出 Android 勾选 Gradle 构建)。"]
    lines += ["", "许可与第三方组件: 见 LICENSES/ 目录。"]
    path = os.path.join(out, "README.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\r\n".join(lines) + "\r\n")
    print(f"  [写] {path}")


def write_licenses(out):
    lic = os.path.join(out, "LICENSES")
    os.makedirs(lic, exist_ok=True)
    shutil.copy2(os.path.join(common.project_root(), "LICENSE"),
                 os.path.join(lic, "AGPL-3.0.txt"))
    with open(os.path.join(lic, "THIRD-PARTY-NOTICES.txt"), "w", encoding="utf-8") as f:
        f.write(THIRD_PARTY_NOTICES)
    print(f"  [写] LICENSES/ (AGPL 全文 + 第三方清单)")


def pack(out="", src="", samples="", zip_path="", android=""):
    out = out or os.path.join(common.project_root(), "deploy", "godot_plugin")
    src = src or DEFAULT_SRC
    samples = samples or DEFAULT_SAMPLES
    if not os.path.isdir(src):
        raise SystemExit(f"找不到 avox 构建安装树: {src}\n请先构建, 或用 --src 指定")
    for need in ("avox.dll", "avox_godot.dll"):
        if not os.path.isfile(os.path.join(src, need)):
            raise SystemExit(f"{src} 下没有 {need}, 先构建 avox (AVOX_ENABLE_GODOT 默认 ON)")
    if not os.path.isfile(os.path.join(samples, "project.godot")):
        raise SystemExit(f"找不到示例工程: {samples}\\project.godot")

    out = os.path.abspath(out)
    reset_out(out)                                       # 分发产物目录, 重建保干净
    print("== 打包 avox_godot 插件分发包 ==")
    print(f"  源 (avox 安装树): {src}")
    print(f"  示例工程:         {samples}")
    print(f"  产物目录:         {out}")

    # 1) samples 工程 (排除 .godot / bin junction), bin 重建为真实 dll
    dst_samples = os.path.join(out, "samples")
    shutil.copytree(samples, dst_samples, ignore=samples_ignore)
    bin_dir = os.path.join(dst_samples, "addons", "avox_godot", "bin")
    os.makedirs(bin_dir)
    copy_runtime(src, bin_dir)
    open(os.path.join(bin_dir, ".gdignore"), "w").close()  # 资源扫描跳过 bin (dll 照常加载)
    print("  [复制] samples 工程 -> samples/ (bin 已重建为真实 dll)")

    # 2) 可选 Android so
    has_android = False
    if android:
        copy_android(android, os.path.join(dst_samples, "addons", "avox_godot", "bin_android"))
        has_android = os.path.isdir(os.path.join(dst_samples, "addons", "avox_godot", "bin_android"))

    # 3) 授权文档 + README
    write_licenses(out)
    write_readme(out, has_android)
    common.print_thirdparty_hint()

    # 4) 可选 zip
    if zip_path:
        if os.path.isdir(zip_path):
            zip_path = os.path.join(
                zip_path, f"avox-godot-{PLATFORM}-x64-{common.get_version()}.zip")
        common.make_zip(out, zip_path)
    print(f"== 完成: {out} ==")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="", help="产物目录 (默认 deploy/godot_plugin)")
    ap.add_argument("--src", default="", help="avox 构建安装树 (默认 build/.../Release, 只读)")
    ap.add_argument("--samples", default="", help="示例工程 (默认 platform/godot/samples)")
    ap.add_argument("--zip", default="", help="额外打 zip (文件路径或目录, 目录时自动命名)")
    ap.add_argument("--android", default="", help="Android 构建树 bin_android 目录 (可选, 附 so)")
    args = ap.parse_args(argv)
    pack(out=args.out, src=args.src, samples=args.samples, zip_path=args.zip, android=args.android)


if __name__ == "__main__":
    main()
