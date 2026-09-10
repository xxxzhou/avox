#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""播放回归矩阵一键驱动: 供流 → 跑本平台 runner → 汇总判定行。

每次改动后跑一次, 确认"播放"链路(拉流/解码/帧输出/录制)没被搞坏。
用例表与判定口径在 tests/playmatrix/PlayMatrix.hpp, 各平台 runner 只是宿主;
本脚本负责把三件事串起来, 并复用 script/testenv 的 [AVOX][TEST] 判定行约定。

用法:
  python script/testenv/play_regress.py                  # 桌面全跑 (自动推流)
  python script/testenv/play_regress.py --no-push        # 已有流源, 不重推
  python script/testenv/play_regress.py --list           # 只列用例表
  python script/testenv/play_regress.py --skip=shot,webrtc-h265
  python script/testenv/play_regress.py --host=192.168.68.245   # 拉另一台 ZLM
  python script/testenv/play_regress.py --bin=<runner 路径>
  python script/testenv/play_regress.py --android --serial=<序列号>   # 真机 (push + adb shell)

退出码: 0 = 全过 (可接 CI), 1 = 有 FAIL 或环境不满足
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TESTENV = REPO_ROOT / "script" / "testenv"
COLLECT = TESTENV / "collect_verdicts.py"
PUSH = TESTENV / "push_streams.py"
ANDROID_REMOTE = "/data/local/tmp/playmatrix"
ASSETS = ["webrtc_pull.mp4", "avox_electron.mp4"]

# 离线子集: 只吃仓库里的本地 mp4, 不需要 ZLM/局域网 —— CI 上可跑的那部分。
# 保留: file-h264/h265(硬解) · file-h264/h265-soft(软解) · shot(截图+图像质量) · rec-transcode-h264
OFFLINE_SKIP = [
    "rtsp-h264", "rtsp-h265", "rtmp-h264", "rtmp-h265", "hls-h264", "hls-h265",
    "ts-h264", "ts-h265", "rtsp-h264-zm", "rtsp-h265-zm",
    "rtsp-h264-soft", "rtsp-h265-soft",
    "webrtc-h264", "webrtc-h265", "frame-contract", "rec-copy-h264",
]


def local_env() -> dict:
    """去掉代理变量: 流源与 runner 全在本机/局域网, 走代理会把 127.0.0.1 请求打飞。"""
    env = os.environ.copy()
    for k in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "all_proxy"):
        env.pop(k, None)
    env["no_proxy"] = env["NO_PROXY"] = "127.0.0.1,localhost,::1"
    return env


def find_ffmpeg(explicit: str) -> str:
    """PATH 上没有 ffmpeg 时, 退回仓库 3rdparty 里预置的 (推流只需 remux)。"""
    if explicit:
        return explicit
    env = shutil.which("ffmpeg")
    if env:
        return env
    for pat in ("3rdparty/library/Windows/ffmpeg/bin*/ffmpeg.exe",
                "3rdparty/library/windows/ffmpeg/bin*/ffmpeg.exe"):
        for p in sorted(REPO_ROOT.glob(pat)):
            return str(p)
    return ""


def find_bin(kind: str) -> str:
    """取 build/ 下最新构建的 runner。

    kind: desktop -> playtest[.exe]; android -> playtest; apple -> avoxtest
    (Apple 侧宿主是 platform/ios/avoxtest, 出 iOS app + macOS 无头 CLI 两种形态)
    """
    if kind == "apple":
        names = ["avoxtest"]
    elif kind == "android":
        names = ["playtest"]
    else:
        names = ["playtest.exe"] if sys.platform == "win32" else ["playtest"]
    found = []
    for name in names:
        found += [p for p in (REPO_ROOT / "build").glob(f"**/{name}") if p.is_file()]
    # macOS .app 包里的可执行同名, 优先裸可执行
    if kind == "apple":
        found.sort(key=lambda p: (".app/" in str(p), -p.stat().st_mtime))
    else:
        found.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return str(found[0]) if found else ""


