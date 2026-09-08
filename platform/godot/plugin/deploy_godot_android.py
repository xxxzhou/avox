#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""部署 avox_godot Android 产物到 Godot 项目 (真实拷贝, 非 junction)。

用法 (仓库根):
  python platform/godot/plugin/deploy_godot_android.py [--project platform/godot/tools]

布局:
  <project>/addons/avox_godot/bin_android/{libavox_godot.so, libavox.so, libc++_shared.so}
  <project>/addons/avox_godot/plugins/libavox_torrent.so   # 动态插件, 启动时解压到 filesDir
gdextension 的 android.* 段指向 bin_android/; Godot 导出 APK 时 res:// 下的
plugins/*.so 作为普通资源打进包, 运行期由 avox_godot 的 Android 引导解压并 setPluginsDir。
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
RELEASE_CANDIDATES = [
    REPO_ROOT / "build" / "android" / "avplay" / "install" / "aarch64" / "Release",
    REPO_ROOT / "build" / "android" / "avplay" / "install" / "arm64" / "Release",
    REPO_ROOT / "build" / "android" / "avplay" / "install" / "aarch64",  # libavox.so 无 CONFIG 层
]


def find_release() -> Path:
    for c in RELEASE_CANDIDATES:
        if (c / "libavox_godot.so").is_file():
            return c
    for c in RELEASE_CANDIDATES:
        if c.is_dir():
            return c
    return None


# ── avox Java 助手类 → classes.dex ──
# AndAudioRender 经 FindClass("avox.android.library.AvoxAudioTrack") 走 Java AudioTrack,
# Godot APK 没有 AvoxJava, 这里编出 dex 交 patch_apk 注入 classes2.dex (API 21+ 自动加载)。
# 只编 native 侧 FindClass 的两个类; JNIHelper 引 swig 包, 与 SWIG=OFF 构建不符, 排除
JAVA_SRC = REPO_ROOT / "platform" / "android" / "AvoxJava" / "avox" / "src" / "main" / "java" / "avox" / "android" / "library"
JAVA_SOURCES = ["AvoxAudioTrack.java", "AvoxSurfaceTextureOb.java"]
DEX_OUT = REPO_ROOT / "build" / "android" / "avox_java"


def find_sdk_latest(sub: str, name: str):
    sdk = Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"
    base = sdk / sub
    if not base.is_dir():
        return None
    for v in sorted((d for d in base.iterdir() if d.is_dir()), reverse=True):
        tool = v / name
        if tool.is_file():
            return tool
    return None


def build_java_dex() -> None:
    if not JAVA_SRC.is_dir():
        print(f"跳过 javadex: 缺 {JAVA_SRC}")
        return
    java_home = Path(os.environ.get("JAVA_HOME", r"D:/Program Files/Android/Android Studio/jbr"))
    javac = java_home / "bin" / "javac.exe"
    android_jar = find_sdk_latest("platforms", "android.jar")
    d8 = find_sdk_latest("build-tools", "d8.bat")
    if not javac.is_file() or android_jar is None or d8 is None:
        print(f"跳过 javadex: 缺 javac({javac.exists()})/android.jar({android_jar is not None})/d8({d8 is not None})")
        return
    classes = DEX_OUT / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir(parents=True)
    srcs = [str(JAVA_SRC / name) for name in JAVA_SOURCES]
    subprocess.run([str(javac), "--release", "8", "-classpath", str(android_jar),
                    "-d", str(classes)] + srcs, check=True)
    subprocess.run([str(d8), "--release", "--lib", str(android_jar),
                    "--output", str(DEX_OUT)] + [str(p) for p in sorted(classes.rglob("*.class"))], check=True)
    print(f"OK classes.dex ({(DEX_OUT / 'classes.dex').stat().st_size/1e3:.0f}KB)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", default=str(SCRIPT_DIR.parent / "tools"))
    args = ap.parse_args()

    release = find_release()
    if release is None:
        print("未找到 Android 产物 (libavox_godot.so), 先跑: python build_android.py")
        return 2
    project = Path(args.project)
    if not (project / "project.godot").is_file():
        print(f"不是 Godot 项目: {project}")
        return 2

    bin_dst = project / "addons" / "avox_godot" / "bin_android"
    plugins_dst = project / "addons" / "avox_godot" / "plugins"
    bin_dst.mkdir(parents=True, exist_ok=True)
    plugins_dst.mkdir(parents=True, exist_ok=True)

    # gdextension 主库 + 运行依赖: libavox.so 的 DT_NEEDED 平铺进 APK lib 目录
    # (系统库 liblog/libandroid/EGL/... 由 Android 提供, 不打)。libavox.so /
    # libc++_shared.so / 模块库装在 install/aarch64(无 CONFIG 层), 逐级回退查找。
    root = release.parent if release.name == "Release" else release
    ffmpeg_lib = REPO_ROOT / "3rdparty" / "library" / "android" / "ffmpeg" / "lib"
    companions = {
        "libavox_godot.so": [release],
        "libavox.so": [root],
        "libc++_shared.so": [root],
        "libmk_api.so": [root],
        "libfdk-aac.so": [root],
        "libavutil.so": [root, ffmpeg_lib],
        "libavformat.so": [root, ffmpeg_lib],
        "libavcodec.so": [root, ffmpeg_lib],
        "libswresample.so": [root, ffmpeg_lib],
    }
    for name, cands in companions.items():
        src = None
        for c in [release] + cands:
            cand = c / name
            if cand.is_file():
                src = cand
                break
        if src is not None:
            shutil.copy2(src, bin_dst / name)
            print(f"OK {name}")
        else:
            print(f"跳过(缺) {name}")

    # 动态插件: 白名单(avox_torrent), 打包为 APK 资源, 运行期解压到 filesDir/plugins
    plugin_src = release / "plugins"
    for name in ["libavox_torrent.so"]:
        src = plugin_src / name
        if src.is_file():
            shutil.copy2(src, plugins_dst / name)
            print(f"OK plugins/{name}")
        else:
            print(f"跳过(缺) plugins/{name}")

    # .gdextension 仓库静态文件复制到 addons
    ext_src = SCRIPT_DIR / "avox_godot.gdextension"
    if ext_src.is_file():
        shutil.copy2(ext_src, project / "addons" / "avox_godot" / "avox_godot.gdextension")
        print("OK avox_godot.gdextension")

    # Java 助手类 dex (音频路径依赖, patch_apk 注入为 classes2.dex)
    build_java_dex()

    print(f"\n部署完成: {project}/addons/avox_godot/")
    print("Godot 导出 APK 前置: 编辑器已装 Android Build Template (项目→安装导出模板)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
