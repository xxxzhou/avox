#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""torrent_bench.py — 磁力/torrent 播放链路分步基准驱动

配套 samples/functest/torrentbench.cpp (分步计时: 探测文件列表/起播/稳态追帧/seek恢复)。
每次优化前后各跑一轮, compare 出分步差值, 确认每步优化的实际收益。
脚本同时解析引擎日志, 给出每阶段窗口内的下载速率/peer数等量化指标。

用法(仓库根目录):
  python script/torrent/torrent_bench.py list                        # 查看语料
  python script/torrent/torrent_bench.py run --tag bbb --fresh       # 冷启动单测(清缓存)
  python script/torrent/torrent_bench.py run --tag bbb --rounds 3    # 多轮(受swarm波动,取中位数)
  python script/torrent/torrent_bench.py run --all --probe-only      # 只测文件列表阶段
  python script/torrent/torrent_bench.py aggregate --tags bbb --keep 3   # 最近K轮中位数汇总
  python script/torrent/torrent_bench.py report                      # 汇总最近一轮
  python script/torrent/torrent_bench.py compare old.json new.json   # 分步对比
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
EXE = REPO / "build/windows/avox/install/AMD64/Release/torrentbench.exe"
OUT_DIR = REPO / "results/torrent_bench"
LOG_DIR = OUT_DIR / "logs"

# ---- 测试语料: 全部为 LibreContent/公有领域, 合法且 swarm 健康 ----
# bbb/sintel/cosmos/steel 是 webtorrent 项目的经典测试种子(infohash 广为流传),
# BT swarm 常年有源; bbb_multi 是 archive.org 条目种子(多文件, 官方长期做种),
# 专门覆盖多文件种子的 piece 偏移换算路径。
CORPUS = {
    "bbb": {
        "url": ("magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c"
                "&dn=Big+Buck+Bunny&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"),
        "note": "Big Buck Bunny 1080p (单文件磁力)",
    },
    "sintel": {
        "url": ("magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10"
                "&dn=Sintel&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"),
        "note": "Sintel 1080p (单文件磁力, 体积较大)",
    },
    "cosmos": {
        "url": ("magnet:?xt=urn:btih:c9e15763f722f23e98a29decdfae341b98d53056"
                "&dn=Cosmos+Laundromat+First+Cycle&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"),
        "note": "Cosmos Laundromat (单文件磁力)",
    },
    "steel": {
        "url": ("magnet:?xt=urn:btih:209c8226b299b308beaf2b9cd3fb49212dbd13ec"
                "&dn=Tears+of+Steel&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"),
        "note": "Tears of Steel (AV1/Opus, SDK无解码器→验证无轨道快速报错路径)",
    },
    "bbb_multi": {
        "url": "https://archive.org/download/BigBuckBunny_124/BigBuckBunny_124_archive.torrent",
        "note": "archive.org 多文件 .torrent (选中文件为种子第9个/偏移62KB, 覆盖多文件偏移换算)",
        "remote_torrent": True,
    },
}

STAGE_ORDER = [
    "probe_ms", "probe_fail", "open_ready_ms", "open_ready_fail",
    "playing_ms", "playing_fail", "steady_ms",
    "seek25_ms", "seek50_ms", "seek75_ms",
]
METRIC_ORDER = [
    "meta_source", "ph_metadata_ready", "ph_head_ready", "ph_tail_done",
    "ph_file_opened", "steady_dl_kbps_avg", "steady_dl_kbps_max", "peers_max",
    "done_mb_at_open", "done_mb_at_end",
    "seek25_dl_mb", "seek50_dl_mb", "seek75_dl_mb",
]
# 引擎状态日志行: [1234ms] ... status peers:145 ws:0 dl(KB/s):2060 done(MB):33 state:3
RE_STATUS = re.compile(r"\[(\d+)ms\].*status peers:(\d+) ws:(\d+) dl\(KB/s\):(\d+) done\(MB\):(\d+)")


