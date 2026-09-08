# avox SDK Python 高层封装 — Player 模块
# BasePlayer, IMediaPlayer, ISourcePlayer, IRtcPlayer

import AvoxWrapper as _pw
from avox._core import _CallbackBridge, PlayerState, IoPlan, _sourceInfoToDict
from avox._observer import _DefaultPlayerOb, _DefaultRtcEventOb
from avox.video import ISurfaceRender
from avox.audio import IAudioRender
from avox.muxer import IMediaMuxer
from avox.common import IOption as _IOption

# ─── BasePlayer ─────────────────────────────────────────

class BasePlayer(_CallbackBridge):
    """播放器基类, 提供 observer 管理、状态属性、生命周期。

    正规模式: addObserver(IMediaPlayerOb子类) / removeObserver(ob)
    快捷模式: onStateChange(cb) / onReady(cb) / onComplete(cb) / ...
    """

    def __init__(self):
        super().__init__()
        self._native = None
        self._surfaceRender = None
        self._audioRender = None
        self._muxer = None
        self._option = None
        self._closed = False

    # ── Observer (正规模式) — 子类覆写 _addObserverImpl/_removeObserverImpl ──

    # ── Observer (快捷模式) ──

    def _ensureDefaultObserver(self):
        if self._default_ob is None:
            self._default_ob = _DefaultPlayerOb(self)
            self._default_ob.__disown__()
            self._observers.append(self._default_ob)
            self._addObserverImpl(self._default_ob)

    def onStateChange(self, callback):
        """注册状态变化回调: onStateChange(preState, state)"""
        return self._on('onStateChange', callback)

    def onIoError(self, callback):
        """注册 IO 错误回调: onIoError(error, msg)"""
        return self._on('onIoError', callback)

    def onDecodeError(self, callback):
        """注册解码错误回调: onDecodeError(trackType, error)"""
        return self._on('onDecodeError', callback)

    def onReady(self, callback):
        """注册就绪回调: onReady()"""
        return self._on('onReady', callback)

    def onComplete(self, callback):
        """注册播放完成回调: onComplete()"""
        return self._on('onComplete', callback)

    def onSeek(self, callback):
        """注册 seek 完成回调: onSeek()"""
        return self._on('onSeek', callback)

    def onPause(self, callback):
        """注册暂停回调: onPause()"""
        return self._on('onPause', callback)

    def onResume(self, callback):
        """注册恢复回调: onResume()"""
        return self._on('onResume', callback)

    def onClose(self, callback):
        """注册关闭回调: onClose()"""
        return self._on('onClose', callback)

    # ── 属性 ──

    @property
    def state(self):
        """当前 PlayerState"""
        if self._native:
            return PlayerState(self._native.getState())
        return PlayerState.none

    @property
    def isPlaying(self):
        return self.state == PlayerState.playing

    @property
    def isPaused(self):
        return self.state == PlayerState.pause

    @property
    def isStopped(self):
        return self.state in (PlayerState.stopped, PlayerState.none)

    # ── 生命周期 ──

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
        return False

    def close(self):
        """关闭播放器"""
        if self._native and not self._closed:
            self._native.close()

    def destroy(self):
        """销毁播放器: 移除 observer → 关闭 → 释放引用"""
        if self._closed:
            return
        self._cleanupObservers()
        if self._native:
            self._native.close()
        self._native = None
        self._surfaceRender = None
        self._audioRender = None
        self._muxer = None
        self._option = None
        self._closed = True

# ─── IMediaPlayer ───────────────────────────────────────

