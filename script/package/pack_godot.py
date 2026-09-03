#!/usr/bin/env python3
"""打包 avox Godot 工具箱到目标目录 (干净、自包含、可分发)。

源 = avox 构建安装树 (默认 build/.../Release, 只读不动);
产物 = 另一个 --out 目录, 只含运行所需:
  - avox 运行时 (核心 dll + ffmpeg + plugins 壳 + assets glsl/fonts/script)
  - 终端工具 avox_cli.exe (命令行控制台) / avox_agent.exe (AI 智能体)
  - Godot 工程导出发布版 avox_tools.exe (~55M, 无编辑器, 脚本编进 .pck)
模型 / 第三方 AI 库 (onnx/opencv/openvino/sherpa) 默认不打包,
用户用产物里的 assets/script/fetch_assets.py 按需下载 (沿用 manifest)。

用法:
  python pack_godot.py                                   # 默认导出发布版 -> deploy/godot
  python pack_godot.py --out D:\\avox_godot_pack
  python pack_godot.py --no-export                       # 逃生舱: 编辑器+源码 (不可移植, 本地测)
  python pack_godot.py --with-thirdparty                 # 连 AI 库一起打包 (默认按需下载)
  python pack_godot.py --godot-template <release模板exe> # 自动装模板再导出

前置: 导出发布版需 Godot <ver> 的 export templates。模板没装时本脚本会自动下载安装
      (从 GitHub 官方全平台 zip 提取 Windows 部分, 一次性)。GitHub 慢可 --template-url 换镜像,
      或手动下 zip 再 --godot-template <zip>。--no-export 则完全不导出。
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_common as common

PLATFORM = "windows"
DEFAULT_SRC = os.path.join(
    common.project_root(), "build", "windows", "avplay", "install", "AMD64", "Release")
DEFAULT_TOOLS = os.path.join(common.project_root(), "platform", "godot", "tools")
DEFAULT_PLUGIN = os.path.join(common.project_root(), "platform", "godot", "plugin")
# Godot 编辑器 exe: 优先环境变量, 否则常见安装位置
DEFAULT_GODOT_EXE = os.environ.get("AVOX_GODOT_EXE", r"D:\Work\godot\godot.exe")
EXPORT_EXE_NAME = "avox_tools.exe"
EXPORT_PRESET = "Windows Desktop"
# 终端工具 exe (avox_cli 命令行控制台 / avox_agent AI 智能体), 随运行时铺到产物根
CLI_TOOLS = ("avox_cli.exe", "avox_agent.exe")

# 导出暂存副本要排除的 (避免 .godot 缓存 / bin junction / 开发日志污染工程)
STAGE_IGNORE_DIRS = {".godot"}
STAGE_IGNORE_SUFFIXES = (".log",)


# ── avox 运行时铺设 ──────────────────────────────────────────
def copy_runtime_top(src, out):
    """顶层运行文件: *.dll/*.node/AvoxWrapper.py, 剔除插件第三方依赖与临时锁文件。"""
    copied = 0
    for f in sorted(os.listdir(src)):
        s = os.path.join(src, f)
        if not os.path.isfile(s):
            continue
        if f.startswith("~"):                       # 编辑器/进程锁残留 (~xxx.TMP)
            continue
        if f.endswith((".dll", ".node")) or f == "AvoxWrapper.py":
            if common.is_plugin_dep(f):             # onnxruntime/opencv/openvino/tbb/sherpa-onnx -> fetch 下载
                continue
            common.copy_file(s, os.path.join(out, f))
            copied += 1
    print(f"  [复制] 顶层运行文件 ({copied} 个)")
    return copied


def copy_cli_tools(src, out):
    """拷 avox 终端工具 (avox_cli.exe / avox_agent.exe) 到产物根 (依赖顶层 avox.dll); 缺则跳过。"""
    copied = []
    for name in CLI_TOOLS:
        s = os.path.join(src, name)
        if os.path.isfile(s):
            common.copy_file(s, os.path.join(out, name))
            copied.append(name)
    if copied:
        print(f"  [复制] 终端工具 ({', '.join(copied)})")
    else:
        print("  [跳过] 未见终端工具 (avox_cli.exe/avox_agent.exe)")
    return len(copied)


def lay_avox_runtime(src, out, with_thirdparty):
    """铺 avox 运行时到 out: 顶层 dll + 终端工具 + plugins 壳 + assets 子集 (无模型)。"""
    copy_runtime_top(src, out)
    copy_cli_tools(src, out)
    common.copy_plugins(os.path.join(src, "plugins"), os.path.join(out, "plugins"), with_thirdparty)
    # sherpa-onnx-c-api.dll 硬依赖 onnxruntime.dll (语音输入必需)。构建树放在 src 顶层,
    # copy_runtime_top 会按插件依赖过滤它, src/plugins 里又没有 —— 必须显式补,
    # 按 manifest 约定放 plugins/ (avox 加载插件时 AddDllDirectory 会搜索该目录)
    onnx = os.path.join(src, "onnxruntime.dll")
    if os.path.isfile(onnx):
        common.copy_file(onnx, os.path.join(out, "plugins", "onnxruntime.dll"))
    else:
        print("  [警告] 未见 onnxruntime.dll, 语音输入/ONNX 推理将不可用")
    common.copy_assets(os.path.join(src, "assets"), os.path.join(out, "assets"))
    # avox_godot.dll 兜底 (导出时 Godot 也会拷, 这里确保一定在 out 根)
    gd = os.path.join(src, "avox_godot.dll")
    if os.path.isfile(gd):
        common.copy_file(gd, os.path.join(out, "avox_godot.dll"))


# ── Godot 导出 ──────────────────────────────────────────────
def godot_version(godot_exe):
    """godot --version -> '4.7.1.stable' (取前 4 段: major.minor.patch.status)。"""
    r = subprocess.run([godot_exe, "--version"], capture_output=True, text=True)
    raw = (r.stdout or "").strip() or (r.stderr or "").strip()
    if not raw:
        raise SystemExit(f"无法获取 godot 版本 (检查 --godot-exe): {godot_exe}")
    parts = raw.split(".")
    return ".".join(parts[:4]) if len(parts) >= 4 else raw


def template_dir(ver):
    # Windows: Godot 模板放 %APPDATA% (Roaming), 非 LOCALAPPDATA
    base = os.environ.get("APPDATA") or os.environ.get("LOCALAPPDATA") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "Godot", "export_templates", ver)


def template_download_url(ver):
    """4.7.1.stable -> GitHub 官方全平台模板包 (.tpz, 实为 zip) 的 URL 与 git tag。"""
    parts = ver.split(".")
    if len(parts) >= 4:
        tag = ".".join(parts[:3]) + "-" + parts[3]            # 4.7.1-stable
    else:
        tag = ver
    asset = f"Godot_v{tag}_export_templates.tpz"
    return f"https://github.com/godotengine/godot/releases/download/{tag}/{asset}", tag


# Godot 4.4+ 模板改名 windows_release_x86_64.exe; 更早叫 template_release.windows.x86_64.exe。
# 检测两种都认; 提取时保留 .tpz 内原名 (4.7 即 windows_release_x86_64.exe)。
RELEASE_TEMPLATE_NAMES = ("windows_release_x86_64.exe", "template_release.windows.x86_64.exe")


def release_template_path(tdir):
    """已装的 release 模板路径 (4.7+ 或旧名); 都没有时返回 4.7+ 默认名。"""
    for n in RELEASE_TEMPLATE_NAMES:
        p = os.path.join(tdir, n)
        if os.path.isfile(p):
            return p
    return os.path.join(tdir, RELEASE_TEMPLATE_NAMES[0])


def _is_win_x64_template(name):
    """匹配 Windows x86_64 release 模板条目 (含 console 包装器); 跳过 debug (我们只做 release 导出)。"""
    b = os.path.basename(name).lower()
    return "windows_release" in b and "x86_64" in b


def install_from_zip(zip_path, dest_dir):
    """从 export_templates(.tpz/.zip, 内含 templates/ 前缀) 提取 Windows x86_64 模板 (保留原名) 到 dest_dir。"""
    os.makedirs(dest_dir, exist_ok=True)
    n = 0
    with zipfile.ZipFile(zip_path) as zf:
        for info in zf.infolist():
            if not _is_win_x64_template(info.filename):
                continue
            base = os.path.basename(info.filename)
            with zf.open(info) as src, open(os.path.join(dest_dir, base), "wb") as dst:
                shutil.copyfileobj(src, dst)
            print(f"    [解压] {base}")
            n += 1
    print(f"  [模板] 提取 {n} 个 Windows 模板 -> {dest_dir}")
    return n


def install_template_from_local(path, dest_dir):
    """--godot-template: 给 .tpz/.zip 就提取 Windows 部分; 给 exe 就当 release 模板装。"""
    if path.lower().endswith((".zip", ".tpz")):
        print(f"  [模板] 从本地模板包提取: {path}")
        install_from_zip(path, dest_dir)
    else:
        os.makedirs(dest_dir, exist_ok=True)
        shutil.copy2(path, os.path.join(dest_dir, RELEASE_TEMPLATE_NAMES[0]))
        print(f"  [模板] 安装 release 模板 -> {dest_dir}")


def _confirm(prompt):
    try:
        return input(prompt).strip().lower() in ("", "y", "yes")
    except EOFError:
        return False


class _RangeFile:
    """HTTP Range 支撑的随机只读文件, 喂给 zipfile 做按需分段读 (不整包下载)。"""
    def __init__(self, url, size):
        self.url = url
        self.size = size
        self.pos = 0
        self.reqs = 0
        self.bytes = 0
        self.progress = None

    def seekable(self):
        return True

    def seek(self, p, w=0):
        self.pos = p if w == 0 else (self.pos + p if w == 1 else self.size + p)
        return self.pos

    def tell(self):
        return self.pos

    def read(self, n=-1):
        if n < 0 or self.pos + n > self.size:
            n = self.size - self.pos
        if n <= 0:
            return b""
        req = urllib.request.Request(
            self.url, headers={"User-Agent": "pack_godot/1.0",
                               "Range": f"bytes={self.pos}-{self.pos + n - 1}"})
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
        self.reqs += 1
        self.bytes += len(data)
        self.pos += len(data)
        if self.progress:
            self.progress(self.bytes)
        return data


def _http_total_and_range(url):
    """返回 (总大小, 是否支持 Range 分段读)。"""
    try:
        with urllib.request.urlopen(urllib.request.Request(
                url, method="HEAD", headers={"User-Agent": "pack_godot/1.0"}), timeout=30) as h:
            total = int(h.headers.get("Content-Length") or 0)
            accept = (h.headers.get("Accept-Ranges") or "").lower()
    except Exception:
        return 0, False
    if not total:
        return 0, False
    if accept == "bytes":
        return total, True
    try:                                                            # 头里没声明, 实测一次
        with urllib.request.urlopen(urllib.request.Request(
                url, headers={"User-Agent": "pack_godot/1.0", "Range": "bytes=0-0"}), timeout=30) as r:
            return total, r.status == 206
    except Exception:
        return total, False


def _write_sel_progress(got, total_bytes):
    """selective 下载进度 (单行刷新, 写 stderr)。"""
    if total_bytes <= 0:
        return
    pct = min(100, got * 100 // total_bytes)
    sys.stderr.write(f"\r  下载 {pct}% ({min(got, total_bytes) // 1048576}/{total_bytes // 1048576} MB)")
    sys.stderr.flush()


def _selective_download(url, total, dest_dir, yes):
    """分段读 .tpz: 只下 Windows 条目 (~几十MB), 不碰其它平台 (省掉 1.1GB)。"""
    print("  [模板] 分段下载 (只取 Windows, 跳过 Android/iOS/macOS/Linux/Web)")
    rf = _RangeFile(url, total)
    try:
        zf = zipfile.ZipFile(rf)                                    # 只读末尾索引 (~几KB)
    except Exception as e:
        print(f"  [模板] 读取模板包索引失败: {e}")
        return False
    want = [i for i in zf.infolist() if _is_win_x64_template(i.filename)]
    if not want:
        print("  [模板] 模板包里没找到 Windows x86_64 条目")
        return False
    sum_bytes = sum(i.compress_size for i in want)
    print(f"         只需 ~{sum_bytes // 1048576}MB (全包 {total // 1048576}MB, 省 ~{(total - sum_bytes) // 1048576}MB)")
    if not yes and not _confirm("  开始下载? [Y/n] "):
        print("  [模板] 已取消。手动下载 .tpz 后 --godot-template <文件>, 或 --no-export 逃生舱")
        return False
    os.makedirs(dest_dir, exist_ok=True)
    rf.progress = lambda b: _write_sel_progress(b, sum_bytes)       # 提取阶段进度 (实下字节/目标)
    n = 0
    for i in want:
        base = os.path.basename(i.filename)
        try:
            data = zf.read(i)                                       # 触发该条目的 Range 读
        except Exception as e:
            sys.stderr.write("\n")
            print(f"  [模板] 提取 {base} 失败: {e}")
            continue
        with open(os.path.join(dest_dir, base), "wb") as f:
            f.write(data)
        sys.stderr.write("\n")
        print(f"    [提取] {base} ({len(data) // 1048576}MB)")
        n += 1
    print(f"  [模板] 装好 {n} 个 Windows 模板 (实下 {rf.bytes // 1048576}MB) -> {dest_dir}")
    return n > 0


def _full_download(url, dest_dir, yes):
    """Range 不支持时的兜底: 整包下载 (1.2GB) 再提取 Windows 部分。"""
    print("  [模板] 整包下载 (CDN 不支持分段, 只能下全平台包再提取 Windows)")
    print(f"         {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "pack_godot/1.0"})
    try:
        resp = urllib.request.urlopen(req, timeout=30)
    except Exception as e:
        print(f"  [模板] 连接失败: {e}")
        print(f"         可手动下载该 .tpz, 再用 --godot-template <文件> 安装")
        return False
    try:
        totalsize = int(resp.headers.get("Content-Length") or 0)
        if totalsize:
            print(f"         大小约 {totalsize // (1024 * 1024)} MB (提取后只留 Windows ~几十MB)")
        if not yes and not _confirm("  开始下载? [Y/n] "):
            print("  [模板] 已取消。手动下载 .tpz 后 --godot-template <文件>, 或 --no-export 逃生舱")
            return False
        tmp = os.path.join(tempfile.gettempdir(), f"godot_templates_{os.getpid()}.tpz")
        got = 0
        with open(tmp, "wb") as out:
            while True:
                chunk = resp.read(1024 * 256)
                if not chunk:
                    break
                out.write(chunk)
                got += len(chunk)
                if totalsize:
                    sys.stderr.write(f"\r  下载 {min(100, got * 100 // totalsize)}% "
                                     f"({got // (1024 * 1024)}/{totalsize // (1024 * 1024)} MB)")
                    sys.stderr.flush()
        sys.stderr.write("\n")
    finally:
        resp.close()
    try:
        install_from_zip(tmp, dest_dir)
        return True
    except Exception as e:
        print(f"  [模板] 解压失败: {e}")
        return False
    finally:
        if os.path.exists(tmp):
            try:
                os.remove(tmp)
            except OSError:
                pass


def download_and_install_template(url, dest_dir, yes):
    """优先分段下载 (只取 Windows ~几十MB); CDN 不支持 Range 时退回整包。"""
    total, ranged = _http_total_and_range(url)
    if ranged:
        return _selective_download(url, total, dest_dir, yes)
    return _full_download(url, dest_dir, yes)


def _template_missing_error(godot_exe, ver, release):
    url, _ = template_download_url(ver)
    raise SystemExit(
        f"缺少 Godot 导出模板: {release}\n"
        f"  装法 1: 让本脚本自动下载 (默认会提示, 只取 Windows ~几十MB): python pack_godot.py ...\n"
        f"  装法 2: 手动下载 {url}\n"
        f"          再 python pack_godot.py --godot-template <下载的.tpz>\n"
        f"  装法 3: Godot 编辑器({godot_exe}) -> Editor 菜单 -> Manage Export Templates -> Download (版本 {ver})\n"
        f"  逃生舱: python pack_godot.py --no-export  (不导出, 编辑器+源码模式, 不可移植)")


def ensure_template(godot_exe, ver, godot_template, template_url, no_download, yes):
    """确保导出模板就位:
    1) --godot-template(本地 .tpz/.zip/exe)直接装;
    2) 已装则跳过;
    3) 缺则自动下载 (分段优先, 只取 Windows ~几十MB; --no-download 禁用; --template-url 换镜像)。"""
    tdir = template_dir(ver)
    release = release_template_path(tdir)
    if godot_template:
        install_template_from_local(godot_template, tdir)
    if os.path.isfile(release):
        return release
    if no_download:
        _template_missing_error(godot_exe, ver, release)
    url = template_url or template_download_url(ver)[0]
    print(f"  [模板] 未安装: {release}")
    if download_and_install_template(url, tdir, yes):
        got = release_template_path(tdir)
        if os.path.isfile(got):
            return got
    _template_missing_error(godot_exe, ver, release)


def _stage_ignore(directory, names):
    """copytree 忽略: .godot 缓存 / 日志 / 插件 bin junction (单独重建只放 avox_godot.dll)。"""
    kept = []
    for n in names:
        if n in STAGE_IGNORE_DIRS:
            kept.append(n)
        elif n.endswith(STAGE_IGNORE_SUFFIXES):
            kept.append(n)
        elif n == "bin" and os.path.basename(directory) == "avox_godot":
            kept.append(n)   # junction -> 整个 Release 树, 绝不能跟着拷
    return kept


def build_stage(src, tools_project, plugin_dir, stage):
    """拷贝 tools 工程到 stage (排除 .godot/bin junction/日志), 重建真实 bin 只放 avox_godot.dll。"""
    shutil.copytree(tools_project, stage, ignore=_stage_ignore, dirs_exist_ok=True)
    addon = os.path.join(stage, "addons", "avox_godot")
    bin_dir = os.path.join(addon, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    # 规范 .gdextension (不依赖 tools 里 deploy 的副本, 确定取仓库静态文件)
    gdext = os.path.join(plugin_dir, "avox_godot.gdextension")
    shutil.copy2(gdext, os.path.join(addon, "avox_godot.gdextension"))
    # bin 只放插件入口 dll (真实目录, 非 junction; 避免 Godot 顺 junction 全扫 Release 进 pck)
    shutil.copy2(os.path.join(src, "avox_godot.dll"), os.path.join(bin_dir, "avox_godot.dll"))
    # .gdignore 双保险: 让资源扫描器跳过 bin (gdextension 仍能解析引用的 dll)
    open(os.path.join(bin_dir, ".gdignore"), "w").close()


EXPORT_PRESETS_CFG = """\
[preset.0]
name="Windows Desktop"
platform="Windows Desktop"
runnable=true
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_library_dependencies=true
script_export_mode=2

[preset.0.options]
custom_template/debug=""
custom_template/release=""
binary_format/embed_pck=true
binary_format/64_bits=true
"""


def export_project(godot_exe, stage, out_exe):
    """godot --headless --import 然后 --export-release, 产物落到 out_exe。"""
    cfg = os.path.join(stage, "export_presets.cfg")
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(EXPORT_PRESETS_CFG)
    # 1) 首扫/重导入: 生成 .godot, 注册 GDExtension
    print("  [导出] 重导入工程...")
    r1 = subprocess.run([godot_exe, "--headless", "--import", "--path", stage],
                        capture_output=True, text=True)
    if r1.returncode != 0:
        print(r1.stdout, end="")
        print(r1.stderr, end="", file=sys.stderr)
        raise SystemExit(f"godot --import 失败 (exit={r1.returncode})")
    # 2) 导出发布版
    print(f"  [导出] --export-release {EXPORT_PRESET} -> {out_exe}")
    r2 = subprocess.run(
        [godot_exe, "--headless", "--export-release", EXPORT_PRESET, out_exe, "--path", stage],
        capture_output=True, text=True)
    print(r2.stdout, end="")
    if r2.returncode != 0:
        print(r2.stderr, end="", file=sys.stderr)
        raise SystemExit(f"godot --export-release 失败 (exit={r2.returncode})\n"
                         f"  常见: 模板版本不匹配 / gdextension 未识别 (看上面 godot 输出)")


def do_export(godot_exe, godot_template, template_url, no_download, yes, src, tools_project, plugin_dir, out):
    """导出 Godot 工程发布版到 out/avox_tools.exe; 返回 exe 路径或 None。"""
    if not os.path.isfile(godot_exe):
        raise SystemExit(f"找不到 Godot 编辑器: {godot_exe}\n  用 --godot-exe 或环境变量 AVOX_GODOT_EXE 指定")
    ver = godot_version(godot_exe)
    print(f"  [godot] 版本 {ver}")
    ensure_template(godot_exe, ver, godot_template, template_url, no_download, yes)
    stage = tempfile.mkdtemp(prefix="avox_godot_stage_")
    try:
        print(f"  [暂存] {stage}")
        build_stage(src, tools_project, plugin_dir, stage)
        out_exe = os.path.join(out, EXPORT_EXE_NAME)
        export_project(godot_exe, stage, out_exe)
        return out_exe
    finally:
        try:
            shutil.rmtree(stage, ignore_errors=True)
        except OSError as e:
            print(f"  [警告] 暂存目录清理失败 (可手删): {stage}: {e}")


# ── --no-export 逃生舱 (编辑器 + 源码, 不可移植) ────────────
def do_no_export(godot_exe, src, tools_project, plugin_dir, out):
    """不导出: 工程源码拷进 out/project, 真实 bin 只放 avox_godot.dll, 写 run.bat 调编辑器 --path。"""
    proj = os.path.join(out, "project")
    if os.path.exists(proj):
        shutil.rmtree(proj, ignore_errors=True)
    shutil.copytree(tools_project, proj, ignore=_stage_ignore)
    addon = os.path.join(proj, "addons", "avox_godot")
    bin_dir = os.path.join(addon, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    shutil.copy2(os.path.join(plugin_dir, "avox_godot.gdextension"),
                 os.path.join(addon, "avox_godot.gdextension"))
    shutil.copy2(os.path.join(src, "avox_godot.dll"), os.path.join(bin_dir, "avox_godot.dll"))
    open(os.path.join(bin_dir, ".gdignore"), "w").close()
    # run.bat: 用本机编辑器跑工程 (不可移植, 仅供本地测试)
    bat = os.path.join(out, "run.bat")
    godot_abs = os.path.abspath(godot_exe)
    with open(bat, "w", encoding="ascii") as f:
        f.write('@echo off\r\n')
        f.write(f'"{godot_abs}" --path "%~dp0project"\r\n')
    print(f"  [逃生舱] 工程源码 -> {proj}")
    print(f"  [逃生舱] 启动脚本 -> {bat} (调本机 {godot_abs})")


# ── README / 收尾 ───────────────────────────────────────────
def write_readme(out, exported, with_thirdparty):
    path = os.path.join(out, "README.txt")
    lines = [
        "avox Godot 工具箱",
        "",
        "运行: 双击 " + (EXPORT_EXE_NAME if exported else "run.bat"),
    ]
    if not exported:
        lines += ["", "注意: 本包为 --no-export 模式, run.bat 调用本机 Godot 编辑器, 不可移植到其他机器。"]
    if not with_thirdparty:
        lines += [
            "",
            "按需下载 AI 模型 / 第三方库 (默认未打包, 按功能下载):",
            "  cd " + out,
            "  python assets\\script\\fetch_assets.py check            # 看缺什么",
            "  python assets\\script\\fetch_assets.py --interactive    # 交互勾选",
            "  python assets\\script\\fetch_assets.py --select sherpa_zh_en   # 例: 下语音识别模型",
        ]
    lines += ["", "终端工具 avox_cli.exe (命令行控制台) / avox_agent.exe (AI 智能体) 在本目录, 命令行运行。"]
    lines += ["", "avox_godot.dll / avox.dll / ffmpeg 等运行时已在本目录, 勿移动。"]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\r\n".join(lines) + "\r\n")
    print(f"  [写] {path}")


def pack(out="", src="", godot_project="", godot_exe="", godot_template="",
         template_url="", no_download=False, yes=False,
         no_export=False, zip_path="", with_thirdparty=False):
    out = out or os.path.join(common.project_root(), "deploy", "godot")
    src = src or DEFAULT_SRC
    godot_project = godot_project or DEFAULT_TOOLS
    godot_exe = godot_exe or DEFAULT_GODOT_EXE
    if not os.path.isdir(src):
        raise SystemExit(f"找不到 avox 构建安装树: {src}\n请先构建, 或用 --src 指定")
    if not os.path.isfile(os.path.join(src, "avox.dll")):
        raise SystemExit(f"{src} 下没有 avox.dll, 确认是构建安装目录")
    if not os.path.isfile(os.path.join(src, "avox_godot.dll")):
        raise SystemExit(f"{src} 下没有 avox_godot.dll, 先构建 avox (顶层 AVOX_ENABLE_GODOT, Windows 默认 ON)")
    if not os.path.isfile(os.path.join(godot_project, "project.godot")):
        raise SystemExit(f"找不到 Godot 工程: {godot_project}\\project.godot")

    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)

    print(f"== 打包 avox Godot 工具箱 ==")
    print(f"  源 (avox 安装树): {src}")
    print(f"  Godot 工程:      {godot_project}")
    print(f"  产物目录:        {out}")
    print(f"  模式: {'--no-export (编辑器+源码)' if no_export else '导出发布版'}")

    # 1) 铺 avox 运行时 (扁平到 out 根; 无模型, AI 库默认按需下载)
    lay_avox_runtime(src, out, with_thirdparty)

    # 2) Godot 侧
    if no_export:
        do_no_export(godot_exe, src, godot_project, DEFAULT_PLUGIN, out)
        exported = False
    else:
        exe = do_export(godot_exe, godot_template, template_url, no_download, yes,
                        src, godot_project, DEFAULT_PLUGIN, out)
        if not exe or not os.path.isfile(exe):
            raise SystemExit("导出失败, 未见 " + EXPORT_EXE_NAME)
        print(f"  [导出] 产物: {exe}")
        exported = True

    # 3) README + 下载提示
    write_readme(out, exported, with_thirdparty)
    if not with_thirdparty:
        common.print_thirdparty_hint()

    # 4) 可选 zip
    if zip_path:
        if os.path.isdir(zip_path):
            zip_path = os.path.join(zip_path, f"avox-godot-{PLATFORM}-x64-{common.get_version()}.zip")
        common.make_zip(out, zip_path)

    print(f"== 完成: {out} ==")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="", help="产物目录 (默认 deploy/godot)")
    ap.add_argument("--src", default="", help="avox 构建安装树 (默认 build/.../Release, 只读)")
    ap.add_argument("--godot-project", default="", help="Godot 工程根 (默认 platform/godot/tools)")
    ap.add_argument("--godot-exe", default="", help="Godot 编辑器 exe (默认 D:\\Work\\godot\\godot.exe 或 AVOX_GODOT_EXE)")
    ap.add_argument("--godot-template", default="",
                    help="手动下载的 export_templates.tpz/.zip 或 template_release exe, 自动装到模板目录")
    ap.add_argument("--template-url", default="",
                    help="覆盖自动下载模板的 URL (GitHub 慢时换镜像)")
    ap.add_argument("--no-download", action="store_true",
                    help="模板缺失时不自动下载, 只报错 (默认会提示下载)")
    ap.add_argument("-y", "--yes", action="store_true", help="自动确认下载等交互提示")
    ap.add_argument("--no-export", action="store_true",
                    help="不导出, 编辑器+源码模式 (不可移植, 本地测; 模板没装时用)")
    ap.add_argument("--zip", default="", help="额外把产物打 zip (zip 文件路径)")
    ap.add_argument("--with-thirdparty", action="store_true",
                    help="连 AI 第三方库 (onnx/opencv/openvino/sherpa) 一起打包 (默认按需下载)")
    args = ap.parse_args(argv)
    pack(out=args.out, src=args.src, godot_project=args.godot_project, godot_exe=args.godot_exe,
         godot_template=args.godot_template, template_url=args.template_url,
         no_download=args.no_download, yes=args.yes, no_export=args.no_export,
         zip_path=args.zip, with_thirdparty=args.with_thirdparty)


if __name__ == "__main__":
    main()
