#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""注册/注销 Windows magnet: 协议到本播放器 (HKCU, 无需管理员)。

注册后浏览器点击磁力链接会拉起 godot 播放器并直接进解析流程。
main.gd 已支持: 命令行 user args 里 magnet:? 开头自动 _open_probe_for。

用法:
  python register_magnet_protocol.py            # 注册 (自动定位 godot.exe 与 tools 工程)
  python register_magnet_protocol.py --remove   # 注销
可选环境变量: GODOT_BIN, GODOT_PROJECT (默认本仓库 tools 工程)
"""
import os
import sys
import winreg
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
KEY = r"Software\Classes\magnet"


def godot_bin() -> str:
    env = os.environ.get("GODOT_BIN")
    if env and Path(env).is_file():
        return env
    for cand in [r"D:\Work\godot\godot.exe", r"C:\Program Files\Godot\godot.exe"]:
        if Path(cand).is_file():
            return cand
    raise SystemExit("未找到 godot.exe, 设 GODOT_BIN 环境变量后重试")


def main() -> int:
    remove = "--remove" in sys.argv
    if remove:
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY + r"\shell\open\command")
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY + r"\shell\open")
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY + r"\shell")
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY + r"\DefaultIcon")
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY)
        print("已注销 magnet 协议")
        return 0
    exe = godot_bin()
    project = os.environ.get("GODOT_PROJECT", str(REPO_ROOT / "platform" / "godot" / "tools"))
    cmd = f'"{exe}" --path "{project}" -- "%1"'
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY) as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, "URL:magnet Protocol")
        winreg.SetValueEx(k, "URL Protocol", 0, winreg.REG_SZ, "")
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY + r"\DefaultIcon") as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, exe)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY + r"\shell\open\command") as k:
        winreg.SetValueEx(k, None, 0, winreg.REG_SZ, cmd)
    print(f"已注册 magnet 协议 -> {cmd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