class IMediaPlayer(BasePlayer):
    """媒体播放器封装 (URL 播放, 支持直播/本地)。"""

    def __init__(self):
        super().__init__()
        self._native = _pw.createMediaPlayer()

    def _addObserverImpl(self, ob):
        _pw.addMediaPlayerOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeMediaPlayerOb(self._native, ob)

    # ── 直接代理 ──

    def open(self, url):
        """打开媒体 URL (支持直播/本地文件/RTSP/RTMP 等)。"""
        return self._native.open(url)

    def close(self):
        """关闭播放器。"""
        return self._native.close()

    def seek(self, pos):
        """跳转到指定位置。pos 为毫秒。"""
        return self._native.seek(pos)

    def pause(self):
        """暂停播放。"""
        return self._native.pause()

    def resume(self):
        """恢复播放。"""
        return self._native.resume()

    def speed(self, speed):
        """设置播放速度。1.0=正常, 2.0=两倍速。"""
        return self._native.speed(speed)

    def setHardDecode(self, hard):
        """设置是否硬解。hard=True 使用 DX11/MediaCodec/VideoToolbox 硬件解码。"""
        return self._native.setHardDecode(hard)

    def setIoPlan(self, plan):
        """设置 IO 方案。plan 为 IoPlan 枚举 (ffmpeg/zlmediakit), 下次 open 生效。"""
        return self._native.setIoPlan(plan)

    def getOption(self):
        """获取内置参数设置器, 返回 IOption 封装 (支持语义化 setter / dict 风格访问)。"""
        if self._option is None:
            nativeOpt = self._native.getOption()
            if nativeOpt:
                self._option = _IOption(nativeOpt)
        return self._option

    def getSubtitle(self):
        """获取字幕控制器 (原生 ISubtitle)。"""
        return self._native.getSubtitle()

    def getRate(self, type, bAvg):
        """获取码率 (Kb/s)。type 为 TrackType, bAvg=True 返回平均码率, False 返回实时码率。"""
        return self._native.getRate(type, bAvg)

    def getLossRate(self, type):
        """获取丢包率 (0.0~1.0, 仅 RTSP/RTP 等 UDP 协议有效)。type 为 TrackType。"""
        return self._native.getLossRate(type)

    # ── 子对象 (返回封装) ──

    def getSurfaceRender(self):
        if self._surfaceRender is None:
            nativeSr = self._native.getSurfaceRender()
            if nativeSr:
                self._surfaceRender = ISurfaceRender(nativeSr)
        return self._surfaceRender

    def getAudioRender(self):
        if self._audioRender is None:
            nativeAr = self._native.getAudioRender()
            if nativeAr:
                self._audioRender = IAudioRender(nativeAr)
        return self._audioRender

    def getMuxer(self, bTranscode=False):
        if self._muxer is None:
            nativeMuxer = self._native.getMuxer(bTranscode)
            if nativeMuxer:
                self._muxer = IMediaMuxer(nativeMuxer)
        return self._muxer

    def getSourceInfo(self):
        """获取源信息, 返回 dict (而非 SWIG ISourceInfo)。"""
        info = self._native.getSourceInfo()
        return _sourceInfoToDict(info)

    # ── 属性 ──

    @property
    def duration(self):
        """时长 (ms), 直播 <=0"""
        return self._native.getDuration() if self._native else 0

    @property
    def position(self):
        """当前渲染 PTS (ms)"""
        return self._native.getPosition() if self._native else 0

    @property
    def startTime(self):
        """起始 PTS (ms), 有跳变时可能从跳变 PTS 开始。"""
        return self._native.getStartTime() if self._native else 0

    @property
    def process(self):
        """播放进度 0-1"""
        return self._native.getProcess() if self._native else 0.0

    @property
    def fps(self):
        """实时帧率"""
        return self._native.getFps() if self._native else 0.0

# ─── ISourcePlayer ──────────────────────────────────────

class ISourcePlayer(BasePlayer):
    """设备/原始源播放器封装 (无解码队列, 无 A/V 同步)。"""

    def __init__(self):
        super().__init__()
        self._native = _pw.createDevicePlayer()

    def _addObserverImpl(self, ob):
        _pw.addSourcePlayerOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeSourcePlayerOb(self._native, ob)

    # ── 直接代理 ──

    def open(self):
        """打开设备源播放。"""
        return self._native.open()

    def close(self):
        """关闭设备源播放器。"""
        return self._native.close()

    def setAudioSource(self, source):
        """设置音频设备源 (原生 IAudioSource)。"""
        return self._native.setAudioSource(source)

    def setVideoSource(self, source):
        """设置视频设备源 (原生 IVideoSource)。"""
        return self._native.setVideoSource(source)

    def getSubtitle(self):
        """获取字幕控制器 (仅支持流式识别, 翻译无效)。"""
        return self._native.getSubtitle()

    # ── 子对象 (返回封装) ──

    def getSurfaceRender(self):
        if self._surfaceRender is None:
            nativeSr = self._native.getSurfaceRender()
            if nativeSr:
                self._surfaceRender = ISurfaceRender(nativeSr)
        return self._surfaceRender

    def getAudioRender(self):
        if self._audioRender is None:
            nativeAr = self._native.getAudioRender()
            if nativeAr:
                self._audioRender = IAudioRender(nativeAr)
        return self._audioRender

    def getMuxer(self):
        if self._muxer is None:
            nativeMuxer = self._native.getMuxer()
            if nativeMuxer:
                self._muxer = IMediaMuxer(nativeMuxer)
        return self._muxer

    def getSourceInfo(self):
        info = self._native.getSourceInfo()
        return _sourceInfoToDict(info)

# ─── IRtcPlayer ─────────────────────────────────────────