def detect_local_ip() -> str:
    """本机在局域网里的地址 (真机拉流要用它, 不是 127.0.0.1)。

    注意: 装了 VPN/TUN 代理的机器上 UDP-connect 会拿到 198.18.x 这类假地址,
    那里优先用网卡枚举里的私有网段; 都不对就用 --host / --lan-ip 显式指定。
    """
    cands = []
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            cands.append(info[4][0])
    except OSError:
        pass
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        cands.append(s.getsockname()[0])
    except OSError:
        pass
    finally:
        s.close()

    def rank(ip: str) -> int:
        if ip.startswith("192.168."):
            return 0
        if ip.startswith("10."):
            return 1
        if ip.startswith("172."):
            return 2
        if ip.startswith(("127.", "198.18.", "169.254.")):
            return 9
        return 3

    uniq = list(dict.fromkeys(cands))
    if not uniq:
        return "127.0.0.1"
    uniq.sort(key=rank)
    best = uniq[0]
    if rank(best) >= 9:
        print(f"[warn] 没找到局域网地址 (探测到 {best}), 真机会连不上; "
              f"请显式传 --host=<本机局域网IP> 与 --lan-ip=<同值>")
    return best


def run_streams(lan_ip: str, ffmpeg: str) -> bool:
    """推测试流; 失败只告警, 不阻塞 (网络用例会自己判 FAIL)。"""
    if not PUSH.is_file():
        print(f"[warn] 未找到 {PUSH}, 跳过推流")
        return False
    cmd = [sys.executable, str(PUSH)]
    if lan_ip:
        cmd.append(f"--lan-ip={lan_ip}")
    if ffmpeg:
        cmd.append(f"--ffmpeg={ffmpeg}")
    print(f"[streams] {' '.join(cmd)}")
    r = subprocess.run(cmd, cwd=str(REPO_ROOT), env=local_env())
    if r.returncode != 0:
        print("[warn] 推流未全成功, 继续 (网络用例可能 FAIL)")
        return False
    return True


def aggregate(output: str, out_md: str) -> int:
    """复用 collect_verdicts 的行约定与输出格式。"""
    collect = [sys.executable, str(COLLECT), "-"]
    if out_md:
        collect += ["--out", out_md]
    return subprocess.run(collect, input=output, text=True, encoding="utf-8",
                          errors="replace").returncode


def run_desktop(args, cmd) -> int:
    if args.list:
        return subprocess.run(cmd + ["--list"], env=local_env()).returncode
    if not args.no_push:
        # 桌面拉流用 127.0.0.1, 判定行里的地址保持默认即可
        run_streams(args.lan_ip, find_ffmpeg(args.ffmpeg))
    print(f"[run] {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True,
                          encoding="utf-8", errors="replace", env=local_env())
    output = (proc.stdout or "") + (proc.stderr or "")
    print(output.rstrip())
    code = aggregate(output, args.out)
    return 0 if proc.returncode == 0 and code == 0 else 1


def run_apple(args, runner: str) -> int:
    """Apple 宿主是 avoxtest (iOS app + macOS 无头 CLI), 不接受命令行开关:
    端点走 AVOX_HOST 环境变量, 产物目录走 AVOX_PM_OUT; 用例表是共享的那份。"""
    if args.list:
        print("[note] Apple 宿主用例表与其它平台共享, `playtest --list` 或看 "
              "tests/playmatrix/README.md")
        return 0
    env = local_env()
    if args.host:
        env["AVOX_HOST"] = args.host
    if args.outdir:
        env["AVOX_PM_OUT"] = args.outdir
    # 本地文件用例 (file-*) 要 <可执行目录>/wall_long.mp4 与 wall_265.mp4
    cwd = Path(runner).parent
    if not (cwd / "wall_long.mp4").is_file():
        print(f"[warn] {cwd}/wall_long.mp4 缺失 → file-* 用例会判 FAIL, "
              f"生成命令见 platform/ios/avoxtest/README.md")
    if not args.no_push:
        run_streams(args.lan_ip or args.host, find_ffmpeg(args.ffmpeg))
    print(f"[run] {runner}  (cwd={cwd})")
    proc = subprocess.run([runner], cwd=str(cwd), capture_output=True, text=True,
                          encoding="utf-8", errors="replace", env=env)
    output = (proc.stdout or "") + (proc.stderr or "")
    print(output.rstrip())
    code = aggregate(output, args.out)
    return 0 if proc.returncode == 0 and code == 0 else 1


