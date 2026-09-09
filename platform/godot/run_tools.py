#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""avox Godot 工具箱 launcher —— 直接启动 hub / 播放器 / 语音输入。

用法:
  python run_tools.py                 # 交互菜单选目标
  python run_tools.py hub             # 主界面 (project.godot 默认主场景)
  python run_tools.py player          # 播放器
  python run_tools.py voice           # 语音输入
  python run_tools.py player --editor # 以编辑器打开 (而非直接运行)
  python run_tools.py --list-engines  # 列出引擎查找结果
  python run_tools.py player --godot D:\\Work\\godot\\godot.exe

引擎查找优先级: --godot 参数 > GODOT 环境变量 > Release 里已 pack 的 godot >
                PATH 中的 godot > 开发机默认 D:\\Work\\godot\\godot.exe。
工作目录设为 avox Release 根 (avox.dll / ffmpeg / plugins 的解析来源, 与 tools.lnk 的
WorkingDir 一致); 启动后本进程会阻塞直到 Godot 关闭 (开发期可看到引擎 stderr/Ctrl+C)。
依赖: tools/addons/avox_godot/bin junction -> Release (plugin/deploy_godot.ps1 建立)。
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# 脚本位于 platform/godot/, 项目根 = 上两级
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
TOOLS_PROJECT = SCRIPT_DIR / "tools"          # Godot 项目根 (含 project.godot)

# avox Release (dll home): Windows 构建输出; bin junction 指向这里
RELEASE_CANDIDATES = [
    REPO_ROOT / "build" / "windows" / "avox" / "install" / "AMD64" / "Release",
    REPO_ROOT / "build" / "windows" / "avox" / "install" / "x86_64" / "Release",
]

# 引擎固定候选 (开发机); 另会查 Release 内 pack 过的副本 + PATH
ENGINE_FIXED_CANDIDATES = [
    Path(r"D:\Work\godot\godot.exe"),
    Path.home() / "godot" / "godot.exe",
]

# 启动目标: scene=None 表示走 project.godot 的 run/main_scene (即 hub)
TARGETS: dict[str, dict] = {
    "hub":    {"title": "主界面  (hub)", "scene": None},
    "player": {"title": "播放器",        "scene": "res://src/mediaplayer/main.tscn"},
}


def _engine_names() -> list[str]:
    # Windows 优先 godot.exe; 其它平台优先 godot。两个都试保证跨平台。
    return ["godot.exe", "godot"] if os.name == "nt" else ["godot", "godot.exe"]


def find_release() -> Path | None:
    for c in RELEASE_CANDIDATES:
        if (c / "avox_godot.dll").is_file():   # 优先返回真正含产物的目录
            return c
    for c in RELEASE_CANDIDATES:               # 退而求其次: 目录存在即可
        if c.is_dir():
            return c
    return None


def resolve_explicit(src: str | None) -> Path | None:
    # 解析显式指定的路径 (--godot 参数 / GODOT 环境变量)。给了目录则在目录里找引擎。
    # 返回 None = 未给出; 给了但无效 (文件/目录里都没有 godot) 也返回 None, 由调用方决定报错与否。
    if not src:
        return None
    p = Path(src).expanduser()
    if p.is_file():
        return p
    if p.is_dir():
        for n in _engine_names():
            if (p / n).is_file():
                return p / n
    return None


def find_engine() -> Path | None:
    # 自动查找 (不含 --godot 参数): GODOT 环境变量 > Release 内 pack 副本 > PATH > 固定候选
    e = resolve_explicit(os.environ.get("GODOT"))
    if e:
        return e
    rel = find_release()
    if rel:
        for n in _engine_names():
            if (rel / n).is_file():
                return rel / n
    for n in _engine_names():
        w = shutil.which(n)
        if w:
            return Path(w)
    for c in ENGINE_FIXED_CANDIDATES:
        if c.is_file():
            return c
    return None


def _engine_candidates() -> list[tuple[str, Path]]:
    """供 --list-engines: (来源描述, 路径), 含已找到与未找到。"""
    rows: list[tuple[str, Path]] = []
    env = os.environ.get("GODOT")
    if env:
        rows.append(("GODOT 环境变量", Path(env).expanduser()))
    rel = find_release()
    if rel:
        for n in _engine_names():
            rows.append((f"Release 内 {n}", rel / n))
    for n in _engine_names():
        w = shutil.which(n)
        if w:
            rows.append((f"PATH ({n})", Path(w)))
    for c in ENGINE_FIXED_CANDIDATES:
        rows.append(("固定候选", c))
    return rows


def preflight(project: Path, release: Path | None) -> list[str]:
    """返回阻断性问题 (空列表 = 一切就绪)。"""
    probs: list[str] = []
    if not (project / "project.godot").is_file():
        probs.append(f"找不到 Godot 项目: {project} (缺 project.godot)")
    bin_link = project / "addons" / "avox_godot" / "bin"
    if not bin_link.exists():
        probs.append(
            f"插件 bin 未就位: {bin_link}\n"
            f"      先部署: powershell -File platform/godot/plugin/deploy_godot.ps1 "
            f"-GodotProject platform/godot/tools"
        )
    if release is None or not (release / "avox_godot.dll").is_file():
        probs.append(
            "找不到 avox_godot.dll —— 先构建 avox (AVOX_ENABLE_GODOT),\n"
            "      产物应在 build/windows/avox/install/AMD64/Release/"
        )
    return probs


