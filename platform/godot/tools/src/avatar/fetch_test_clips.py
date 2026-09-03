#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
faceavatar 工具测试视频下载器 (视频 → MediaPipe → ARKit52 → 驱动 avatar)
=====================================================================
搬到任何机器都能跑: 纯标准库, 无需 pip install。

自动下载 (已逐一 HEAD 验证可直链, 稳定):
    Intel IoT DevKit sample-videos —— 正脸 / 头部姿态 / 多人片段。
    全部来自 https://github.com/intel-iot-devkit/sample-videos (raw.githubusercontent.com)。

    这批偏「头部转动 / 多脸」, 用来验证 landmarker 稳定性 + 头部跟随最合适。
    想测「说话口型 / viseme / 大表情」, 没有稳定直链 —— 跑 `--manual` 看手动获取指引。

用法:
    python fetch_test_clips.py                 # 下载全部到 ./test_clips
    python fetch_test_clips.py --list          # 只看目录, 不下
    python fetch_test_clips.py --only headpose # 按 tag 选 (headpose/single/multiface)
    python fetch_test_clips.py --only head-pose-face-detection-female.mp4
    python fetch_test_clips.py --dir D:\\clips # 指定输出目录
    python fetch_test_clips.py --force         # 已存在也重下
    python fetch_test_clips.py --manual        # 打印说话/表情类的手动获取指引

特性: 断点续传 (HTTP Range) / 跳过已完成 / 进度条 / 单个失败不影响其它 / 结尾汇总。
"""

import argparse
import sys
import urllib.error
import urllib.request
from pathlib import Path

# GitHub raw 对默认 urllib UA 一般不拦, 加个 UA 更稳
_HEADERS = {"User-Agent": "avox-fetch-test-clips/1.0"}
_CHUNK = 64 * 1024

# ── 目录 (已验证直链, 全部返回 206) ──────────────────────────────────────
# fmt: off
CLIPS = [
    {"file": "head-pose-face-detection-female.mp4",
     "tags": ["headpose", "single"],
     "desc": "正脸女性, 头部左右转动 (测 landmarker 头部跟随 / 鲁棒性)"},
    {"file": "head-pose-face-detection-male.mp4",
     "tags": ["headpose", "single"],
     "desc": "正脸男性, 头部转动"},
    {"file": "head-pose-face-detection-female-and-male.mp4",
     "tags": ["headpose", "multiface"],
     "desc": "男女交替正脸"},
    {"file": "face-demographics-walking-and-pause.mp4",
     "tags": ["multiface"],
     "desc": "多人走动 + 停顿, 脸更稳 (测多脸跟踪)"},
    {"file": "face-demographics-walking.mp4",
     "tags": ["multiface"],
     "desc": "多人走动, 多张脸"},
    {"file": "one-by-one-person-detection.mp4",
     "tags": ["single"],
     "desc": "单人逐个出现"},
    {"file": "classroom.mp4",
     "tags": ["multiface"],
     "desc": "教室多人 (脸较小较远, 压测用)"},
]
# fmt: on
_REPO = "https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/"

# ── 说话 / 表情类: 没有稳定直链, 手动获取 ─────────────────────────────────
MANUAL = """\
[说话 / 表情 / 口型类] 无稳定直链, 手动获取后放进 test_clips/ 即可:

1) Pixabay (免版税, 直接 Download):  https://pixabay.com/videos/search/talking%20face/
   / 女性正脸:  https://pixabay.com/videos/search/woman's%20face/
2) Pexels (免版税, 量大):  https://www.pexels.com/search/videos/talking%20head/
3) YouTube ARKit blendshape 演示 (表情全量程), 用 yt-dlp 抓:
   yt-dlp -f mp4 -o "test_clips/arkit_testchan.%%(ext)s" \\
     "https://www.youtube.com/watch?v=byhSLHOBTOQ"
   yt-dlp -f mp4 -o "test_clips/arkit_unity.%%(ext)s" \\
     "https://www.youtube.com/watch?v=34zWbn0eOwE"

挑片标准 (配合本管线 192×192 缩放, 480p 足够):
    正面镜头 / 顺光 / 脸占画面 ≥1/4 / 嘴部清晰不遮挡 / 5–30s 短片段迭代最省事。
