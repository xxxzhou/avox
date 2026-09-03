# avox SDK Python 高层封装 — Common 模块
# IOption (参数设置器) 封装, 含所有可用 key 常量与类型安全访问

import AvoxWrapper as _pw
from avox._core import ArgType

# ─── Option Key 常量 ────────────────────────────────────
# 对应 C++ avox/module/OptionKey.hpp, 按 key 前缀分组

# ── 播放器 (mp.*) ──

#: 低延迟模式 (bool, 默认 false)
KEY_LOW_LATENCY = "mp.lowlatency"
#: 低延迟加速倍速 (double, 默认 1.2, 需 >1.0)
KEY_LL_SPEED = "mp.lowlatency.speed"
#: 播放器延迟时间 (int, 毫秒, 默认 1000)
KEY_DELAY_MS = "mp.delay.ms"
#: 窗口双倍刷新 (bool, 默认 false)
KEY_WINDOW_DOUBLE_REFRESH = "mp.window.double.refresh"
#: 变速类型 (int, 1=local 2=server)
KEY_SPEED_TYPE = "mp.speed.type"
#: 严格大于4倍速只处理I帧 (bool, 默认 true, 恰好4倍仍全量)
KEY_IFRAME_ONLY_GT4 = "mp.iframe.gt4"
#: 主时钟类型 (int, 0=none 1=audio(默认) 2=video 3=external)
KEY_SYNC_TYPE = "mp.synctype"

# ── IO (io.*) ──

#: IO 超时 (int, 毫秒)
KEY_IO_TIMEOUT_MS = "io.timeout.ms"
#: RTSP 传输协议 (string, "udp"/"tcp")
KEY_IO_RTSP_TRANSPORT = "io.rtsp.transport"
#: Track ready 等待超时 (int, 毫秒, 默认 3000)
KEY_IO_TRACK_READY_MS = "io.trackready.ms"

# ── 日志 (log.*) ──

#: IO 线程包信息 (bool)
KEY_LOG_SOURCE_PACKET = "log.source.packet"
#: 解码器帧信息 (bool)
KEY_LOG_DECODER_FRAME = "log.decoder.frame"
#: 渲染帧信息 (bool)
KEY_LOG_RENDER_FRAME = "log.render.frame"

# ─── Key → (ArgType, 说明) 注册表 ──────────────────────
# 用于 get/set 时自动推断类型, 以及 asDict() 输出说明

_KEY_REGISTRY = {
    KEY_LOW_LATENCY:           (ArgType.Boolean, "低延迟模式"),
    KEY_LL_SPEED:              (ArgType.Number,  "低延迟加速倍速 (>1.0)"),
    KEY_DELAY_MS:              (ArgType.Int,     "播放器延迟 (ms)"),
    KEY_WINDOW_DOUBLE_REFRESH: (ArgType.Boolean, "窗口双倍刷新"),
    KEY_SPEED_TYPE:            (ArgType.Int,     "变速类型 (1=local 2=server)"),
    KEY_IFRAME_ONLY_GT4:       (ArgType.Boolean, "大于4倍只处理I帧 (默认true)"),
    KEY_SYNC_TYPE:             (ArgType.Int,     "主时钟 (0=none 1=audio 2=video 3=ext)"),
    KEY_IO_TIMEOUT_MS:         (ArgType.Int,     "IO 超时 (ms)"),
    KEY_IO_RTSP_TRANSPORT:     (ArgType.String,  "RTSP 传输 (udp/tcp)"),
    KEY_IO_TRACK_READY_MS:     (ArgType.Int,     "Track ready 超时 (ms)"),
    KEY_LOG_SOURCE_PACKET:     (ArgType.Boolean, "IO 包日志"),
    KEY_LOG_DECODER_FRAME:     (ArgType.Boolean, "解码帧日志"),
    KEY_LOG_RENDER_FRAME:      (ArgType.Boolean, "渲染帧日志"),
}

# ─── IOption 封装 ───────────────────────────────────────

