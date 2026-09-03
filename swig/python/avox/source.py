# avox SDK Python 高层封装 — Source 模块
# IVideoManager / IAudioManager (设备枚举与获取) 及 IVideoSource / IAudioSource
#
# ── 平台默认设备 SDK (getDefaltVideoSdk / getDefaltAudioSdk) ──
#   Windows : 视频用 win_capture, 音频用 wasapi
#   Android : 视频 and_ndkcamer2, 音频 android
#   iOS     : 视频 ios_avf,     音频 ios
#
# ── Windows 设备索引含义 (重点) ──
#   getVideoManager(win_capture):
#     设备列表 = 所有显示器(桌面/monitor) + 所有可枚举窗口(window)。
#     顺序为先 monitor(按 EnumDisplayMonitors) 后 window(按 EnumWindows);
#     用 device.getDeviceKind() 区分: monitor=桌面, window=窗口。
#   getAudioManager(wasapi):
#     index 0 = 默认麦克风 (mic)
#     index 1 = 默认声卡回环采集 (loopback, 默认播放设备的输出)
#     index 2+ = 其余麦克风端点
#
# 设备类别枚举 (与原生 AvoxSource.h 对应, 直接用 _pw.* 访问):
#   VDeviceKind: _pw.VDeviceKind_{none,camera,window,monitor}
#   ADeviceKind: _pw.ADeviceKind_{none,mic,loopback}
# 设备 SDK 枚举:
#   VDeviceSdk: _pw.VDeviceSdk_{none,win_mf,win_capture,and_ndkcamer2,ios_avf}
#   ADeviceSdk: _pw.ADeviceSdk_{none,wasapi,android,ios}

import AvoxWrapper as _pw

# ─── IVideoSource ───────────────────────────────────────


class IVideoSource:
    """视频设备源封装 (摄像头/窗口/桌面)。

    借用原生对象, 不接管其生命周期 (原生 manager 持有 device)。
    传给 ISourcePlayer.setVideoSource / IRtcPlayer.setVideoSource 时请用 .native。
    """

    def __init__(self, native):
        self._native = native

    def getDeviceName(self):
        """设备显示名。"""
        return self._native.getDeviceName()

    def getDeviceId(self):
        """设备标识 Id。"""
        return self._native.getDeviceId()

    def getDeviceKind(self):
        """设备类别 (VDeviceKind): camera / window / monitor。"""
        return self._native.getDeviceKind()

    def open(self):
        """打开设备。"""
        return self._native.open()

    def close(self):
        """关闭设备。"""
        return self._native.close()

    def bOpening(self):
        """是否正在打开。"""
        return self._native.bOpening()

    @property
    def native(self):
        """底层原生 IVideoSource (供 ISourcePlayer.setVideoSource 等使用)。"""
        return self._native


# ─── IAudioSource ───────────────────────────────────────


class IAudioSource:
    """音频设备源封装 (麦克风/声卡回环)。

    借用原生对象, 不接管其生命周期 (原生 manager 持有 device)。
    传给 ISourcePlayer.setAudioSource / IRtcPlayer.setAudioSource 时请用 .native。
    """

    def __init__(self, native):
        self._native = native

    def getDeviceName(self):
        """设备显示名。"""
        return self._native.getDeviceName()

    def getDeviceId(self):
        """设备标识 Id。"""
        return self._native.getDeviceId()

    def getDeviceKind(self):
        """设备类别 (ADeviceKind): mic / loopback。"""
        return self._native.getDeviceKind()

    def open(self):
        """打开设备。"""
        return self._native.open()

    def close(self):
        """关闭设备。"""
        return self._native.close()

    def bOpening(self):
        """是否正在打开。"""
        return self._native.bOpening()

    @property
    def native(self):
        """底层原生 IAudioSource (供 ISourcePlayer.setAudioSource 等使用)。"""
        return self._native


# ─── IVideoManager ──────────────────────────────────────


class IVideoManager:
    """视频设备管理器封装: 设备枚举、按索引/Id 获取。"""

    def __init__(self, native):
        self._native = native

    def getDeviceCount(self):
        """设备数量。"""
        return self._native.getDeviceCount()

    def getDevice(self, index):
        """按索引取设备, 返回 IVideoSource 封装; 越界返回 None。"""
        native = self._native.getDevice(index)
        return IVideoSource(native) if native else None

    def findDevice(self, id):
        """按 deviceId 取设备, 返回 IVideoSource 封装; 未找到返回 None。"""
        native = self._native.findDevice(id)
        return IVideoSource(native) if native else None

    def refreshDevices(self):
        """刷新设备列表 (部分 SDK 需手动调用, 如 win_capture 重新枚举桌面/窗口)。"""
        return self._native.refreshDevices()

    @property
    def native(self):
        """底层原生 IVideoManager。"""
        return self._native


# ─── IAudioManager ──────────────────────────────────────


class IAudioManager:
    """音频设备管理器封装: 设备枚举、按索引/Id 获取。"""

    def __init__(self, native):
        self._native = native

    def getDeviceCount(self):
        """设备数量。"""
        return self._native.getDeviceCount()

    def getDevice(self, index):
        """按索引取设备, 返回 IAudioSource 封装; 越界返回 None。"""
        native = self._native.getDevice(index)
        return IAudioSource(native) if native else None

    def findDevice(self, id):
        """按 deviceId 取设备, 返回 IAudioSource 封装; 未找到返回 None。"""
        native = self._native.findDevice(id)
        return IAudioSource(native) if native else None

    def refreshDevices(self):
        """刷新设备列表。"""
        return self._native.refreshDevices()

    @property
    def native(self):
        """底层原生 IAudioManager。"""
        return self._native


# ─── 模块级工厂函数 ─────────────────────────────────────


def getDefaltVideoSdk():
    """当前平台的默认视频设备 SDK (Win=win_capture, Android=and_ndkcamer2, iOS=ios_avf)。"""
    return _pw.getDefaltVideoSdk()


def getDefaltAudioSdk():
    """当前平台的默认音频设备 SDK (Win=wasapi, Android=android, iOS=ios)。"""
    return _pw.getDefaltAudioSdk()


def getVideoManager(sdk=None):
    """获取视频设备管理器, 返回 IVideoManager 封装; 失败返回 None。

    sdk 为 VDeviceSdk 枚举 (_pw.VDeviceSdk_win_capture / and_ndkcamer2 / ios_avf ...);
    省略时用当前平台默认 (getDefaltVideoSdk)。

    Windows (win_capture): 设备列表 = 所有显示器(桌面) + 所有可枚举窗口,
        顺序先 monitor 后 window; 用 device.getDeviceKind() 区分桌面与窗口。
    """
    if sdk is None:
        sdk = _pw.getDefaltVideoSdk()
    native = _pw.getVideoManager(sdk)
    return IVideoManager(native) if native else None


def getAudioManager(sdk=None):
    """获取音频设备管理器, 返回 IAudioManager 封装; 失败返回 None。

    sdk 为 ADeviceSdk 枚举 (_pw.ADeviceSdk_wasapi / android / ios);
    省略时用当前平台默认 (getDefaltAudioSdk)。

    Windows (wasapi) 设备索引:
        0 = 默认麦克风 (mic)
        1 = 默认声卡回环采集 (loopback)
        2+ = 其余麦克风端点
    """
    if sdk is None:
        sdk = _pw.getDefaltAudioSdk()
    native = _pw.getAudioManager(sdk)
    return IAudioManager(native) if native else None
