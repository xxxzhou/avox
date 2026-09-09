#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""注册/注销 Windows deep-link 协议到本播放器 (HKCU, 无需管理员)。

注册后浏览器/命令行点击 magnet: 或 avox:// 链接会拉起 godot 播放器。
main.gd 已支持: magnet:? 走解析流程; avox://<url> 剥壳后按内容路由
(avox://rtsp://... 直接播放, avox://magnet:?... 走解析)。

用法:
  python register_magnet_protocol.py            # 注册 magnet + avox
  python register_magnet_protocol.py magnet     # 只注册指定 scheme
  python register_magnet_protocol.py --remove   # 注销全部
可选环境变量: GODOT_BIN, GODOT_PROJECT (默认本仓库 tools 工程)
"""
import os
import sys
import winreg
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCHEMES = ("magnet", "avox")


def godot_bin() -> str:
    env = os.environ.get("GODOT_BIN")
    if env and Path(env).is_file():
        return env
    for cand in [r"D:\Work\godot\godot.exe", r"C:\Program Files\Godot\godot.exe"]:
        if Path(cand).is_file():
            return cand
    raise SystemExit("未找到 godot.exe, 设 GODOT_BIN 环境变量后重试")


def register_one(exe: str, project: str, scheme: str) -> None:
    key = rf"Software\Classes\{scheme}"
    cmd = f'"{exe}" --path "{project}" -- "%1"'
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, f"URL:{scheme} Protocol")
        winreg.SetValueEx(k, "URL Protocol", 0, winreg.REG_SZ, "")
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key + r"\DefaultIcon") as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, exe)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key + r"\shell\open\command") as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, cmd)
    print(f"已注册 {scheme}: 协议 -> {cmd}")


def remove_one(scheme: str) -> None:
    key = rf"Software\Classes\{scheme}"
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key + r"\shell\open\command")
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key + r"\shell\open")
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key + r"\shell")
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key + r"\DefaultIcon")
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key)
    print(f"已注销 {scheme}: 协议")


def main() -> int:
    remove = "--remove" in sys.argv
    schemes = tuple(a for a in sys.argv[1:] if a in SCHEMES) or SCHEMES
    if remove:
        for scheme in schemes:
            try:
                remove_one(scheme)
            except FileNotFoundError:
                print(f"{scheme}: 协议未注册, 跳过")
        return 0
    exe = godot_bin()
    project = os.environ.get("GODOT_PROJECT", str(REPO_ROOT / "platform" / "godot" / "tools"))
    for scheme in schemes:
        register_one(exe, project, scheme)
    return 0


if __name__ == "__main__":
    sys.exit(main())