def run_android(args, extra) -> int:
    adb = args.adb or shutil.which("adb") or ""
    if not adb:
        print("[error] 未找到 adb, 传 --adb=<路径>")
        return 1
    prefix = [adb] + ([f"-s{args.serial}"] if args.serial else [])

    def sh(*cmd):
        return subprocess.run(prefix + list(cmd), capture_output=True, text=True,
                              encoding="utf-8", errors="replace", env=local_env())

    devs = [l.split()[0] for l in sh("devices").stdout.splitlines()[1:]
            if l.strip() and l.split()[-1] == "device"]
    if not devs:
        print("[error] 没有在线的 adb 设备 (adb devices)")
        return 1
    if not args.serial:
        prefix.append(f"-s{devs[0]}")
    print(f"[android] device={devs[0]}")

    runner = Path(args.bin)
    install = runner.parent
    libs = sorted({p for p in install.rglob("*.so")})
    sh("shell", f"mkdir -p {ANDROID_REMOTE}/assets/video")
    pushes = [(runner, ANDROID_REMOTE)]
    pushes += [(p, ANDROID_REMOTE) for p in libs]
    for a in ASSETS:
        p = REPO_ROOT / "assets" / "video" / a
        if p.is_file():
            pushes.append((p, f"{ANDROID_REMOTE}/assets/video"))
    # 渲染资源: avox 在无 assetManager(控制台进程) 时从 <exe目录>/assets 找 shader/字体
    app_assets = REPO_ROOT / "platform" / "android" / "AvoxJava" / "avox" / "assets"
    for sub in ("glsl", "fonts"):
        if (app_assets / sub).is_dir():
            pushes.append((app_assets / sub, f"{ANDROID_REMOTE}/assets"))
    for src, dst in pushes:
        if sh("push", str(src), dst).returncode != 0:
            print(f"[error] push 失败: {src}")
            return 1
    print(f"[android] pushed {len(pushes)} items -> {ANDROID_REMOTE}")

    # Android 侧目前没编 webrtc 插件, 对应用例必红; 与其留噪音不如显式跳过并说明
    if not any(p.name == "libavox_webrtc.so" for p in libs):
        auto = ["webrtc-h264", "webrtc-h265"]
        print(f"[warn] 未找到 libavox_webrtc.so, 自动跳过 {','.join(auto)} "
              f"(需先让 plugins/avox_webrtc 支持 Android)")
        for i, a in enumerate(extra):
            if a.startswith("--skip="):
                extra[i] = a + "," + ",".join(auto)
                break
        else:
            extra.append("--skip=" + ",".join(auto))

    remote = (f"cd {ANDROID_REMOTE} && chmod +x ./{runner.name} && "
              f"LD_LIBRARY_PATH=. ./{runner.name} " + " ".join(extra))
    if args.list:
        r = sh("shell", remote)
        print(r.stdout + r.stderr)
        return r.returncode
    if not args.no_push:
        run_streams(args.lan_ip or detect_local_ip(), find_ffmpeg(args.ffmpeg))
    print(f"[run] adb shell \"{remote}\"")
    r = sh("shell", remote)
    output = (r.stdout or "") + (r.stderr or "")
    print(output.rstrip())
    return aggregate(output, args.out)