class IOption:
    """参数设置器封装, key 常量 + 自动类型推断的 get/set/[] 访问。

    用法::

        from avox import Common

        opt = player.getOption()

        # 通用 set (按注册类型或 Python 类型自动分发)
        opt.set(Common.KEY_LOW_LATENCY, True)
        opt.set(Common.KEY_DELAY_MS, 200)
        opt.set(Common.KEY_IO_RTSP_TRANSPORT, "tcp")

        # dict 风格
        opt[Common.KEY_LOG_SOURCE_PACKET] = True

        # 通用 get
        opt.get(Common.KEY_DELAY_MS)       # 200
        opt.get("unknown.key", default=0)  # 0 (不存在时返回 default)

        # 导出
        opt.asDict()   # {key: (value, desc), ...}

        # 低层: 类型确定时直接调原生方法
        opt.setBool(Common.KEY_LOW_LATENCY, True)
        opt.getInt(Common.KEY_DELAY_MS)
    """

    def __init__(self, native):
        self._native = native

    # ── 原生代理 (类型确定时可直接用) ──

    def setBool(self, key, value):
        """设置 bool 值。"""
        return self._native.setBool(key, value)

    def setInt(self, key, value):
        """设置 int 值。"""
        return self._native.setInt(key, value)

    def setString(self, key, value):
        """设置 string 值。"""
        return self._native.setString(key, value)

    def setNumber(self, key, value):
        """设置 double 值。"""
        return self._native.setNumber(key, value)

    def getType(self, key):
        """获取 key 的 ArgType。"""
        return ArgType(self._native.getType(key))

    def getInt(self, key):
        """获取 int 值。"""
        return self._native.getInt(key)

    def getDouble(self, key):
        """获取 double 值。"""
        return self._native.getDouble(key)

    def getString(self, key):
        """获取 string 值。"""
        return self._native.getString(key)

    def getBool(self, key):
        """获取 bool 值。"""
        return self._native.getBool(key)

    # ── 通用 get/set (自动推断类型) ──

    def get(self, key, default=None):
        """通用 get: 按 key 注册类型或运行时类型自动取值, 不存在返回 default。"""
        reg = _KEY_REGISTRY.get(key)
        if reg:
            argType = reg[0]
        else:
            try:
                argType = ArgType(self._native.getType(key))
            except Exception:
                return default
        if argType == ArgType.Null:
            return default
        if argType == ArgType.Boolean:
            return self._native.getBool(key)
        if argType == ArgType.Int:
            return self._native.getInt(key)
        if argType == ArgType.Number:
            return self._native.getDouble(key)
        if argType == ArgType.String:
            return self._native.getString(key)
        return default

    def set(self, key, value):
        """通用 set: 按 key 注册类型或 Python 值类型自动分发。

        注册表有的 key 按注册类型; 未注册的按 Python 类型推断
        (bool→setBool, int→setInt, float→setNumber, str→setString)。
        """
        reg = _KEY_REGISTRY.get(key)
        if reg:
            argType = reg[0]
            if argType == ArgType.Boolean:
                return self._native.setBool(key, bool(value))
            if argType == ArgType.Int:
                return self._native.setInt(key, int(value))
            if argType == ArgType.Number:
                return self._native.setNumber(key, float(value))
            if argType == ArgType.String:
                return self._native.setString(key, str(value))
        # 未注册: 按 Python 类型推断 (注意 bool 是 int 子类, 需先判)
        if isinstance(value, bool):
            return self._native.setBool(key, value)
        if isinstance(value, int):
            return self._native.setInt(key, value)
        if isinstance(value, float):
            return self._native.setNumber(key, value)
        if isinstance(value, str):
            return self._native.setString(key, value)
        raise TypeError(f"IOption.set: unsupported value type {type(value)} for key {key!r}")

    # ── dict 风格访问 ──

    def __getitem__(self, key):
        val = self.get(key, _SENTINEL)
        if val is _SENTINEL:
            raise KeyError(key)
        return val

    def __setitem__(self, key, value):
        self.set(key, value)

    def __contains__(self, key):
        try:
            t = ArgType(self._native.getType(key))
            return t != ArgType.Null
        except Exception:
            return False

    # ── 导出 ──

    def asDict(self):
        """导出所有已注册 key 的当前值 → dict {key: (value, desc)}。

        只遍历 _KEY_REGISTRY 中的 key; 未注册的自定义 key 不含。
        """
        result = {}
        for key, (argType, desc) in _KEY_REGISTRY.items():
            try:
                rawType = ArgType(self._native.getType(key))
            except Exception:
                continue
            if rawType == ArgType.Null:
                continue
            if argType == ArgType.Boolean:
                val = self._native.getBool(key)
            elif argType == ArgType.Int:
                val = self._native.getInt(key)
            elif argType == ArgType.Number:
                val = self._native.getDouble(key)
            elif argType == ArgType.String:
                val = self._native.getString(key)
            else:
                continue
            result[key] = (val, desc)
        return result

    def keys(self):
        """返回所有已设值的注册 key 列表。"""
        return list(self.asDict().keys())

    @property
    def native(self):
        """底层原生 IOption (供低层操作使用)。"""
        return self._native


# ─── 内部哨兵 ────────────────────────────────────────────

_SENTINEL = object()

# ─── 模块级工厂函数 ─────────────────────────────────────

def createJsonOption():
    """创建独立的 JsonOption 实例 (非播放器内置, 可用于通用 key-value 存储)。"""
    native = _pw.createJsonOption()
    return IOption(native) if native else None