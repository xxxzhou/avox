#!/usr/bin/env python3
"""
城.py —— avox 插件模型/库统一获取脚本

读取 assets/script/assets_manifest.json（唯一权威清单），提供两层能力:

  检查层 (check):  扫描 plugins/ → 比对 manifest → 输出缺什么
  下载层 (download): 给定 item id 或 URL，纯下载到目标目录

也可无子命令走原有全流程 (--list / --interactive / --all / --select)。

用法:
  # 检查层
  python fetch_assets.py check                     # 人类可读表格
  python fetch_assets.py check --json              # 结构化 JSON (供 avox_cmd 调用)

  # 下载层
  python fetch_assets.py download --select id1,id2 --root <部署根>
  python fetch_assets.py download --url <URL> --dest <目录> --name <文件名>

  # 兼容原有
  python fetch_assets.py --list                    # 列出全部及就绪状态
  python fetch_assets.py --interactive             # 交互式多选
  python fetch_assets.py --all                     # 处理全部 download+script 项
  python fetch_assets.py --select id1,id2          # 处理指定项

选项:
  --manifest PATH     清单路径 (默认 assets/script/assets_manifest.json)
  --root PATH         仓库根目录 (默认按脚本位置推算)
  --force             强制重新下载 (忽略已存在)
  --dry-run           只打印将做什么, 不实际执行
  --hf-mirror         把 huggingface.co 替换为 hf-mirror.com (国内访问)
  --no-color          关闭彩色输出

退出码: 0=全部成功 (或仅查询), 1=有项失败, 2=参数/输入错误
仅依赖 Python 标准库。
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path


# ---------- 颜色 ----------
class C:
    """简易颜色包装; --no-color 时全部置空。"""
    def __init__(self, enabled):
        self.e = enabled

    def __getattr__(self, name):
        return "" if not self.e else _CODES.get(name, "")


_CODES = {
    "R": "\033[31m", "G": "\033[32m", "Y": "\033[33m",
    "B": "\033[34m", "M": "\033[35m", "C": "\033[36m",
    "BOLD": "\033[1m", "DIM": "\033[2m",
    "W": "\033[0m",
}


# ---------- 输出辅助 ----------
def _log(msg, c=None, file=None):
    """写 stderr (进度/状态), 不污染 stdout。"""
    f = file or sys.stderr
    if c:
        print(msg, file=f)
    else:
        print(msg, file=f)


def _out(msg):
    """写 stdout (最终结果/JSON)。"""
    print(msg)


# ---------- 路径 / 清单 ----------
def find_project_root():
    """assets/script/fetch_assets.py 的上三级 = 项目根 (源码仓库根 或 部署根)。"""
    return Path(__file__).resolve().parent.parent.parent


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def dest_path(item, root):
    """item.dest 相对仓库根的绝对路径。"""
    return root / item["dest"]


def verify_ok(item, root):
    """有 verify_files 则判断就绪/缺失; 无 verify_files 返回 None (不判断)。"""
    vf = item.get("verify_files")
    if not vf:
        return None
    dest = dest_path(item, root)
    return all((dest / v).exists() for v in vf)


def check_verify_files(item, root):
    """返回 (ready: bool|None, missing: list[str])。"""
    vf = item.get("verify_files", [])
    if not vf:
        return None, []
    dest = dest_path(item, root)
    missing = [v for v in vf if not (dest / v).exists()]
    return (len(missing) == 0), missing


# ---------- 状态文案 ----------
def status_text(item, root, c):
    ok = verify_ok(item, root)
    if ok is None:
        return f"{c.DIM}—{c.W}"
    return f"{c.G}就绪{c.W}" if ok else f"{c.Y}缺失{c.W}"


def method_tag(method, c):
    return {
        "download": f"{c.C}download{c.W}",
        "script":   f"{c.M}script  {c.W}",
        "manual":   f"{c.Y}manual  {c.W}",
        "build":    f"{c.DIM}build   {c.W}",
    }.get(method, method)


# ---------- 插件扫描 ----------
def scan_plugins(root):
    """扫描 <root>/plugins/ 下的 avox_* 动态库, 返回插件名集合 (如 {"avox_cv", "avox_ocr"})。"""
    plugins_dir = root / "plugins"
    result = set()
    if not plugins_dir.is_dir():
        return result
    # Windows: avox_*.dll
    for f in plugins_dir.glob("avox_*.dll"):
        result.add(f.stem)
    # Linux: libavox_*.so
    for f in plugins_dir.glob("libavox_*.so"):
        result.add(f.name[3:-3])
    return result


# ---------- 下载 ----------
def _apply_mirror(url, hf_mirror):
    if hf_mirror and "huggingface.co" in url:
        return url.replace("https://huggingface.co", "https://hf-mirror.com")
    return url


def _write_progress(path, obj):
    """把进度事件以 JSONL 追加写入 path (供外部程序轮询, 如 Godot AssetManager); 失败静默。"""
    if not path:
        return
    try:
        with open(path, "a", encoding="utf-8") as pf:
            pf.write(json.dumps(obj, ensure_ascii=False) + "\n")
            pf.flush()
    except OSError:
        pass


def download_file(url, dest_file, c, retries=3, progress_file=None):
    """带进度下载单个文件到 dest_file (Path)。进度写 stderr; 若给 progress_file 则同时
    追加 JSONL 事件 (start/prog/file_done) 供外部程序轮询。失败返回 False。"""
    dest_file.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest_file.with_suffix(dest_file.suffix + ".part")
    label = dest_file.name

    def emit(ev, **kw):
        _write_progress(progress_file, {"ev": ev, "name": label, **kw})

    last_t = 0.0

    def reporthook(block_num, block_size, total_size):
        nonlocal last_t
        downloaded = block_num * block_size
        if total_size > 0:
            percent = min(100, downloaded * 100 / total_size)
            bar_len = 30
            filled = int(bar_len * downloaded / total_size)
            bar = "=" * filled + "-" * (bar_len - filled)
            sys.stderr.write(
                f"\r  {c.DIM}[{bar}]{c.W} {percent:5.1f}% "
                f"({downloaded / 1048576:.1f}/{total_size / 1048576:.1f}MB)"
            )
            sys.stderr.flush()
            now = time.monotonic()
            if progress_file and now - last_t >= 0.25:
                last_t = now
                emit("prog", pct=round(percent, 1), got=downloaded, total=total_size)
        else:
            sys.stderr.write(f"\r  {downloaded / 1048576:.1f}MB")
            sys.stderr.flush()
            now = time.monotonic()
            if progress_file and now - last_t >= 0.25:
                last_t = now
                emit("prog", pct=0.0, got=downloaded, total=0)

    req = urllib.request.Request(url, headers={"User-Agent": "fetch_assets/1.0"})
    for attempt in range(1, retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=60) as resp, open(tmp, "wb") as out:
                total = int(resp.getheader("Content-Length") or 0)
                emit("start", total=total)
                while True:
                    chunk = resp.read(1024 * 64)
                    if not chunk:
                        break
                    out.write(chunk)
                    block_num = out.tell() // (1024 * 64)
                    reporthook(block_num, 1024 * 64, total)
            sys.stderr.write("\n")
            os.replace(tmp, dest_file)
            emit("file_done", ok=True)
            return True
        except Exception as e:
            sys.stderr.write("\n")
            print(f"  {c.R}下载失败 (第 {attempt}/{retries} 次): {e}{c.W}", file=sys.stderr)
            if attempt < retries:
                time.sleep(2 * attempt)
    if tmp.exists():
        tmp.unlink()
    emit("file_done", ok=False)
    return False


def check_sha256(path, expected, c):
    if not expected:
        return True
    print(f"  校验 sha256...", file=sys.stderr)
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    actual = h.hexdigest()
    if actual.lower() == expected.lower():
        return True
    print(f"  {c.R}sha256 不匹配! 期望 {expected}, 实际 {actual}{c.W}", file=sys.stderr)
    return False


def extract_archive(archive_file, dest, fmt, flatten, c, strip=0):
    """解压到 dest; flatten=True 时去掉目录层级只留文件。
    strip>0 时先剥离 N 层单一顶层目录 (类似 tar --strip-components),
    保留内部结构——用于包内带顶层目录 (如 kokoro-multi-lang-v1_0/) 时避免多一层。"""
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="fetch_extract_", dir=str(dest.parent)))
    try:
        if fmt == "zip":
            with zipfile.ZipFile(archive_file, "r") as z:
                z.extractall(tmp)
        elif fmt in ("tarbz2", "targz"):
            mode = "r:bz2" if fmt == "tarbz2" else "r:gz"
            with tarfile.open(archive_file, mode) as t:
                t.extractall(tmp)
        else:
            print(f"  {c.R}未知压缩格式: {fmt}{c.W}", file=sys.stderr)
            return False
        src_root = tmp
        for _ in range(strip):
            entries = [p for p in src_root.iterdir() if not p.name.startswith(".")]
            if len(entries) != 1 or not entries[0].is_dir():
                print(f"  {c.Y}strip={strip}: 期望单一顶层目录, 实际 {len(entries)} 项, 放弃剥离{c.W}",
                      file=sys.stderr)
                break
            src_root = entries[0]
        dest.mkdir(parents=True, exist_ok=True)
        if flatten:
            for root, _dirs, files in os.walk(src_root):
                for fn in files:
                    shutil.move(os.path.join(root, fn), dest / fn)
        else:
            for entry in os.listdir(src_root):
                shutil.move(str(src_root / entry), str(dest / entry))
        return True
    finally:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)


def filename_from_url(url):
    return url.rstrip("/").split("/")[-1] or "download.bin"


# ---------- 各 method 处理 ----------
def process_download(item, root, force, hf_mirror, dry, c, progress_file=None):
    """返回 (ok, msg)。进度/状态写 stderr; progress_file 透传给 download_file。"""
    if verify_ok(item, root) and not force:
        return True, "已就绪, 跳过"
    if dry:
        urls = [f["url"] for f in item.get("files", [])]
        return True, "将下载: " + ", ".join(urls)
    dest = dest_path(item, root)
    dest.mkdir(parents=True, exist_ok=True)
    archive = item.get("archive")
    for f in item.get("files", []):
        url = _apply_mirror(f["url"], hf_mirror)
        if archive:
            archive_name = filename_from_url(url)
            archive_file = dest / archive_name
            print(f"  下载压缩包 {archive_name}", file=sys.stderr)
            if not download_file(url, archive_file, c, progress_file=progress_file):
                return False, f"下载失败: {archive_name}"
            if not check_sha256(archive_file, f.get("sha256"), c):
                return False, f"sha256 校验失败: {archive_name}"
            print(f"  解压 ({archive['format']}, flatten={archive.get('flatten', False)})", file=sys.stderr)
            if not extract_archive(archive_file, dest, archive["format"],
                                   archive.get("flatten", False), c,
                                   archive.get("strip", 0)):
                return False, f"解压失败: {archive_name}"
            archive_file.unlink(missing_ok=True)
        else:
            name = f.get("name") or filename_from_url(url)
            target = dest / name
            print(f"  下载 {name}", file=sys.stderr)
            if not download_file(url, target, c, progress_file=progress_file):
                return False, f"下载失败: {name}"
            if not check_sha256(target, f.get("sha256"), c):
                return False, f"sha256 校验失败: {name}"
    final = verify_ok(item, root)
    if final is None:
        return True, "完成 (无 verify_files)"
    return final, ("完成" if final else "完成但 verify_files 未全")


def process_script(item, dry, c):
    cmd = item["command"]
    if dry:
        return True, f"将执行: {cmd}"
    print(f"  执行: {cmd}", file=sys.stderr)
    try:
        ret = subprocess.run(cmd, shell=True)
        return ret.returncode == 0, ("脚本返回 0" if ret.returncode == 0 else f"脚本返回 {ret.returncode}")
    except Exception as e:
        return False, f"执行异常: {e}"


def process_manual(item, c):
    """打印来源与目标, 不下载。输出到 stderr。"""
    src = item.get("source_ref") or item.get("source") or "(未提供来源)"
    print(f"  来源: {src}", file=sys.stderr)
    expected = item.get("files_expected")
    if expected:
        print(f"  需要文件: {', '.join(expected)}", file=sys.stderr)
    print(f"  目标目录: {item['dest']}", file=sys.stderr)
    note = item.get("note")
    if note:
        print(f"  说明: {note}", file=sys.stderr)
    return True, "已打印获取指引 (需人工获取)"


def process_build(item, c):
    print(f"  非下载件, 由构建产出: {item.get('build_command', '(build 脚本)')}", file=sys.stderr)
    print(f"  来源: {item.get('source_ref', '')}", file=sys.stderr)
    if item.get("note"):
        print(f"  说明: {item['note']}", file=sys.stderr)
    return True, "编译产物, 随 build 流程产出"


def process_item(item, root, platform, force, hf_mirror, dry, c, progress_file=None):
    """按 method 分派, 返回 dict(id, action, ok, msg)。先按平台 resolve。"""
    eff = resolve_platform(item, platform)
    if eff is None:
        print(f"\n{c.BOLD}[{item['id']}] {item['name']}{c.W}  "
              f"({method_tag(item['method'], c)} · {item['plugin']})", file=sys.stderr)
        print(f"  -> {c.Y}SKIP 本平台({platform})不适用{c.W}  "
              f"(适用: {', '.join(applicable_platforms(item))})", file=sys.stderr)
        return {"id": item["id"], "action": "skip", "ok": True,
                "msg": f"本平台 {platform} 不适用"}
    method = eff["method"]
    print(f"\n{c.BOLD}[{eff['id']}] {eff['name']}{c.W}  "
          f"({method_tag(method, c)} · {eff['plugin']} · {platform})", file=sys.stderr)
    if method == "download":
        ok, msg = process_download(eff, root, force, hf_mirror, dry, c, progress_file)
    elif method == "script":
        ok, msg = process_script(eff, dry, c)
    elif method == "manual":
        ok, msg = process_manual(eff, c)
    elif method == "build":
        ok, msg = process_build(eff, c)
    else:
        ok, msg = False, f"未知 method: {method}"
    tag = f"{c.G}OK{c.W}" if ok else f"{c.R}FAIL{c.W}"
    print(f"  -> {tag} {msg}", file=sys.stderr)
    return {"id": eff["id"], "action": method, "ok": ok, "msg": msg}


# ---------- 平台 ----------
def detect_platform():
    """从 sys.platform 推断目标平台。"""
    if sys.platform == "win32" or os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if "linux" in sys.platform:
        return "linux"
    return sys.platform


def resolve_platform(item, platform):
    """按平台解析 item, 返回 effective item dict。
    item 平台无关(无 platforms 字段) -> 返回原 item (用顶层 files);
    item 平台相关但该平台不适用 -> 返回 None;
    否则 -> 继承顶层元数据, 用 platforms[platform] 的 files/verify_files/archive/note 覆盖。"""
    plats = item.get("platforms")
    if not plats:
        return item
    spec = plats.get(platform)
    if not spec:
        return None
    merged = {k: v for k, v in item.items() if k != "platforms"}
    merged.update(spec)
    return merged


def applicable_platforms(item):
    """item 适用的平台列表; 平台无关返回 ['all']。"""
    plats = item.get("platforms")
    return ["all"] if not plats else list(plats.keys())


# ---------- 列表 / 交互 ----------
def filter_items(items, plugin=None, type_=None, method=None):
    out = items
    if plugin:
        out = [it for it in out if it["plugin"] == plugin]
    if type_:
        out = [it for it in out if it["type"] == type_]
    if method:
        out = [it for it in out if it["method"] == method]
    return out


def print_list(items, root, platform, c):
    print(f"{c.BOLD}当前平台: {platform}{c.W}", file=sys.stderr)
    print(f"{c.BOLD}#   类型    插件           id                      方式      平台        状态   名称{c.W}", file=sys.stderr)
    print("-" * 112, file=sys.stderr)
    for i, it in enumerate(items, 1):
        plats = applicable_platforms(it)
        if "all" in plats:
            plat_col, stat = f"{c.DIM}全平台{c.W}", status_text(it, root, c)
        elif platform in plats:
            plat_col, stat = platform, status_text(resolve_platform(it, platform), root, c)
        else:
            plat_col, stat = f"{c.DIM}{','.join(plats)}{c.W}", f"{c.DIM}不适用{c.W}"
        print(f"{i:>2}  {it['type']:<7} {it['plugin']:<14} {it['id']:<22} "
              f"{it['method']:<9} {plat_col}  {stat}  {it['name']}", file=sys.stderr)
    print(f"\n共 {len(items)} 项。", file=sys.stderr)


def interactive_select(items, root, platform, c):
    """多选: 输入编号切换, a=全选, c=全不选, 回车=执行, q=退出。返回选中 items 列表。"""
    if not sys.stdin.isatty():
        print(f"{c.R}非交互终端, 无法进入交互模式; 请用 --select / --all 指定。{c.W}", file=sys.stderr)
        return None
    selected = set()
    while True:
        print(file=sys.stderr)
        for i, it in enumerate(items, 1):
            mark = f"{c.G}[x]{c.W}" if (i - 1) in selected else f"{c.DIM}[ ]{c.W}"
            plats = applicable_platforms(it)
            if "all" in plats:
                stat, flag = status_text(it, root, c), ""
            elif platform in plats:
                stat, flag = status_text(resolve_platform(it, platform), root, c), ""
            else:
                stat, flag = f"{c.DIM}不适用{c.W}", f"{c.DIM}(仅 {','.join(plats)}){c.W}"
            print(f"  {mark} {i:>2}. [{it['type']}] {it['plugin']:<13} "
                  f"{stat} {it['name']} {flag}", file=sys.stderr)
        print(f"\n  {c.DIM}输入编号切换 (如 1 3 5), a=全选, c=全不选, "
              f"回车=执行所选, q=退出{c.W}", file=sys.stderr)
        try:
            raw = input("选> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return []
        if raw in ("q", "quit", "exit"):
            return []
        if raw == "":
            if not selected:
                print(f"  {c.Y}未选中任何项。{c.W}", file=sys.stderr)
                continue
            return [items[i] for i in sorted(selected)]
        if raw == "a":
            selected = set(range(len(items)))
            continue
        if raw == "c":
            selected.clear()
            continue
        for tok in raw.replace(",", " ").split():
            if tok.isdigit():
                idx = int(tok) - 1
                if 0 <= idx < len(items):
                    selected.symmetric_difference_update([idx])
                else:
                    print(f"  {c.R}忽略越界编号: {tok}{c.W}", file=sys.stderr)


# ============================================================
# 子命令: check
# ============================================================
def cmd_check(args):
    """扫描 plugins/ + 比对 manifest → 输出缺失项。"""
    c = C(not args.no_color and sys.stderr.isatty())
    platform = args.platform or detect_platform()
    default_root = find_project_root()
    root = Path(args.root).resolve() if args.root else default_root
    manifest_path = Path(args.manifest) if args.manifest else (Path(__file__).resolve().parent / "assets_manifest.json")
    if not manifest_path.exists():
        print(f"{c.R}找不到清单: {manifest_path}{c.W}", file=sys.stderr)
        return 2
    manifest = load_manifest(manifest_path)
    items = manifest["items"]
    if args.plugin:
        items = [it for it in items if it["plugin"] == args.plugin]
    # 扫描已安装插件
    installed = scan_plugins(root)
    # 比对每项
    items_status = []
    for it in items:
        eff = resolve_platform(it, platform)
        if eff is None:
            continue
        ready, missing = check_verify_files(eff, root)
        items_status.append({
            "id": eff["id"], "name": eff["name"], "plugin": eff["plugin"],
            "type": eff["type"], "method": eff["method"], "dest": eff["dest"],
            "ready": ready, "missing_files": missing,
            "files": eff.get("files", []),
        })
    if args.as_json:
        _out(json.dumps({
            "platform": platform,
            "installed_plugins": sorted(installed),
            "items": items_status,
        }, ensure_ascii=False, indent=2))
    else:
        # 人类可读表格
        print(f"{c.BOLD}当前平台: {platform}  |  已安装插件: {', '.join(sorted(installed)) or '(无)'}{c.W}", file=sys.stderr)
        print(f"{c.BOLD}#   类型    插件           id                      方式      状态   缺失文件              名称{c.W}", file=sys.stderr)
        print("-" * 120, file=sys.stderr)
        for i, it in enumerate(items_status, 1):
            if it["ready"] is None:
                stat = f"{c.DIM}—{c.W}"
            elif it["ready"]:
                stat = f"{c.G}就绪{c.W}"
            else:
                stat = f"{c.Y}缺失{c.W}"
            miss_str = ", ".join(it["missing_files"]) if it["missing_files"] else ""
            print(f"{i:>2}  {it['type']:<7} {it['plugin']:<14} {it['id']:<22} "
                  f"{it['method']:<9} {stat}  {miss_str:<20} {it['name']}", file=sys.stderr)
        print(f"\n共 {len(items_status)} 项。", file=sys.stderr)
    return 0


# ============================================================
# 子命令: download
# ============================================================
def cmd_download(args):
    """纯下载: 按 manifest item id 或直接 URL。"""
    c = C(not args.no_color and sys.stderr.isatty())
    platform = args.platform or detect_platform()
    default_root = find_project_root()
    root = Path(args.root).resolve() if args.root else default_root
    manifest_path = Path(args.manifest) if args.manifest else (Path(__file__).resolve().parent / "assets_manifest.json")

    # 直接 URL 模式
    if args.url:
        if not args.dest:
            print(f"{c.R}--url 模式需要 --dest 指定目标目录{c.W}", file=sys.stderr)
            return 2
        dest = Path(args.dest)
        name = args.name or filename_from_url(args.url)
        print(f"下载 {args.url} → {dest / name}", file=sys.stderr)
        if args.dry_run:
            print(f"  (dry-run, 不实际下载)", file=sys.stderr)
            return 0
        ok = download_file(_apply_mirror(args.url, args.hf_mirror), dest / name, c,
                           progress_file=args.progress_file)
        if ok:
            print(f"  {c.G}OK{c.W} 下载完成", file=sys.stderr)
        else:
            print(f"  {c.R}FAIL{c.W} 下载失败", file=sys.stderr)
        return 0 if ok else 1

    # 按 id 下载 (从 manifest 取 URL/dest)
    if not args.select:
        print(f"{c.Y}未指定下载项。用 --select id1,id2 或 --url <URL>。{c.W}", file=sys.stderr)
        return 2
    if not manifest_path.exists():
        print(f"{c.R}找不到清单: {manifest_path}{c.W}", file=sys.stderr)
        return 2
    manifest = load_manifest(manifest_path)
    items = manifest["items"]
    wanted = {s.strip() for s in args.select.split(",") if s.strip()}
    chosen = [it for it in items if it["id"] in wanted]
    missing_ids = wanted - {it["id"] for it in chosen}
    if missing_ids:
        print(f"{c.R}未知 id: {', '.join(missing_ids)}{c.W}", file=sys.stderr)
        return 2
    if not chosen:
        print(f"{c.Y}没有需要下载的项。{c.W}", file=sys.stderr)
        return 0
    print(f"{c.BOLD}仓库根: {root}  |  平台: {platform}{c.W}", file=sys.stderr)
    if args.hf_mirror:
        print(f"{c.C}已启用 HF 镜像 (hf-mirror.com){c.W}", file=sys.stderr)
    results = []
    for it in chosen:
        results.append(process_item(it, root, platform, args.force, args.hf_mirror,
                                    args.dry_run, c, args.progress_file))
    ok_cnt = sum(1 for r in results if r["ok"])
    print(f"\n{c.BOLD}汇总: {ok_cnt}/{len(results)} 成功{c.W}", file=sys.stderr)
    if args.as_json:
        _out(json.dumps({"results": results}, ensure_ascii=False, indent=2))
    return 0 if ok_cnt == len(results) else 1


# ============================================================
# 兼容: 无子命令时走原有逻辑
# ============================================================
def cmd_legacy(args):
    """原有全流程 (--list / --interactive / --all / --select)。"""
    c = C(not args.no_color and sys.stderr.isatty())
    platform = args.platform or detect_platform()
    default_root = find_project_root()
    root = Path(args.root).resolve() if args.root else default_root
    manifest_path = Path(args.manifest) if args.manifest else (Path(__file__).resolve().parent / "assets_manifest.json")
    if not manifest_path.exists():
        print(f"{c.R}找不到清单: {manifest_path}{c.W}", file=sys.stderr)
        return 2
    manifest = load_manifest(manifest_path)
    items = manifest["items"]
    items = filter_items(items, args.plugin, args.type_, args.method)
    # 仅列表
    if args.list and not (args.all or args.select or args.interactive):
        if args.as_json:
            out = []
            for it in items:
                eff = resolve_platform(it, platform)
                out.append({
                    "id": it["id"], "name": it["name"], "plugin": it["plugin"],
                    "type": it["type"], "method": it["method"], "dest": it["dest"],
                    "platforms": applicable_platforms(it),
                    "applicable": eff is not None,
                    "ready": verify_ok(eff, root) if eff else None,
                })
            _out(json.dumps({"platform": platform, "items": out}, ensure_ascii=False, indent=2))
        else:
            print_list(items, root, platform, c)
        return 0
    # 确定要处理的项
    chosen = []
    if args.interactive:
        chosen = interactive_select(items, root, platform, c)
        if chosen is None:
            return 2
    elif args.select:
        wanted = {s.strip() for s in args.select.split(",") if s.strip()}
        chosen = [it for it in items if it["id"] in wanted]
        missing = wanted - {it["id"] for it in chosen}
        if missing:
            print(f"{c.R}未知 id: {', '.join(missing)}{c.W}", file=sys.stderr)
            return 2
    elif args.all:
        chosen = [it for it in items if it["method"] in ("download", "script")]
    else:
        print(f"{c.Y}未指定动作。用 --list 查看, --interactive 多选, "
              f"--all 或 --select id 处理。{c.W}", file=sys.stderr)
        return 2
    if not chosen:
        print(f"{c.Y}没有需要处理的项。{c.W}", file=sys.stderr)
        return 0
    print(f"{c.BOLD}仓库根: {root}  |  平台: {platform}{c.W}", file=sys.stderr)
    print(f"清单: {manifest_path}", file=sys.stderr)
    if args.hf_mirror:
        print(f"{c.C}已启用 HF 镜像 (hf-mirror.com){c.W}", file=sys.stderr)
    results = []
    for it in chosen:
        results.append(process_item(it, root, platform, args.force, args.hf_mirror,
                                    args.dry_run, c, args.progress_file))
    ok_cnt = sum(1 for r in results if r["ok"])
    print(f"\n{c.BOLD}汇总: {ok_cnt}/{len(results)} 成功{c.W}", file=sys.stderr)
    if args.as_json:
        _out(json.dumps({"results": results}, ensure_ascii=False, indent=2))
    return 0 if ok_cnt == len(results) else 1


# ---------- main ----------
def build_argparser():
    p = argparse.ArgumentParser(
        description="avox 插件模型/库统一获取脚本 (JSON 驱动)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="subcmd")

    # ---- check 子命令 ----
    ck = sub.add_parser("check", help="扫描插件 + 比对清单, 输出缺失项")
    ck.add_argument("--json", dest="as_json", action="store_true", help="JSON 输出 (供程序调用)")
    ck.add_argument("--manifest", default=None, help="清单路径")
    ck.add_argument("--root", default=None, help="仓库根目录")
    ck.add_argument("--platform", default=None, help="目标平台")
    ck.add_argument("--plugin", default=None, help="按插件过滤")
    ck.add_argument("--no-color", action="store_true", help="关闭彩色输出")

    # ---- download 子命令 ----
    dl = sub.add_parser("download", help="下载指定资源")
    dl.add_argument("--select", default=None, help="逗号分隔的 manifest item id")
    dl.add_argument("--url", default=None, help="直接下载 URL (--url 模式)")
    dl.add_argument("--dest", default=None, help="目标目录 (--url 模式)")
    dl.add_argument("--name", default=None, help="文件名 (--url 模式)")
    dl.add_argument("--manifest", default=None, help="清单路径")
    dl.add_argument("--root", default=None, help="仓库根目录")
    dl.add_argument("--platform", default=None, help="目标平台")
    dl.add_argument("--force", action="store_true", help="强制重新下载")
    dl.add_argument("--hf-mirror", action="store_true", help="huggingface.co -> hf-mirror.com")
    dl.add_argument("--dry-run", action="store_true", help="只打印, 不执行")
    dl.add_argument("--json", dest="as_json", action="store_true", help="JSON 输出")
    dl.add_argument("--progress-file", default=None,
                    help="进度事件写入该文件 (JSONL, 供程序轮询)")
    dl.add_argument("--no-color", action="store_true", help="关闭彩色输出")

    # ---- 兼容: 无子命令时的原有参数 ----
    p.add_argument("--manifest", default=None, help="清单路径 (默认 assets/script/assets_manifest.json)")
    p.add_argument("--root", default=None, help="仓库根目录 (默认按脚本位置推算)")
    p.add_argument("--list", "-l", action="store_true", help="列出所有项及就绪状态")
    p.add_argument("--interactive", "-i", action="store_true", help="交互式多选")
    p.add_argument("--all", action="store_true", help="处理全部 download+script 项")
    p.add_argument("--select", default=None, help="逗号分隔的 id 列表")
    p.add_argument("--plugin", default=None, help="按插件过滤")
    p.add_argument("--type", dest="type_", default=None, choices=["model", "library"], help="按类型过滤")
    p.add_argument("--method", default=None, choices=["download", "script", "manual", "build"], help="按方式过滤")
    p.add_argument("--platform", default=None, help="目标平台 (默认自动检测: windows/linux/macos)")
    p.add_argument("--force", action="store_true", help="强制重新下载")
    p.add_argument("--dry-run", action="store_true", help="只打印, 不执行")
    p.add_argument("--hf-mirror", action="store_true", help="huggingface.co -> hf-mirror.com")
    p.add_argument("--no-color", action="store_true", help="关闭彩色输出")
    p.add_argument("--json", dest="as_json", action="store_true", help="结构化 JSON 输出 (供程序调用)")
    p.add_argument("--progress-file", default=None, help="进度事件写入该文件 (JSONL, 供程序轮询)")
    return p


def main(argv=None):
    args = build_argparser().parse_args(argv)
    # 有子命令: check / download
    if args.subcmd == "check":
        return cmd_check(args)
    if args.subcmd == "download":
        return cmd_download(args)
    # 无子命令: 兼容原有行为
    return cmd_legacy(args)


if __name__ == "__main__":
    sys.exit(main())
