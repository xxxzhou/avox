# avox SDK Python 高层封装 — Muxer 模块
# IMediaMuxer, IRecorder

import AvoxWrapper as _pw
from avox._core import _CallbackBridge, RecorderState, _sourceInfoToDict
from avox._observer import _DefaultRecorderOb
from avox.video import ISurfaceRender
from avox.audio import IAudioRender

# ─── IMediaMuxer ────────────────────────────────────────

class IMediaMuxer(_CallbackBridge):
    """媒体封装器 (录制/推流) 封装。

    正规模式: addObserver(IRecorderOb子类) / removeObserver(ob)
    快捷模式: onStateChange(cb) / onProgress(cb) / onIoError(cb) / onEncodeError(cb) / onComplete(cb)
    """

    def __init__(self, native):
        super().__init__()
        self._native = native

    # ── Observer (正规模式) ──

    def _addObserverImpl(self, ob):
        _pw.addMuxerOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeMuxerOb(self._native, ob)

    # ── Observer (快捷模式) ──

    def _ensureDefaultObserver(self):
        if self._default_ob is None:
            self._default_ob = _DefaultRecorderOb(self)
            self._default_ob.__disown__()
            self._observers.append(self._default_ob)
            _pw.addMuxerOb(self._native, self._default_ob)

    def onStateChange(self, callback):
        """注册状态变化回调: onStateChange(preState, state)"""
        return self._on('onStateChange', callback)

    def onProgress(self, callback):
        """注册进度回调: onProgress(RecorderProgress)"""
        return self._on('onProgress', callback)

    def onIoError(self, callback):
        """注册 IO 错误回调: onIoError(error, msg)"""
        return self._on('onIoError', callback)

    def onEncodeError(self, callback):
        """注册编码错误回调: onEncodeError(trackType, error)"""
        return self._on('onEncodeError', callback)

    def onComplete(self, callback):
        """注册完成回调: onComplete()"""
        return self._on('onComplete', callback)

    # ── 直接代理 ──

    def setHardEncode(self, hard):
        """设置是否硬编码。hard=True 使用硬件编码。"""
        return self._native.setHardEncode(hard)

    def setVideoCodec(self, codecId):
        """设置视频编码。codecId 为 VCodecId 枚举 (h264/h265)。"""
        return self._native.setVideoCodec(codecId)

    def setAudioCodec(self, codecId):
        """设置音频编码。codecId 为 ACodecId 枚举 (aac/opus/g711a/...)。"""
        return self._native.setAudioCodec(codecId)

    def setMuxerType(self, type):
        """设置封装类型。type 为 MuxerType 枚举 (ffmpeg/zlmediakit/onvif)。"""
        return self._native.setMuxerType(type)

    def open(self, url):
        """打开输出 URL (文件路径或推流地址如 rtmp://)。"""
        return self._native.open(url)

    def close(self):
        """关闭封装器。"""
        return self._native.close()

    # ── 便利方法 ──

    def setAudioDesc(self, desc):
        """设置音频描述。desc 为 dict: {channels, sampleRate, format}。自动创建 AudioDesc。"""
        ad = _pw.AudioDesc()
        ad.channels = desc.get('channels', 1)
        ad.sampleRate = desc.get('sampleRate', 8000)
        ad.format = desc.get('format', 2)
        self._native.setAudioDesc(ad)

    # ── 属性 ──

    @property
    def state(self):
        return RecorderState(self._native.getState()) if self._native else RecorderState.none

    # ── 生命周期 ──

    def destroy(self):
        self._cleanupObservers()
        self._native = None

# ─── IRecorder ──────────────────────────────────────────