def ensure_torrent_file(entry: dict) -> str:
    """archive.org 的 .torrent 落地为本地文件(引擎吃本地路径), 带缓存。"""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    local = OUT_DIR / (Path(entry["url"]).name.replace("?", "_"))
    if not local.exists() or local.stat().st_size == 0:
        print(f"下载种子: {entry['url']}")
        req = urllib.request.Request(entry["url"], headers={"User-Agent": "avox-bench"})
        with urllib.request.urlopen(req, timeout=60) as r, open(local, "wb") as f:
            f.write(r.read())
    return str(local)


def parse_engine_log(log_path: Path, marks: dict) -> dict:
    """从引擎日志提取阶段窗口内的量化指标(下载速率/peer/已下载量/元数据来源)。"""
    m = {}
    if not log_path.exists():
        return m
    samples = []  # (ms, peers, dl_kbps, done_mb)
    meta_source = ""
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "status peers:" in line:
            r = RE_STATUS.search(line)
            if r:
                samples.append((int(r.group(1)), int(r.group(2)),
                                int(r.group(3)), int(r.group(5))))
        elif not meta_source:
            # 先见为准: 探测阶段先于起播阶段, 起播期的 disk_cache 不覆盖探测来源
            if "metadata via http cache" in line:
                meta_source = "http(itorrents)"
            elif "metadata cache hit" in line:
                meta_source = "disk_cache"
            elif "metadata received" in line:
                meta_source = "bep9"
    m["meta_source"] = meta_source
    # 阶段细分(引擎"[torrent] stage xxx"日志): 拆解open_ready耗时构成
    stage_ms = {}
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "[torrent] stage " not in line:
            continue
        r = re.match(r"\[(\d+)ms\].*stage (\w+)", line)
        if r:
            stage_ms[r.group(2)] = int(r.group(1))
    t_open = marks.get("open_begin", 0)
    chain = ["metadata_ready", "head_ready", "tail_done", "file_opened"]
    prev = t_open
    for name in chain:
        cur = stage_ms.get(name)
        if cur is not None:
            m[f"ph_{name}"] = max(0, cur - prev)
            prev = cur
    if marks and samples:
        t_open_end = marks.get("open_end")
        if t_open_end is not None:
            before = [s for s in samples if s[0] <= t_open_end]
            if before:
                m["done_mb_at_open"] = before[-1][3]
        m["done_mb_at_end"] = samples[-1][3]
        m["peers_max"] = max(s[1] for s in samples)
        t0, t1 = marks.get("steady_begin"), marks.get("steady_end")
        if t0 is not None and t1 is not None and t1 > t0:
            # done(MB)差分算平均速率(瞬时download_rate采样失真, 不可用)
            win = [s for s in samples if t0 <= s[0] <= t1]
            if len(win) >= 2:
                mb = win[-1][3] - win[0][3]
                sec = (win[-1][0] - win[0][0]) / 1000
                if sec > 0:
                    m["steady_dl_kbps_avg"] = int(mb * 1024 / sec)
            m["steady_dl_kbps_max"] = max((s[2] for s in win), default=0)
        # 每次seek的下载量(分辨"快因网速"还是"快因调度")
        for key, ms in marks.items():
            if not (key.startswith("seek") and key.endswith("_begin")):
                continue
            t1s = marks.get(key[:-6] + "_end")
            if t1s is None:
                continue
            w = [s for s in samples if ms <= s[0] <= t1s]
            if len(w) >= 2:
                m[key[:-6] + "_dl_mb"] = round(w[-1][3] - w[0][3], 1)
    return m


