#!/usr/bin/env python3
# tools/dump_enums.py — 列出 avox SDK 全量枚举 (动态, 跟源码同步, 不维护静态表)
#
# 纯 AST 解析, 不 import avox / 不依赖 avox.dll —— 裸检出库、缺 DLL、纯文本环境都能跑。
# 枚举分两处来源, 写法不同:
#   1) avox._core        18 个 IntEnum 包装类 → 写 `Xxx.member`         (推荐)
#   2) AvoxWrapper.py    50+ 组 Enum_member 裸常量 → 写 `_pw.Xxx_member` (含 _core 没封的)
# 定位顺序: AVOX_HOME/plugins/ → 脚本自身上溯到 install root/plugins/ → cwd/plugins/ → cwd 仓库源码
#
# 用法 (run_code / avox_cli python / 系统 python 均可):
#   python dump_enums.py             # 全量 (_core 包装 + AvoxWrapper 裸常量)
#   python dump_enums.py --core-only # 仅包装枚举
#   python dump_enums.py --raw-only  # 仅 _core 未封的裸常量枚举

import argparse
import ast
import os
import re
import sys

# 运行时路径 (AVOX_HOME / install root 下) 与开发机仓库路径 (cwd 下)
_PLUGIN_CORE = os.path.join('plugins', 'avox', '_core.py')
_PLUGIN_WRAP = os.path.join('plugins', 'AvoxWrapper.py')
_REPO_CORE = os.path.join('swig', 'python', 'avox', '_core.py')
_REPO_WRAP = os.path.join('swig', 'python', 'files', 'AvoxWrapper.py')

# AvoxWrapper.py 模块级裸常量名形如 EnumPrefix_member (member 可含下划线, 如 AVOX_AUDIO_U8)
_RAW_RE = re.compile(r'^([A-Z][A-Za-z0-9]*)_(.+)$')

# 单枚举成员展示上限 (KeyCode 等成员极多, 截断并提示总数)
_MEMBER_CAP = 40


def _up(path, n):
    for _ in range(n):
        path = os.path.dirname(path)
    return path


def _locate(plugin_rel, repo_rel):
    """按优先级返回第一个存在的文件绝对路径, 找不到返回 None。

    脚本在 <install>/assets/agent/skills/avox-python-api/tools/, 上溯 6 级即 install root。
    """
    self_root = _up(os.path.abspath(__file__), 6)
    cands = []
    home = os.environ.get('AVOX_HOME', '')
    if home:
        cands.append(os.path.join(home, plugin_rel))
    cands.append(os.path.join(self_root, plugin_rel))
    cands.append(os.path.join(os.getcwd(), plugin_rel))
    cands.append(os.path.join(os.getcwd(), repo_rel))
    for p in cands:
        if os.path.isfile(p):
            return os.path.abspath(p)
    return None


def _read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def _is_enum_class(node):
    for b in node.bases:
        name = b.id if isinstance(b, ast.Name) else getattr(b, 'attr', None)
        if name in ('IntEnum', 'Enum'):
            return True
    return False


def parse_core(path):
    """_core.py → {EnumName: [member, ...]}。

    识别两种定义: class Xxx(IntEnum) 块 (静态成员); 及 X = _swig_enum('X', ...) (动态构造,
    成员留空, 由 main() 从 AvoxWrapper 同名前缀组补)。
    """
    tree = ast.parse(_read(path))
    enums = {}
    for node in tree.body:
        if isinstance(node, ast.ClassDef) and _is_enum_class(node):
            members = []
            for item in node.body:
                if isinstance(item, ast.Assign):
                    for t in item.targets:
                        if isinstance(t, ast.Name) and not t.id.startswith('_'):
                            members.append(t.id)
            enums[node.name] = members
            continue
        # 动态枚举: KeyCode = _swig_enum('KeyCode', 'KeyCode') → 成员留空, 后续从裸常量补
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and isinstance(node.value, ast.Call)
                and isinstance(node.value.func, ast.Name)
                and node.value.func.id == '_swig_enum'
                and node.value.args
                and isinstance(node.value.args[0], ast.Constant)):
            enums[node.value.args[0].value] = []
    return enums


def parse_wrapper(path):
    """AvoxWrapper.py → {EnumPrefix: [member, ...]} (模块级 Enum_member = ... 常量, 按出现序)。"""
    tree = ast.parse(_read(path))
    groups = {}
    for node in tree.body:
        if not (isinstance(node, ast.Assign) and len(node.targets) == 1):
            continue
        tgt = node.targets[0]
        if not isinstance(tgt, ast.Name):
            continue
        m = _RAW_RE.match(tgt.id)
        if m:
            groups.setdefault(m.group(1), []).append(m.group(2))
    return groups


def _fmt_members(members):
    shown = ', '.join(members[:_MEMBER_CAP])
    if len(members) > _MEMBER_CAP:
        shown += f' …(+{len(members) - _MEMBER_CAP} more)'
    return shown


def main():
    ap = argparse.ArgumentParser(description='dump avox 全量枚举 (动态, 跟源码同步)')
    ap.add_argument('--core-only', action='store_true', help='仅 _core 包装枚举')
    ap.add_argument('--raw-only', action='store_true', help='仅 _core 未封的 AvoxWrapper 裸常量枚举')
    args = ap.parse_args()

    core_path = _locate(_PLUGIN_CORE, _REPO_CORE)
    if not core_path:
        sys.exit('找不到 avox/_core.py: 设 AVOX_HOME, 或在 install root / 仓库根运行')

    core = parse_core(core_path)
    wrap_path = _locate(_PLUGIN_WRAP, _REPO_WRAP)
    wrapper_all = parse_wrapper(wrap_path) if wrap_path else {}
    # 动态构造的 core 枚举 (如经 _swig_enum 生成, 类体无静态成员) 从 AvoxWrapper 同名组补成员
    for cname, members in core.items():
        if not members and cname in wrapper_all:
            core[cname] = wrapper_all[cname]
    # 只保留 _core 没包装的裸常量组, 避免与包装枚举重复
    raw = {k: v for k, v in wrapper_all.items() if k not in core}

    lines = ['# avox 枚举 (动态生成, 以源码为准)', '']
    if wrap_path:
        lines.append(f'> 来源: `{core_path}` + `{wrap_path}`')
    else:
        lines.append(f'> 来源: `{core_path}` (AvoxWrapper.py 未找到, 跳过裸常量)')
    lines.append('')

    if not args.raw_only:
        lines.append('## 包装枚举 — `avox._core` (写 `Xxx.member`, 推荐)')
        lines.append('')
        lines.append(f'共 {len(core)} 个。')
        lines.append('')
        lines.append('| 枚举 | 成员 |')
        lines.append('|---|---|')
        for name, members in core.items():
            lines.append(f'| `{name}` | {_fmt_members(members)} |')
        lines.append('')

    if not args.core_only:
        lines.append('## 裸常量枚举 — `AvoxWrapper` (写 `_pw.Xxx_member`, _core 未封)')
        lines.append('')
        if not raw:
            lines.append('_(未找到 AvoxWrapper.py 或无额外裸常量)_')
        else:
            lines.append(f'共 {len(raw)} 组 (_core 未封的进阶/内部枚举前缀; 按需 grep AvoxWrapper.py 查成员)。')
            lines.append('')
            lines.append('| 前缀 | 成员 (用 `_pw.<前缀>_<成员>`) |')
            lines.append('|---|---|')
            for name in sorted(raw):
                lines.append(f'| `{name}` | {_fmt_members(raw[name])} |')
        lines.append('')

    print('\n'.join(lines))


if __name__ == '__main__':
    main()