class IRecorder(_CallbackBridge):
    """流录制器封装。

    正规模式: addObserver(IRecorderOb子类) / removeObserver(ob)
    快捷模式: onStateChange(cb) / onProgress(cb) / onIoError(cb) / onEncodeError(cb) / onComplete(cb)
    """

    def __init__(self, native, bTranscode=False):
        super().__init__()
        self._native = native
        self._bTranscode = bTranscode
        self._surfaceRender = None
        self._audioRender = None

    # ── Observer (正规模式) ──

    def _addObserverImpl(self, ob):
        _pw.addRecorderOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeRecorderOb(self._native, ob)

    # ── Observer (快捷模式) ──

    def _ensureDefaultObserver(self):
        if self._default_ob is None:
            self._default_ob = _DefaultRecorderOb(self)
            self._default_ob.__disown__()
            self._observers.append(self._default_ob)
            _pw.addRecorderOb(self._native, self._default_ob)

    def onStateChange(self, callback):
        """注册状态变化回调: onStateChange(preState, state)"""
        return self._on('onStateChange', callback)

    def onProgress(self, callback):
        """注册进度回调: onProgress(RecorderProgress)"""
        return self._on('onProgress', callback)

    def onIoError(self, callback):
        """注册 IO 错误回调: onIoError(error, msg)"""
        return self._on('onIoError', callback)

    def onEncodeError(self, callback):
        """注册编码错误回调: onEncodeError(trackType, error)"""
        return self._on('onEncodeError', callback)

    def onComplete(self, callback):
        """注册完成回调: onComplete()"""
        return self._on('onComplete', callback)

    # ── 直接代理 ──

    def setIoPlan(self, ioPlan):
        """设置 IO 方案。ioPlan 为 IoPlan 枚举, 默认 ffmpeg。"""
        return self._native.setIoPlan(ioPlan)

    def setMuxerType(self, type):
        """设置封装类型。type 为 MuxerType 枚举, 默认 ffmpeg。"""
        return self._native.setMuxerType(type)

    def setVideoCodec(self, codecId):
        """设置视频编码, codecId 为 VCodecId 枚举 (h264/h265), 设 none 丢弃视频轨。"""
        return self._native.setVideoCodec(codecId)

    def setAudioCodec(self, codecId):
        """设置音频编码, codecId 为 ACodecId 枚举, 设 none 丢弃音频轨。"""
        return self._native.setAudioCodec(codecId)

    def open(self, inputUrl, outputFile):
        """打开录制。inputUrl 输入流地址, outputFile 输出文件路径。"""
        return self._native.open(inputUrl, outputFile)

    def close(self):
        """关闭录制器。拉流结束或 IO 错误也会自动关闭。"""
        return self._native.close()

    def getSurfaceRender(self):
        """获取 SurfaceRender (仅 bTranscode=True 时可用)。"""
        if not self._bTranscode:
            return None
        if self._surfaceRender is None:
            nativeSr = self._native.getSurfaceRender()
            if nativeSr:
                self._surfaceRender = ISurfaceRender(nativeSr)
        return self._surfaceRender

    def getAudioRender(self):
        """获取 AudioRender (用于 AudioTap 读取音频数据)。
        TranscodeRecorder 返回裸 AudioRender (无设备输出, 可 openTap);
        StreamRecorder 返回 None。"""
        if self._audioRender is None:
            nativeAr = self._native.getAudioRender()
            if nativeAr:
                self._audioRender = IAudioRender(nativeAr)
        return self._audioRender

    def seek(self, posMs):
        """Seek 到相对位置(毫秒), 内部自动加 basetime。未 recording 或源不可 seek 返回 False。"""
        return self._native.seek(posMs) if self._native else False

    def getSourceInfo(self):
        """获取源信息 (track 描述/canSeek), 返回 dict; recording 前返回 None。"""
        info = self._native.getSourceInfo() if self._native else None
        return _sourceInfoToDict(info)

    # ── 属性 ──

    @property
    def state(self):
        return RecorderState(self._native.getState()) if self._native else RecorderState.none

    @property
    def duration(self):
        """源总时长(毫秒), <=0 表示直播/未知(不可 seek); recording 后有效。"""
        return self._native.getDuration() if self._native else 0

    # ── 生命周期 ──

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
        return False

    def destroy(self):
        self._cleanupObservers()
        if self._native:
            self._native.close()
        self._native = None
        self._surfaceRender = None
        self._audioRender = None

# ─── 模块级工厂函数 ─────────────────────────────────────

def createRecorder(bTranscode=False):
    """创建 IRecorder 封装实例。bTranscode=True 时先解码再编码, 支持 getSurfaceRender() 图像处理 (水印/LUT等); False 时直接封装不转码。"""
    native = _pw.createRecorder(bTranscode)
    return IRecorder(native, bTranscode) if native else None