def launch(target: str, engine: Path, project: Path,
           release: Path | None, editor: bool) -> int:
    scene = TARGETS[target]["scene"]
    cmd = [str(engine)]
    if editor:
        cmd.append("--editor")                 # 编辑器模式忽略场景参数
    cmd += ["--path", str(project)]
    if scene and not editor:
        cmd.append(scene)
    # 工作目录 = Release (dll 解析来源); 退化到项目根, 最后才是引擎所在目录
    if release and release.is_dir():
        cwd = release
    elif project.is_dir():
        cwd = project
    else:
        cwd = engine.parent
    # Godot print() 与 avox 内部日志(gLogOb==null 时走 std::cout)都进进程 stdout/stderr ——
    # tee 到 Release/logs/godot_<ts>.log 落盘(avox 侧 IOParseFF 等日志此前随终端关闭丢失),
    # 同时回显终端保留开发期实时输出。终端 utf-8 重配避免中文日志乱码。
    log_path = None
    log_file = None
    if release and release.is_dir():
        log_dir = release / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        log_path = log_dir / f"godot_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        log_file = open(log_path, "w", encoding="utf-8", errors="replace")
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    print(f"==> 启动 {TARGETS[target]['title']}")
    print(f"    引擎    : {engine}")
    print(f"    项目    : {project}")
    if scene and not editor:
        print(f"    场景    : {scene}")
    print(f"    工作目录: {cwd}")
    if log_path:
        print(f"    日志    : {log_path}")
    try:
        # 注入 avox log 路径 env: 插件 init 时读 AVOX_GODOT_LOG_PATH, 让 avox::setLogObserver
        # 直接挂到这个文件, 与 Godot stdout 同源合并 (否则插件会在 dll 目录另起一份自己的
        # godot_<ts>.log, 两个文件各记一半)。
        env = None
        if log_path:
            env = os.environ.copy()
            env["AVOX_GODOT_LOG_PATH"] = str(log_path)
        proc = subprocess.Popen(cmd, cwd=str(cwd), stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", bufsize=1,
                                env=env)
        for line in proc.stdout:
            sys.stdout.write(line)
            if log_file:
                log_file.write(line)
        return proc.wait()
    except KeyboardInterrupt:
        return 130
    finally:
        if log_file:
            log_file.close()


def interactive_menu() -> str | None:
    print("\navc Godot 工具箱 — 选择要启动的工具:\n")
    keys = list(TARGETS)
    for i, k in enumerate(keys, 1):
        print(f"  {i}. {TARGETS[k]['title']}")
    print("  q. 退出")
    choice = input("\n> ").strip().lower()
    if choice in ("q", "quit", "exit"):
        return None
    if choice.isdigit() and 1 <= int(choice) <= len(keys):
        return keys[int(choice) - 1]
    if choice in TARGETS:
        return choice
    print(f"无效选择: {choice!r}")
    return None


def main() -> int:
    ap = argparse.ArgumentParser(
        prog="run_tools.py",
        description="avox Godot 工具箱 launcher (hub / 播放器 / 语音输入)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("target", nargs="?", choices=list(TARGETS),
                    help="启动目标; 省略则进入交互菜单")
    ap.add_argument("--godot", help="godot 引擎路径, 覆盖自动查找")
    ap.add_argument("--editor", action="store_true", help="以编辑器打开项目 (不直接运行场景)")
    ap.add_argument("--list-engines", action="store_true", help="列出引擎查找结果并退出")
    args = ap.parse_args()

    if args.list_engines:
        print("godot 引擎查找候选:")
        rows: list[tuple[str, Path]] = []
        if args.godot:
            rows.append(("--godot 参数", Path(args.godot).expanduser()))
        rows.extend(_engine_candidates())
        for src, p in rows:
            mark = "OK " if p.is_file() else "-- "
            print(f"  {mark} {src}: {p}")
        found = resolve_explicit(args.godot) if args.godot else find_engine()
        print(f"\n将使用: {found}" if found else "\n未找到任何 godot 引擎 (用 --godot 或设 GODOT)。")
        return 0 if found else 2

    release = find_release()
    if args.godot:
        engine = resolve_explicit(args.godot)
        if engine is None:
            print(f"--godot 指定的引擎不存在: {args.godot}")
            return 2
    else:
        engine = find_engine()
        if engine is None:
            print("未找到 godot 引擎。可用 --godot 指定, 或设环境变量 GODOT, 或把 godot 加进 PATH。")
            print("  开发机默认查 D:\\Work\\godot\\godot.exe (run_tools.py --list-engines 看全部候选)")
            return 2

    probs = preflight(TOOLS_PROJECT, release)
    if probs:
        print("启动前置检查未通过:")
        for p in probs:
            print(f"  - {p}")
        return 3

    target = args.target
    if target is None:
        target = interactive_menu()
        if target is None:
            return 0

    return launch(target, engine, TOOLS_PROJECT, release, args.editor)


if __name__ == "__main__":
    sys.exit(main())
