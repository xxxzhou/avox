#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描日志里的 [AVOX][TEST] 判定行, 汇总成 markdown 判定矩阵。

用法:
  python script/testenv/collect_verdicts.py log1.txt log2.txt   # 扫描文件
  adb logcat -d | python script/testenv/collect_verdicts.py -   # '-' 读 stdin
  godot --headless --path platform/godot/tools -s res://test_url.gd -- <url> 2>&1 \\
    | python script/testenv/collect_verdicts.py -

输出: 每个 case 的最新结果 + PASS/FAIL 计数; --out matrix.md 落盘。
行格式约定: [AVOX][TEST] case=<id> result=PASS|FAIL [其他字段 k=v ...]
"""
import argparse
import re
import sys
from pathlib import Path

LINE_RE = re.compile(
    r"\[AVOX\]\[TEST\]\s+case=(?P<case>\S+)\s+result=(?P<result>PASS|FAIL)(?P<rest>.*)")


def scan(text: str, source: str, rows: dict, order: list) -> None:
    for line in text.splitlines():
        m = LINE_RE.search(line)
        if not m:
            continue
        case = m.group("case")
        entry = {"case": case, "result": m.group("result"),
                 "detail": m.group("rest").strip(), "source": source}
        if case not in rows:
            order.append(case)
        rows[case] = entry  # 后出现覆盖先出现 (取最新)


def main() -> int:
    ap = argparse.ArgumentParser(description="汇总 [AVOX][TEST] 判定行")
    ap.add_argument("inputs", nargs="+", help="日志文件路径, '-' 表示 stdin")
    ap.add_argument("--out", default="", help="结果 markdown 落盘路径")
    args = ap.parse_args()
    rows, order, n_lines = {}, [], 0
    for src in args.inputs:
        if src == "-":
            scan(sys.stdin.read(), "<stdin>", rows, order)
            continue
        p = Path(src)
        if p.is_dir():
            for f in sorted(p.glob("*.log")):
                scan(f.read_text(encoding="utf-8", errors="replace"), f.name, rows, order)
                n_lines += 1
        else:
            scan(p.read_text(encoding="utf-8", errors="replace"), p.name, rows, order)
    if not rows:
        print("未发现 [AVOX][TEST] 判定行")
        return 1
    n_pass = sum(1 for r in rows.values() if r["result"] == "PASS")
    lines = [
        "## 自动化判定汇总",
        "",
        f"共 {len(rows)} case, PASS {n_pass}, FAIL {len(rows) - n_pass}",
        "",
        "| case | 结果 | 详情 | 来源 |",
        "|------|------|------|------|",
    ]
    for case in order:
        r = rows[case]
        lines.append(f"| {case} | {r['result']} | {r['detail'] or '—'} | {r['source']} |")
    out = "\n".join(lines)
    print(out)
    if args.out:
        Path(args.out).write_text(out + "\n", encoding="utf-8")
        print(f"\n已写入 {args.out}")
    return 0 if n_pass == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
