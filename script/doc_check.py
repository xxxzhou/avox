#!/usr/bin/env python3
"""doc_check.py — markdown 文档治理校验 (pre-push 门禁的一部分).

模式:
 - 默认 (全量): 存量文档只报 warning, 退出码恒 0. 用于体检, 不阻塞.
 - --strict <f1> <f2> ... <root>: 对本次改动的文件强制检查——缺状态头、
   失效相对链接、多版本文件名都算 error, 阻塞提交 (pre-push 用).

状态头约定 (文档前几行内的注释行):
    > 状态: 有效 · 上次核对: YYYY-MM-DD · 权威源: <相对路径或 - >
 状态取值: 有效 | 已落地 | 施工记录 | 进行中 | 已废弃 | 归档

用法:
    python script/doc_check.py                                   # 全量体检
    python script/doc_check.py --strict a.md b.md repo_root      # 门禁
"""
import os
import re
import sys

STATES = {"有效", "已落地", "施工记录", "进行中", "已废弃", "归档"}
BAD_FILENAME = re.compile(r"[vV]2|[vV]3|old|new|副本|备份|copy|_v\d", re.IGNORECASE)
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
WS = re.compile(r"\s")
# 排除仓库根级目录; walk 恒从 doc_root 开始, 不会触到 /build, 故不放 "build"
# (它对应 doc/build/, 需纳入检查)
SKIP_DIRS = {"3rdparty", "node_modules", ".git"}


def list_md(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for f in filenames:
            if f.endswith(".md"):
                yield os.path.join(dirpath, f)


def has_valid_state_head(lines):
    for l in lines[:5]:
        l = l.strip()
        if not l.startswith("> 状态:"):
            continue
        val = l[len("> 状态:"):].split("·")[0].strip().strip("<>").strip("` ")
        return val in STATES, val
    return False, None


def _has(path):
    try:
        with open(path, encoding="utf-8") as f:
            return has_valid_state_head(f.readlines())[0]
    except (OSError, UnicodeDecodeError):
        return True


def _insert_head(path, head):
    # newline='' 保留原换行(CRLF/LF), 仅插入状态头, 不改全文行尾, 避免 git 全量重写
    try:
        with open(path, encoding="utf-8", newline="") as f:
            lines = f.readlines()
    except (OSError, UnicodeDecodeError):
        return
    crlf = any("\r\n" in l for l in lines[:3])
    nl = "\r\n" if crlf else "\n"
    head = head.replace("\n", nl)
    out, inserted = [], False
    for line in lines:
        out.append(line)
        if not inserted and line.startswith("# "):
            out.append(nl)
            out.append(head)
            out.append(nl)
            inserted = True
    if not inserted:
        out.insert(0, head + nl)
        out.insert(1, nl)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.writelines(out)


def bad_links(path, lines):
    try:
        text = "".join(lines)
    except UnicodeDecodeError:
        return []
    base = os.path.dirname(path)
    # 同步声明豁免: aocec 同步稿顶部声明过「残留相对路径指向 aocec 仓库」,
    # 其跨仓引用是故意保留, 不算坏链
    from_aoce = "aocec" in "".join(lines[:6])
    bad = []
    for m in LINK_RE.finditer(text):
        raw = m.group(1)
        target = raw.strip()
        if not target or target.startswith(("http://", "https://", "mailto:")):
            continue
        # C++ 签名误报: 如 `[&frame](const VideoFramePtr &frame)` 含空格, 非链接
        if " " in raw:
            continue
        if from_aoce and target.startswith(
            ("../../code/", "../../UE4Test/", "../../glsl/", "thirdparty/")
        ):
            continue
        target = target.split("#", 1)[0]
        target_ws = WS.sub("", target)
        if not target_ws:
            continue
        full = os.path.normpath(os.path.join(base, target_ws))
        # 资产类: assets/ 由 fetch_assets 运行期拉取, 未拉取时缺失属正常
        if "assets" in full.split(os.sep):
            continue
        if not os.path.exists(full):
            bad.append(raw)
    return bad


def report_one(path, strict, reports):
    if not path.lower().endswith(".md"):
        return
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.readlines()
    except (OSError, UnicodeDecodeError):
        reports["err"].append(f"  [err ] 无法读取(非 utf-8): {path}")
        return
    ok, val = has_valid_state_head(lines)
    if not ok:
        item = f"  [{'err ' if strict else 'warn'}] 缺/非法的状态头 ({path})"
        reports["err" if strict else "warn"].append(item)
    for t in bad_links(path, lines):
        reports["err" if strict else "warn"].append(
            f"  [{'err ' if strict else 'warn'}] 链接目标不存在: {path} -> {t!r}")
    name = os.path.basename(path)
    if strict and BAD_FILENAME.search(name):
        reports["err"].append(f"  [err ] 疑似多版本/重复命名 '{name}': {path}")


def main():
    args = sys.argv[1:]
    if args and args[0] == "--list":
        import json
        root = os.path.abspath(args[1]) if len(args) > 1 else os.getcwd()
        doc_root = os.path.join(root, "doc") if os.path.isdir(os.path.join(root, "doc")) else root
        data = {"nohdr": [], "badlinks": {}}
        for p in sorted(list_md(doc_root)):
            try:
                with open(p, encoding="utf-8") as f:
                    lines = f.readlines()
            except (OSError, UnicodeDecodeError):
                continue
            rel = os.path.relpath(p, root).replace("\\", "/")
            ok, _val = has_valid_state_head(lines)
            if not ok:
                data["nohdr"].append(rel)
            bl = bad_links(p, lines)
            if bl:
                data["badlinks"][rel] = bl
        print(json.dumps(data, ensure_ascii=False, indent=1))
        return 0

    if args and args[0] == "--backfill":
        status, target = args[1], args[2]
        if status not in STATES:
            print(f"[err ] 非法的状态值: {status}", file=sys.stderr)
            return 1
        head = f"> 状态: {status} · 上次核对: 2026-09-16 · 权威源: -\n"
        targets = []
        if os.path.isdir(target):
            targets = [p for p in list_md(target) if not _has(p)]
        elif os.path.isfile(target):
            targets = [target] if not _has(target) else []
        else:
            print(f"[err ] 路径不存在: {target}", file=sys.stderr)
            return 1
        for p in targets:
            _insert_head(p, head)
            print(f"  [ok  ] +{status}: {p}")
        print(f"backfill 完成: {len(targets)} 篇")
        return 0

    strict = []
    if args and args[0] == "--strict":
        strict = args[1:-1]
        root = args[-1] or os.getcwd()
    else:
        root = args[0] if args else os.getcwd()
    root = os.path.abspath(root)
    doc_root = os.path.join(root, "doc") if os.path.isdir(os.path.join(root, "doc")) else root

    reports = {"err": [], "warn": []}
    if strict:
        for p in strict:
            p = os.path.abspath(p)
            if os.path.exists(p):
                report_one(p, True, reports)
    else:
        for p in sorted(list_md(doc_root)):
            report_one(p, False, reports)

    print("--- doc_check report ---")
    for w in reports["warn"]:
        print(w)
    for e in reports["err"]:
        print(e)
    print(f"warnings: {len(reports['warn'])}, errors: {len(reports['err'])}")
    return 1 if reports["err"] else 0


if __name__ == "__main__":
    sys.exit(main())