def run_case(tag: str, args, round_idx: int = 0) -> dict:
    if tag not in CORPUS:
        sys.exit(f"未知 tag: {tag} (list 查看语料)")
    entry = dict(CORPUS[tag])
    url = entry["url"]
    if entry.get("remote_torrent"):
        url = ensure_torrent_file(entry)
    if not EXE.exists():
        sys.exit(f"基准程序不存在: {EXE} (先编 torrentbench)")
    stamp = time.strftime("%H%M%S")
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    csv_path = LOG_DIR / f"{tag}_{stamp}_r{round_idx}.csv"
    log_path = csv_path.with_suffix(".csv.log")
    if log_path.exists():
        log_path.unlink()  # 日志追加模式, 每轮独立文件避免跨轮混淆
    cmd = [str(EXE), "-i", url, "--tag", tag, "-o", str(csv_path),
           "--probe-timeout", str(args.probe_timeout),
           "--play-sec", str(args.play_sec), "--seeks", args.seeks]
    if args.fresh:
        cmd.append("--fresh")
    if args.probe_only:
        cmd.append("--probe-only")
    if args.no_probe:
        cmd.append("--no-probe")
    label = f"{tag} r{round_idx}" if round_idx else tag
    print(f"== run {label}: {entry['note']}")
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace",
                          timeout=args.probe_timeout * 3 + 900)
    wall = time.time() - t0
    stages, marks = {}, {}
    for line in proc.stdout.splitlines():
        parts = line.split(" ", 3)
        if len(parts) < 3:
            continue
        if parts[0] == "RESULT" and len(parts) >= 4:
            stages[parts[2]] = {"value": int(parts[3].split(" ")[0]),
                                "note": parts[3].split(" ", 1)[1] if " " in parts[3] else ""}
        elif parts[0] == "MARK":
            marks[parts[1]] = int(parts[2])
    result = {"tag": tag, "note": entry["note"], "url": url,
              "stamp": time.strftime("%Y-%m-%d %H:%M:%S"),
              "wall_sec": round(wall, 1), "returncode": proc.returncode,
              "stages": stages,
              "metrics": parse_engine_log(log_path, marks)}
    out = OUT_DIR / f"{tag}_{stamp}_r{round_idx}.json"
    out.write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    print_table(result)
    print(f"结果已存: {out} (总耗时 {wall:.0f}s)")
    return result


def print_table(r: dict):
    print(f"\n  [{r['tag']}] {r['note']}  @ {r['stamp']}")
    for k in STAGE_ORDER:
        if k in r["stages"]:
            v = r["stages"][k]
            extra = f"  ({v['note']})" if v.get("note") else ""
            flag = " <-- 失败" if k.endswith("_fail") else ""
            print(f"    {k:<18} {v['value']:>8} ms{extra}{flag}")
    for k in METRIC_ORDER:
        if k in r.get("metrics", {}):
            print(f"    . {k:<22} {r['metrics'][k]}")
    print()


def load_runs(tag: str, keep: int):
    # 精确匹配 <tag>_HHMMSS[_rN].json, 防 bbb_multi 混入 bbb 的 glob
    pat = re.compile(rf"^{re.escape(tag)}_\d{{6}}(_r\d+)?\.json$")
    files = [f for f in sorted(OUT_DIR.glob("*.json"))
             if pat.match(f.name) and "summary" not in f.name]
    files = files[-keep:]
    runs = []
    for f in files:
        try:
            runs.append(json.loads(f.read_text(encoding="utf-8")))
        except Exception:
            pass
    return runs


def cmd_aggregate(args):
    """最近K轮逐阶段中位数 -> <tag>_summary.json (可进 compare)。"""
    for tag in args.tags.split(","):
        runs = load_runs(tag, args.keep)
        if not runs:
            print(f"[{tag}] 无历史结果")
            continue
        summary = {"tag": tag, "note": runs[-1]["note"] + f" (最近{len(runs)}轮中位数)",
                   "stamp": time.strftime("%Y-%m-%d %H:%M:%S"),
                   "stages": {}, "metrics": {}}
        for k in STAGE_ORDER:
            vals = [r["stages"][k]["value"] for r in runs if k in r["stages"]]
            if vals:
                summary["stages"][k] = {"value": int(statistics.median(vals)),
                                        "note": f"n={len(vals)}"}
        for k in METRIC_ORDER:
            vals = [r["metrics"][k] for r in runs
                    if k in r.get("metrics", {}) and isinstance(r["metrics"][k], (int, float))]
            if vals:
                summary["metrics"][k] = round(statistics.median(vals), 1)
            else:
                for r in runs:
                    if k in r.get("metrics", {}):
                        summary["metrics"][k] = r["metrics"][k]
                        break
        out = OUT_DIR / f"{tag}_summary.json"
        out.write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding="utf-8")
        print_table(summary)
        print(f"汇总已存: {out}")