"""


def human_size(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f}B" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024
    return f"{n:.1f}GB"


def remote_size(url: str):
    """HEAD 取总大小; 不支持就回退 Range: bytes=0-0 读 Content-Range; 都不行返回 None。"""
    try:
        with urllib.request.urlopen(
            urllib.request.Request(url, method="HEAD", headers=_HEADERS), timeout=30
        ) as r:
            cl = r.headers.get("Content-Length")
            if cl:
                return int(cl)
    except Exception:
        pass
    try:
        with urllib.request.urlopen(
            urllib.request.Request(url, headers={**_HEADERS, "Range": "bytes=0-0"}), timeout=30
        ) as r:
            cr = r.headers.get("Content-Range")  # 形如 bytes 0-0/12345
            if cr and "/" in cr:
                return int(cr.rsplit("/", 1)[1])
    except Exception:
        pass
    return None


def _progress(name: str, done: int, total) -> None:
    d, t = human_size(done), (human_size(total) if total else "?")
    if total:
        pct = done * 100 // total
        sys.stderr.write(f"\r  {name[:26]:<26} {d:>9} / {t:<9} {pct:3d}%")
    else:
        sys.stderr.write(f"\r  {name[:26]:<26} {d:>9} / {t:<9}")
    sys.stderr.flush()


def download(url: str, dest: Path, force: bool):
    """返回 (状态, 说明)。状态: ok / skip / fail。支持断点续传。"""
    total = remote_size(url)
    start = 0
    if dest.exists():
        if force:
            dest.unlink()
        elif total is None:
            return ("skip", "远端大小未知, 不覆盖 (用 --force 重下)")
        else:
            start = dest.stat().st_size
            if start >= total:
                return ("skip", f"已完整 {human_size(total)}")
            if start > total:  # 本地比远端还大, 文件异常, 重下
                dest.unlink()
                start = 0

    # 带 Range 续传; 若服务器忽略 Range (回 200 全量) 则从头
    headers = dict(_HEADERS)
    if start > 0:
        headers["Range"] = f"bytes={start}-"
    try:
        resp = urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60)
    except urllib.error.HTTPError as e:
        return ("fail", f"HTTP {e.code}")
    except Exception as e:
        return ("fail", f"{type(e).__name__}: {e}")

    if start > 0 and getattr(resp, "status", None) != 206:
        # 服务器没认 Range, 关掉重开全量
        resp.close()
        start = 0
        try:
            resp = urllib.request.urlopen(
                urllib.request.Request(url, headers=_HEADERS), timeout=60
            )
        except Exception as e:
            return ("fail", f"重开全量失败: {type(e).__name__}: {e}")

    mode = "ab" if start > 0 else "wb"
    done = start
    try:
        with open(dest, mode) as f:
            while True:
                chunk = resp.read(_CHUNK)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                _progress(dest.name, done, total)
        sys.stderr.write("\n")
    except Exception as e:
        sys.stderr.write("\n")
        return ("fail", f"下载中断: {type(e).__name__}: {e}")
    finally:
        resp.close()
    tag = " (续传)" if start > 0 else ""
    return ("ok", f"{human_size(done)}{tag}")


def select(only: str):
    if not only:
        return CLIPS
    keys = {k.strip().lower() for k in only.split(",") if k.strip()}
    out = [
        c for c in CLIPS
        if c["file"].lower() in keys or any(t in keys for t in c["tags"])
    ]
    if not out:
        sys.exit(f"无匹配 (--only {only}); 用 --list 看可选 file/tag")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="faceavatar 测试视频下载器", formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", help="输出目录 (默认: 脚本旁 test_clips/)")
    ap.add_argument("--only", help="只下部分: tag(headpose/single/multiface) 或文件名, 逗号分隔")
    ap.add_argument("--list", action="store_true", help="只列目录, 不下载")
    ap.add_argument("--force", action="store_true", help="已存在也重下")
    ap.add_argument("--manual", action="store_true", help="打印说话/表情类的手动获取指引")
    args = ap.parse_args()

    if args.manual:
        print(MANUAL)
        return 0

    if args.list:
        print(f"共 {len(CLIPS)} 段 (源: {_REPO})\n")
        for c in CLIPS:
            print(f"  {c['file']:<46} [{','.join(c['tags'])}]")
            print(f"      {c['desc']}")
        print("\n说话/表情类无直链 → python fetch_test_clips.py --manual")
        return 0

    out = Path(args.dir).resolve() if args.dir else (Path(__file__).resolve().parent / "test_clips")
    out.mkdir(parents=True, exist_ok=True)
    clips = select(args.only)

    print(f"下载 {len(clips)} 段 → {out}\n")
    results = []
    for c in clips:
        url = _REPO + c["file"]
        dest = out / c["file"]
        status, msg = download(url, dest, args.force)
        mark = {"ok": "✓", "skip": "=", "fail": "✗"}[status]
        results.append((c["file"], status))
        print(f"  {mark} [{status}] {c['file']}  {msg}")

    ok = sum(1 for _, s in results if s == "ok")
    sk = sum(1 for _, s in results if s == "skip")
    fl = sum(1 for _, s in results if s == "fail")
    print(f"\n完成: 新下 {ok} · 跳过 {sk} · 失败 {fl}")
    if fl:
        print("失败文件:")
        for f, s in results:
            if s == "fail":
                print(f"    {f}")
    return 0 if fl == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
