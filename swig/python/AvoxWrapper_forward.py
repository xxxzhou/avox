# AvoxWrapper 转发壳 — install root 放此文件, 真实模块在 plugins/
# 外部 python 从 install root import AvoxWrapper 时, 自动转发到 plugins/ 下的完整模块
import os, sys
_root = os.path.dirname(os.path.abspath(__file__))
_plugins = os.path.join(_root, "plugins")
# avox.dll 在 install root; _AvoxWrapper.pyd 在 plugins/ 链 avox.dll。
# Python 3.8+ 不再从 os.environ['PATH'] 解析扩展的 DLL 依赖 (PEP 570),
# 需用 os.add_dll_directory 显式注册; 同时加 PATH 兼容 <3.8 与子进程。
if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(_root)
if _root not in os.environ.get("PATH", ""):
    os.environ["PATH"] = _root + os.pathsep + os.environ.get("PATH", "")
if _plugins in sys.path:
    sys.path.remove(_plugins)
sys.path.insert(0, _plugins)
# 清除自身在 sys.modules 的缓存, 避免递归导入自身
sys.modules.pop("AvoxWrapper", None)
from AvoxWrapper import *   # 从 plugins/AvoxWrapper.py 导入所有符号