def cmd_report(args):
    for tag in (args.tags.split(",") if args.tags else
                sorted({f.name.split("_r")[0].split("_2")[0].split("_1")[0]
                        for f in OUT_DIR.glob("*.json")} if OUT_DIR.exists() else [])):
        runs = load_runs(tag, 1)
        if runs:
            print_table(runs[-1])


def cmd_compare(args):
    old = json.loads(Path(args.old).read_text(encoding="utf-8"))
    new = json.loads(Path(args.new).read_text(encoding="utf-8"))
    print(f"\n  {'阶段':<22}{'旧':>12}{'新':>12}{'差值':>10}  方向")
    for k in STAGE_ORDER:
        if k in old["stages"] and k in new["stages"]:
            a, b = old["stages"][k]["value"], new["stages"][k]["value"]
            d = b - a
            arrow = "改善" if d < 0 else ("变差" if d > 0 else "持平")
            print(f"  {k:<22}{a:>10}ms{b:>10}ms{d:>+10}  {arrow}")
    for k in METRIC_ORDER:
        if k in old.get("metrics", {}) and k in new.get("metrics", {}):
            a, b = old["metrics"][k], new["metrics"][k]
            print(f"  . {k:<20}{str(a):>12}{str(b):>12}")
    print()


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="torrent 播放分步基准")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_run = sub.add_parser("run", help="跑基准")
    p_run.add_argument("--tag", default="bbb", help="语料tag或 all")
    p_run.add_argument("--rounds", type=int, default=1, help="轮数(swarm有波动, 多轮取中位)")
    p_run.add_argument("--fresh", action="store_true", help="清缓存测冷启动")
    p_run.add_argument("--probe-only", action="store_true", help="只测文件列表阶段")
    p_run.add_argument("--no-probe", action="store_true", help="跳过探测直接起播")
    p_run.add_argument("--play-sec", type=int, default=12, help="稳态消费秒数")
    p_run.add_argument("--seeks", default="25,50,75", help="seek百分比列表")
    p_run.add_argument("--probe-timeout", type=int, default=45000)
    p_agg = sub.add_parser("aggregate", help="最近K轮中位数汇总")
    p_agg.add_argument("--tags", default="bbb,bbb_multi")
    p_agg.add_argument("--keep", type=int, default=3)
    p_rep = sub.add_parser("report", help="汇总最近结果")
    p_rep.add_argument("--tags", default=None, help="逗号分隔tag, 默认全部")
    p_cmp = sub.add_parser("compare", help="新旧两轮分步对比")
    p_cmp.add_argument("old")
    p_cmp.add_argument("new")
    args = ap.parse_args()
    if args.cmd == "list":
        for t, e in CORPUS.items():
            print(f"  {t:<10} {e['note']}\n             {e['url'][:90]}")
    elif args.cmd == "run":
        for tag in (CORPUS if args.tag == "all" else [args.tag]):
            for r in range(args.rounds):
                try:
                    run_case(tag, args, r if args.rounds > 1 else 0)
                except subprocess.TimeoutExpired:
                    print(f"[{tag}] 超时跳过")
    elif args.cmd == "aggregate":
        cmd_aggregate(args)
    elif args.cmd == "report":
        cmd_report(args)
    elif args.cmd == "compare":
        cmd_compare(args)


if __name__ == "__main__":
    main()
