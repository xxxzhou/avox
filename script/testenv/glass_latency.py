#!/usr/bin/env python3
"""Glass-to-glass latency probe: a Python-generated source burns its wallclock
into every frame (Pillow, no ffmpeg drawtext/fontconfig), ffmpeg just encodes
and pushes. Receiver saves screenshots; latency = png_mtime - burned_wallclock.

Method:
  1. Python paces 30fps frames, draws generation wallclock (epoch, ms) on each,
     pipes rawvideo to ffmpeg (libx264 zerolatency) pushing live/lat via RTMP.
  2. For each protocol, run a receiver that saves screenshots; record each PNG's
     mtime (capture moment) and copy the file. The burned epoch is read from the
     image later; latency = capture_mtime - burned_epoch (+ encode/jitter only).

Usage:
  python script/testenv/glass_latency.py                        # all protocols
  python script/testenv/glass_latency.py --protocols rtsp,rtc   # subset
  python script/testenv/glass_latency.py --samples 3

Prereqs: local ZLMediaKit (80/554/1935), ffmpeg in PATH, Pillow; RTC uses the
webrtcplaytest sample (screenshot-enabled build).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from push_streams import find_secret  # 同一套 config.ini 嗅探

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REL = os.path.join(ROOT, "build", "windows", "avox", "install", "AMD64", "Release")
SHOTS = os.path.join(ROOT, "deploy", "latency_shots")
STREAM = "live/lat"
PUSH_PROC = None  # push_source 的 ffmpeg 子进程 (feeder 线程内赋值)
ZLM = "http://127.0.0.1"
FONT = "C:/Windows/Fonts/arial.ttf"


def zlm_online(secret, stream):
    try:
        with urllib.request.urlopen(
                f"{ZLM}/index/api/getMediaList?secret={secret}", timeout=2) as r:
            data = json.loads(r.read().decode("utf-8", "ignore"))
        for m in data.get("data") or []:
            if m.get("app") == "live" and m.get("stream") == stream:
                return True
    except Exception:
        pass
    return False


def pts_to_sec(text):
    """epoch float text like '1757487123.456' -> float"""
    try:
        return float(text.strip())
    except ValueError:
        return None


def push_source(duration_s, stream):
    """Python-paced 30fps source with burned generation wallclock -> ffmpeg -> RTMP.
    BLOCKS for duration_s feeding frames - run it in a background thread.
    stream: ZLM stream name (use a fresh name per run; a name with a half-dead
    session on ZLM gets new publishers silently rejected).
    """
    from PIL import Image, ImageDraw, ImageFont
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg not in PATH")
    errlog = open(os.path.join(ROOT, "deploy", "latency_push_ffmpeg.log"), "w")
    proc = subprocess.Popen([
        ffmpeg, "-hide_banner", "-loglevel", "warning",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1280x720", "-r", "30", "-i", "-",
        # H265 源: 沿用首次测量口径。当初怕的 W0「ff_h264_dx11 解码必崩」09-10 已定界为
        # 帧布局重构的中间构建态 (矩阵 W0 已标解决), 09-11 复测 RTSP/RTMP H264 -hard
        # 全链 nv12 直出 PASS; 换 H264 会动延迟数字口径, 故源保持 H265
        # 显式 yuv420p: x265 对 rgb24 默认编 4:4:4, 播放器 offSurface 要 420 会导致 0 帧
        "-c:v", "libx265", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
        "-x265-params", "keyint=30:min-keyint=30:scenecut=0:bframes=0:rc-lookahead=10",
        "-b:v", "2500k", "-f", "flv", "rtmp://127.0.0.1:1935/live/" + stream,
    ], stdin=subprocess.PIPE, stderr=errlog)
    global PUSH_PROC
    PUSH_PROC = proc
    font = ImageFont.truetype(FONT, 64)
    small = ImageFont.truetype(FONT, 28)
    frame = Image.new("RGB", (1280, 720))
    draw = ImageDraw.Draw(frame)
    # static pattern so motion is visible
    for x in range(0, 1280, 80):
        draw.rectangle([x, 200, x + 40, 719], fill=(60, 60 + (x // 8) % 160, 160))
    burn_times = []
    t_start = time.time()
    n = 0
    while True:
        elapsed = time.time() - t_start
        if elapsed >= duration_s:
            break
        ts = time.time()
        burn_times.append(ts)
        draw.rectangle([0, 0, 1279, 199], fill=(20, 20, 20))
        draw.text((30, 30), f"{ts:.3f}", font=font, fill=(80, 255, 80))
        draw.text((900, 60), f"frame {n}", font=small, fill=(255, 255, 255))
        proc.stdin.write(frame.tobytes())
        n += 1
        # pace at 30fps against our own clock
        target = t_start + (n + 1) / 30.0
        delta = target - time.time()
        if delta > 0:
            time.sleep(delta)
    proc.stdin.close()
    return proc


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--protocols", default="rtsp,rtmp,hls,rtc")
    ap.add_argument("--samples", type=int, default=3)
    args = ap.parse_args()
    protos = [p.strip() for p in args.protocols.split(",") if p.strip()]
    secret = os.environ.get("ZLM_SECRET") or find_secret("")
    os.makedirs(SHOTS, exist_ok=True)
    # fresh stream name per run: a name with a half-dead ZLM session rejects new publishers
    stream = f"lat{time.strftime('%H%M%S')}"

    # feeder blocks for the whole duration - run it in the background
    feeder = threading.Thread(target=push_source, args=(300, stream), daemon=True)
    feeder.start()
    while PUSH_PROC is None:  # feeder 线程创建 ffmpeg 后才写入
        time.sleep(0.01)
    push = PUSH_PROC
    t0 = time.time()
    online = False
    for _ in range(100):
        if push.poll() is not None:
            raise SystemExit(
                f"push ffmpeg exited early (code={push.returncode}); "
                "check deploy/latency_push_ffmpeg.log - another publisher may hold "
                f"live/{stream} (taskkill /F /IM ffmpeg.exe)")
        if zlm_online(secret, stream):
            online = True
            break
        time.sleep(0.1)
    if not online:
        push.kill()
        raise SystemExit("stream did not come online on ZLM within 10s")
    print(f"[lat] stream online {time.time() - t0:.2f}s after push start")

    results = {}

    def record(tag, capture_epoch, src_png):
        dst = os.path.join(SHOTS, f"{tag}.png")
        shutil.copyfile(src_png, dst)
        results.setdefault(tag.split("_")[0], []).append(
            {"capture": round(capture_epoch, 3), "png": dst})
        print(f"[lat] {tag}: capture={capture_epoch:.3f} png={dst}")

    def probe_shot(url, total_s, tag, want):
        """latprobe overwrites <prefix>cur.png every 500ms after 3s warmup;
        collect mtime-change samples (decode-output frames)."""
        cur = os.path.join(SHOTS, f"{tag}_cur.png")
        if os.path.exists(cur):
            os.remove(cur)
        proc = subprocess.Popen([
            os.path.join(REL, "latprobe.exe"), url, str(total_s),
            os.path.join(SHOTS, tag)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        got = 0
        last_mtime = 0
        deadline = time.time() + total_s + 5
        while got < want and time.time() < deadline:
            if os.path.exists(cur):
                mt = os.stat(cur).st_mtime
                if mt != last_mtime:
                    if last_mtime:  # skip the first (warmup frame)
                        record(f"{tag}_{got}", mt, cur)
                        got += 1
                    last_mtime = mt
            time.sleep(0.02)
        proc.wait(timeout=total_s + 10)

    def rtc_shots(url, total_ms, tag, want):
        """webrtcplaytest overwrites one png every 500ms; collect mtime-change samples."""
        png = os.path.join(SHOTS, f"{tag}_cur.png")
        if os.path.exists(png):
            os.remove(png)
        proc = subprocess.Popen([
            os.path.join(REL, "webrtcplaytest.exe"), url, str(total_ms), png],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        got = 0
        last_mtime = 0
        deadline = time.time() + total_ms / 1000 + 5
        while got < want and time.time() < deadline:
            if os.path.exists(png):
                mt = os.stat(png).st_mtime
                if mt != last_mtime:
                    if last_mtime:  # skip the very first (may predate first frame)
                        record(f"{tag}_{got}", mt, png)
                        got += 1
                    last_mtime = mt
            time.sleep(0.02)
        proc.wait(timeout=total_ms / 1000 + 10)

    try:
        for proto in protos:
            if proto == "rtc":
                rtc_shots(
                    f"{ZLM}/index/api/webrtc?app=live&stream={stream}&type=play",
                    12000, "rtc", want=args.samples)
                continue
            url = {"rtsp": f"rtsp://127.0.0.1:554/live/{stream}",
                   "rtmp": f"rtmp://127.0.0.1:1935/live/{stream}",
                   "hls": f"{ZLM}/live/{stream}/hls.m3u8"}[proto]
            probe_shot(url, 14, proto, want=args.samples)
    finally:
        push.kill()

    with open(os.path.join(SHOTS, "manifest.json"), "w") as f:
        json.dump({"samples": results}, f, indent=1)
    print(f"\n[lat] read the burned epoch from each png under {SHOTS}; "
          f"latency = capture - burned (seconds)")
    print("[lat] manifest ->", os.path.join(SHOTS, "manifest.json"))


if __name__ == "__main__":
    main()