def main() -> int:
    ap = argparse.ArgumentParser(description="播放回归矩阵一键驱动")
    ap.add_argument("--bin", default="", help="runner 路径 (默认取 build/ 下最新 playtest)")
    ap.add_argument("--host", default="", help="拉流主机 (默认: 桌面 127.0.0.1, 真机取本机局域网 IP)")
    ap.add_argument("--lan-ip", default="", help="推流时给真机用的局域网 IP")
    ap.add_argument("--no-push", action="store_true", help="不推流 (假定源已就绪)")
    ap.add_argument("--offline", action="store_true",
                    help="离线子集: 跳过全部网络用例 (不需要 ZLM, 供 CI/无流源时跑)")
    ap.add_argument("--ffmpeg", default="", help="推流用 ffmpeg 路径 (默认 PATH, 再退回 3rdparty)")
    ap.add_argument("--skip", default="", help="跳过的 case id, 逗号分隔")
    ap.add_argument("--retries", type=int, default=3, help="拉流失败重开次数")
    ap.add_argument("--outdir", default="", help="录制/截图产物目录")
    ap.add_argument("--list", action="store_true", help="只列用例表")
    ap.add_argument("--out", default="", help="判定矩阵 markdown 落盘路径")
    ap.add_argument("--android", action="store_true", help="跑 Android 真机 (adb push + shell)")
    ap.add_argument("--apple", action="store_true",
                    help="跑 Apple 宿主 avoxtest (macOS 上自动启用)")
    ap.add_argument("--serial", default="", help="adb 设备序列号 (多设备时指定)")
    ap.add_argument("--adb", default="", help="adb 路径 (默认 PATH)")
    args = ap.parse_args()

    if args.offline:
        args.no_push = True
        merged = [s for s in args.skip.split(",") if s] + OFFLINE_SKIP
        args.skip = ",".join(dict.fromkeys(merged))
        print(f"[offline] 跳过 {len(OFFLINE_SKIP)} 条网络用例, 只跑本地文件子集")

    kind = "android" if args.android else (
        "apple" if (args.apple or sys.platform == "darwin") else "desktop")
    runner = args.bin or find_bin(kind)
    if not runner or not Path(runner).is_file():
        print(f"[error] 未找到 runner ({kind}), 先构建并传 --bin=<路径>")
        print("        Windows: python build_windows.py")
        print("        Android: python build_android.py  (产物 build/android/*/install/<abi>/)")
        print("        macOS:   见 platform/ios/avoxtest/README.md (产物 build/avoxtest)")
        return 1
    print(f"[runner] {runner} ({kind})")

    # Apple 侧只有环境变量通道, 单独走
    if kind == "apple":
        return run_apple(args, runner)

    # 真机: 本地源用 push 后的相对路径; 桌面: 直接给绝对路径
    if args.android:
        files = [f"--file-h264=assets/video/{a}" for a in ASSETS[:1]]
        files += [f"--file-h265=assets/video/{a}" for a in ASSETS[1:]]
        host = args.host or detect_local_ip()
    else:
        files = []
        for key, name in (("--file-h264", ASSETS[0]), ("--file-h265", ASSETS[1])):
            p = REPO_ROOT / "assets" / "video" / name
            if p.is_file():
                files.append(f"{key}={p}")
        host = args.host or "127.0.0.1"

    extra = [f"--host={host}", f"--retries={args.retries}"] + files
    if args.skip:
        extra.append(f"--skip={args.skip}")
    # 桌面默认把录制/截图产物收进 build/ 下, 免得散在仓库根目录
    outdir = args.outdir
    if not outdir and not args.android:
        outdir = str(REPO_ROOT / "build" / "playmatrix_out")
        Path(outdir).mkdir(parents=True, exist_ok=True)
    if outdir:
        extra.append(f"--outdir={outdir}")

    if args.android:
        return run_android(args, extra)
    return run_desktop(args, [runner] + extra)


if __name__ == "__main__":
    sys.exit(main())