class IRtcPlayer(BasePlayer):
    """WebRTC 播放器封装。

    额外 Observer: IRtcEventOb (onConnectionState/onFirstVideoFrame/onDataChannelMsg/onLocalSdp/onIceCandidate)
    快捷: onLocalSdp(cb) / onIceCandidate(cb)
    """

    def __init__(self):
        super().__init__()
        self._native = _pw.createWebRtcPlayer()
        self._rtcOb = None
        self._remoteSurfaceRender = None
        self._localSurfaceRender = None
        self._remoteAudioRender = None
        self._localAudioRender = None

    def _addObserverImpl(self, ob):
        _pw.addRtcPlayerOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeRtcPlayerOb(self._native, ob)

    # ── SDP Observer (快捷模式) ──

    def _ensureRtcEventOb(self):
        if self._rtcOb is None:
            self._rtcOb = _DefaultRtcEventOb(self)
            self._rtcOb.__disown__()
            self._observers.append(self._rtcOb)
            self._native.addOb(self._rtcOb)

    def onLocalSdp(self, callback):
        """注册本地 SDP 回调: onLocalSdp(localSdp)"""
        self._ensureRtcEventOb()
        return self._on('onLocalSdp', callback)

    def onIceCandidate(self, callback):
        """注册 ICE 候选回调: onIceCandidate(candidate, mid, mlineIndex)"""
        self._ensureRtcEventOb()
        return self._on('onIceCandidate', callback)

    def onConnectionState(self, callback):
        """注册连接状态回调: onConnectionState(state)"""
        self._ensureRtcEventOb()
        return self._on('onConnectionState', callback)

    # ── 直接代理 ──

    def open(self):
        """打开 WebRTC 播放器。"""
        return self._native.open()

    def close(self):
        """关闭 WebRTC 播放器。"""
        return self._native.close()

    def setRollType(self, type):
        """设置滚动类型。"""
        return self._native.setRollType(type)

    def addIceServer(self, uri, username, password):
        """添加 ICE 服务器。uri 如 'stun:xxx' / 'turn:xxx', username/password 为 TURN 凭证。"""
        return self._native.addIceServer(uri, username, password)

    def setVideoSource(self, videoSource):
        """设置本地视频源 (原生 IVideoSource)。"""
        return self._native.setVideoSource(videoSource)

    def setAudioSource(self, audioSource):
        """设置本地音频源 (原生 IAudioSource)。"""
        return self._native.setAudioSource(audioSource)

    def getLocalSdp(self):
        """获取本地 SDP (协商后可用)。"""
        return self._native.getLocalSdp()

    def setRemoteSdp(self, sdp):
        """设置远端 SDP。"""
        return self._native.setRemoteSdp(sdp)

    def addIceCandidate(self, candidate, mid, mlineIndex):
        """添加远端 ICE 候选。candidate 候选字符串, mid media ID, mlineIndex m-line 索引。"""
        return self._native.addIceCandidate(candidate, mid, mlineIndex)

    # ── 子对象 (返回封装) ──

    def getRemoteSurfaceRender(self):
        if self._remoteSurfaceRender is None:
            nativeSr = self._native.getRemoteSurfaceRender()
            if nativeSr:
                self._remoteSurfaceRender = ISurfaceRender(nativeSr)
        return self._remoteSurfaceRender

    def getLocalSurfaceRender(self):
        if self._localSurfaceRender is None:
            nativeSr = self._native.getLocalSurfaceRender()
            if nativeSr:
                self._localSurfaceRender = ISurfaceRender(nativeSr)
        return self._localSurfaceRender

    def getRemoteAudioRender(self):
        if self._remoteAudioRender is None:
            nativeAr = self._native.getRemoteAudioRender()
            if nativeAr:
                self._remoteAudioRender = IAudioRender(nativeAr)
        return self._remoteAudioRender

    def getLocalAudioRender(self):
        if self._localAudioRender is None:
            nativeAr = self._native.getLocalAudioRender()
            if nativeAr:
                self._localAudioRender = IAudioRender(nativeAr)
        return self._localAudioRender

    def getRemoteSourceInfo(self):
        info = self._native.getRemoteSourceInfo()
        return _sourceInfoToDict(info)

    def getLocalSourceInfo(self):
        info = self._native.getLocalSourceInfo()
        return _sourceInfoToDict(info)

    # ── 生命周期 ──

    def destroy(self):
        if self._closed:
            return
        self._cleanupObservers()
        self._rtcOb = None
        if self._native:
            self._native.close()
        self._native = None
        self._remoteSurfaceRender = None
        self._localSurfaceRender = None
        self._remoteAudioRender = None
        self._localAudioRender = None
        self._closed = True

# ─── 模块级工厂函数 ─────────────────────────────────────

def createMediaPlayer():
    """创建 IMediaPlayer 封装实例。"""
    return IMediaPlayer()

def createDevicePlayer():
    """创建 ISourcePlayer 封装实例。"""
    return ISourcePlayer()

def createWebRtcPlayer():
    """创建 IRtcPlayer 封装实例。"""
    return IRtcPlayer()

def createZlTestSdpAgent(player, serverUrl):
    """创建测试用 SDP Agent。"""
    if player._native:
        return _pw.createZlTestSdpAgent(player._native, serverUrl)
    return None
