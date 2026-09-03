# ── 自动探测 avox install root 并配路径 ──
# 让 from avox import Player 在任意目录都能工作 (无需 cd 到 install root)
# 探测: 1) AVOX_HOME 环境变量  2) 从 avox 包自身位置反推  3) PATH 搜索 avox.dll
import os as _os, sys as _sys

def _setup_avox_paths():
    if getattr(_sys, '_avox_paths_configured', False):
        return
    root = _os.environ.get('AVOX_HOME', '')
    # 从 avox 包自身位置反推: avox/__init__.py 在 <root>/plugins/avox/
    if not root or not _os.path.isfile(_os.path.join(root, 'avox.dll')):
        _init_dir = _os.path.dirname(_os.path.abspath(__file__))
        _plugins_dir = _os.path.dirname(_init_dir)
        _candidate = _os.path.dirname(_plugins_dir)
        if _os.path.isfile(_os.path.join(_candidate, 'avox.dll')):
            root = _candidate
    # PATH 搜索 avox.dll
    if not root or not _os.path.isfile(_os.path.join(root, 'avox.dll')):
        for _p in _os.environ.get('PATH', '').split(_os.pathsep):
            if _os.path.isfile(_os.path.join(_p, 'avox.dll')):
                root = _p
                break
    if not root:
        return  # 探测失败, 不阻断 (后续 import AvoxWrapper 会报错, 可设 AVOX_HOME)
    root = _os.path.abspath(root)
    plugins = _os.path.join(root, 'plugins')
    # os.add_dll_directory (Python 3.8+, PEP 570: _AvoxWrapper.pyd 链 avox.dll)
    if hasattr(_os, 'add_dll_directory'):
        _os.add_dll_directory(root)
    # PATH 兼容 <3.8 和子进程
    if root not in _os.environ.get('PATH', ''):
        _os.environ['PATH'] = root + _os.pathsep + _os.environ.get('PATH', '')
    # sys.path: 只加 plugins/ (AvoxWrapper.py + _AvoxWrapper.pyd 在此; 不加 install root,
    # 避免转发壳 AvoxWrapper.py 与 plugins/AvoxWrapper.py 互相递归)
    if plugins not in _sys.path:
        _sys.path.insert(0, plugins)
    _sys._avox_paths_configured = True

_setup_avox_paths()
del _setup_avox_paths

# avox SDK Python 高层封装
# 惰性导入子模块: from avox import Player; Player.IMediaPlayer()

__version__ = "1.0.0"

_SUBMODULES = {
    'Player': 'avox.player',
    'Input': 'avox.input',
    'Muxer': 'avox.muxer',
    'Audio': 'avox.audio',
    'Video': 'avox.video',
    'Source': 'avox.source',
    'Image': 'avox.image',
    'Vision': 'avox.vision',
    'Common': 'avox.common',
}

def __getattr__(name):
    """惰性导入子模块"""
    if name in _SUBMODULES:
        import importlib
        module = importlib.import_module(_SUBMODULES[name])
        globals()[name] = module
        return module
    raise AttributeError(f"module 'avox' has no attribute {name!r}")

def __dir__():
    return list(globals().keys()) + list(_SUBMODULES.keys())
