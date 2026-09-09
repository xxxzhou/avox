#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Godot Android APK 后处理: 注入 avox 依赖库与动态插件资源, 重新对齐签名。

背景: Godot 导出器只打包 gdextension [libraries] 声明的主库 (libavox_godot.so),
libavox.so 的其余 DT_NEEDED 伴随库与作为运行期资源的动态插件 (libavox_torrent.so,
被导出器按 .so 排除) 都进不了 APK —— 本脚本在导出后补齐。

用法 (仓库根或任意目录):
  python platform/godot/plugin/patch_apk.py <apk 路径>
步骤: 注入 lib/arm64-v8a/*.so + assets/addons/avox_godot/plugins/*.so → zipalign → apksigner
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
INSTALL = REPO_ROOT / "build" / "android" / "avox" / "install" / "aarch64"
FFMPEG_LIB = REPO_ROOT / "3rdparty" / "library" / "android" / "ffmpeg" / "lib"
SDK = Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"
BUILD_TOOLS = SDK / "build-tools" / "36.0.0"

# 注入 lib/arm64-v8a/ 的伴随库 (libavox_godot.so 已由导出器打包)
# 动态插件 libavox_torrent.so 也放这里: Android 释放到 nativeLibraryDir,
# avox_godot 引导用 dladdr 定位该目录作为 ModuleMgr 插件扫描目录
LIB_INJECT = [
    ("libavox.so", [INSTALL / "libavox.so"]),
    ("libc++_shared.so", [INSTALL / "libc++_shared.so"]),
    ("libmk_api.so", [INSTALL / "libmk_api.so"]),
    ("libfdk-aac.so", [INSTALL / "libfdk-aac.so"]),
    ("libavutil.so", [INSTALL / "libavutil.so", FFMPEG_LIB / "libavutil.so"]),
    ("libavformat.so", [INSTALL / "libavformat.so", FFMPEG_LIB / "libavformat.so"]),
    ("libavcodec.so", [INSTALL / "libavcodec.so", FFMPEG_LIB / "libavcodec.so"]),
    ("libswresample.so", [INSTALL / "libswresample.so", FFMPEG_LIB / "libswresample.so"]),
    ("libavox_torrent.so", [INSTALL / "Release" / "plugins" / "libavox_torrent.so"]),
]


# Java 助手类 dex (deploy_godot_android.py 编译产出): AndAudioRender 的
# FindClass("avox.android.library.AvoxAudioTrack") 需要; API 21+ 自动加载 classesN.dex
DEX_CANDIDATES = [
    REPO_ROOT / "build" / "android" / "avox_java" / "classes.dex",
]
# Vulkan compute shader (AAssetManager 读 APK 裸 assets/, 打进 pck 无效);
# 预编译产物仓库已有: glsl/target/ (src/CMakeLists.txt 各平台从这里收集)
SPV_SRC = REPO_ROOT / "glsl" / "target"


def find_sdk_tool(name: str) -> Path:
    base = BUILD_TOOLS
    if not base.exists():
        # 回退: 取 build-tools 下版本最高的目录
        sdk_root = SDK
        if sdk_root.exists():
            vers = [d for d in sdk_root.joinpath("build-tools").iterdir() if d.is_dir()]
            if vers:
                base = max(vers, key=lambda d: d.name)
    return base / name


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    apk = Path(sys.argv[1]).resolve()
    if not apk.is_file():
        print(f"APK 不存在: {apk}")
        return 2

    # 0) manifest 注入 magnet deep-link (浏览器点磁力直接唤起; 幂等)
    try:
        import axml_patch
        axml_patch.patch_apk_manifest(str(apk))
    except Exception as e:  # 注入失败不阻塞打包, 仅失去深链能力
        print(f"警告: manifest 注入失败 ({e})")

    # 1) 收集注入内容
    inject_lib = []
    missing = []
    for name, cands in LIB_INJECT:
        src = next((c for c in cands if c.is_file()), None)
        if src is None:
            missing.append(name)
        else:
            inject_lib.append((f"lib/arm64-v8a/{name}", src))
    if missing:
        print("缺失注入文件:", missing)
        return 2
    dex = next((c for c in DEX_CANDIDATES if c.is_file()), None)
    if dex is not None:
        # 幂等: 已注入过 (任一 dex 含 AvoxAudioTrack) 则不再重复注入,
        # 否则每次 patch 都会多出一个内容相同的 classesN.dex
        already = False
        with zipfile.ZipFile(apk) as zprobe:
            for n in zprobe.namelist():
                if re.fullmatch(r"classes\d+\.dex", n) and \
                        b"AvoxAudioTrack" in zprobe.read(n):
                    already = True
                    break
        if not already:
            # Godot APK 自带 classes2..N.dex, 固定名会顶掉 Godot 自己的类;
            # 取现有最大 N+1 (ART 按 classesN.dex 模式加载全部, N 不限)
            max_n = 1
            for n in zprobe.namelist():
                m = re.fullmatch(r"classes(\d+)\.dex", n)
                if m:
                    max_n = max(max_n, int(m.group(1)))
            dex_arc = f"classes{max_n + 1}.dex"
            inject_lib.append((dex_arc, dex))
            print(f"OK {dex_arc} ({dex.stat().st_size/1e3:.0f}KB)")
        else:
            print("OK dex 已注入, 跳过")
    else:
        print("警告: 缺 classes.dex (deploy 未编 Java 助手类), AndAudioRender 音频将不可用")
    # Vulkan compute shader → assets/glsl/ (AAssetManager 只读 APK 裸 assets/,
    # 打进 pck 无效); 缺了 VkVideoRender 初始化加载 shader 会失败
    if SPV_SRC.is_dir():
        for spv in sorted(SPV_SRC.glob("*.spv")):
            inject_lib.append((f"assets/glsl/{spv.name}", spv))
        print(f"OK assets/glsl/ ({len(list(SPV_SRC.glob('*.spv')))} spv)")
    else:
        print("警告: 缺 glsl_spv/, Vulkan 渲染管线将不可用")

    # 2) 重写 APK (zip 注入)
    tmp = apk.with_suffix(".apk.patched")
    with zipfile.ZipFile(apk, "r") as zin, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            if item.filename in {a for a, _ in inject_lib}:
                continue  # 已存在则用新内容覆盖
            zout.writestr(item, zin.read(item.filename))
        # 库以 STORED 方式写入 (so 已是压缩无益的机器码;STORED 满足对齐要求由 zipalign 处理)
        for arc, src in inject_lib:
            zout.writestr(arc, src.read_bytes(), compress_type=zipfile.ZIP_STORED)
            print(f"OK {arc} ({src.stat().st_size/1e6:.1f}MB)")
    shutil.move(tmp, apk)

    # 3) 对齐 + 签名 (debug keystore)
    zipalign = find_sdk_tool("zipalign.exe")
    apksigner = find_sdk_tool("apksigner.bat")
    aligned = apk.with_suffix(".apk.aligned")
    subprocess.run([str(zipalign), "-f", "-p", "4", str(apk), str(aligned)], check=True)
    shutil.move(aligned, apk)
    ks = Path.home() / ".android" / "debug.keystore"
    subprocess.run([str(apksigner), "sign", "--ks", str(ks), "--ks-pass", "pass:android",
                    "--key-pass", "pass:android", "--ks-key-alias", "androiddebugkey",
                    str(apk)], check=True)
    print(f"完成: {apk} ({apk.stat().st_size/1e6:.0f}MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
