# avox SDK Python 高层封装 — 内部 DefaultObserver 子类
# SWIG Director 子类, 将 C++ 回调桥接到 _CallbackBridge._emit

import AvoxWrapper as _pw
from avox._core import _CallbackBridge

# ─── _DefaultPlayerOb ───────────────────────────────────
# IMediaPlayerOb → onStateChange/onIoError/onDecodeError/onReady/onComplete/onSeek/onPause/onResume/onClose

class _DefaultPlayerOb(_pw.IMediaPlayerOb):
    def __init__(self, owner):
        _pw.IMediaPlayerOb.__init__(self)
        self._owner = owner
    def onStateChange(self, preState, state):
        self._owner._emit('onStateChange', preState, state)
    def onIoError(self, error, msg):
        self._owner._emit('onIoError', error, msg)
    def onDecodeError(self, trackType, error):
        self._owner._emit('onDecodeError', trackType, error)
    def onReady(self):
        self._owner._emit('onReady')
    def onComplete(self):
        self._owner._emit('onComplete')
    def onSeek(self):
        self._owner._emit('onSeek')
    def onPause(self):
        self._owner._emit('onPause')
    def onResume(self):
        self._owner._emit('onResume')
    def onClose(self):
        self._owner._emit('onClose')

# ─── _DefaultRecorderOb ─────────────────────────────────
# IRecorderOb → onStateChange/onProgress/onIoError/onEncodeError/onComplete

class _DefaultRecorderOb(_pw.IRecorderOb):
    def __init__(self, owner):
        _pw.IRecorderOb.__init__(self)
        self._owner = owner
    def onStateChange(self, preState, state):
        self._owner._emit('onStateChange', preState, state)
    def onProgress(self, progress):
        self._owner._emit('onProgress', progress)
    def onIoError(self, error, msg):
        self._owner._emit('onIoError', error, msg)
    def onEncodeError(self, trackType, error):
        self._owner._emit('onEncodeError', trackType, error)
    def onComplete(self):
        self._owner._emit('onComplete')

# ─── _DefaultRtcEventOb ─────────────────────────────────
# IRtcEventOb → onConnectionState/onFirstVideoFrame/onDataChannelMsg/onLocalSdp/onIceCandidate

class _DefaultRtcEventOb(_pw.IRtcEventOb):
    def __init__(self, owner):
        _pw.IRtcEventOb.__init__(self)
        self._owner = owner
    def onConnectionState(self, state):
        self._owner._emit('onConnectionState', state)
    def onFirstVideoFrame(self):
        self._owner._emit('onFirstVideoFrame')
    def onDataChannelMsg(self, data, size):
        self._owner._emit('onDataChannelMsg', data, size)
    def onLocalSdp(self, localSdp):
        self._owner._emit('onLocalSdp', localSdp)
    def onIceCandidate(self, candidate, mid, mlineIndex):
        self._owner._emit('onIceCandidate', candidate, mid, mlineIndex)

# ─── _DefaultSurfaceRenderOb ────────────────────────────
# ISurfaceRenderOb → onSurface/onWinSizeChange/onFrame/onRender

class _DefaultSurfaceRenderOb(_pw.ISurfaceRenderOb):
    def __init__(self, owner):
        _pw.ISurfaceRenderOb.__init__(self)
        self._owner = owner
    def onSurface(self):
        self._owner._emit('onSurface')
    def onWinSizeChange(self, width, height):
        self._owner._emit('onWinSizeChange', width, height)
    def onFrame(self, frame):
        self._owner._emit('onFrame', frame)
    def onRender(self):
        self._owner._emit('onRender')

# ─── _DefaultAudioTapOb ──────────────────────────────────
# IAudioTapOb → onAudioDesc/onFrame

class _DefaultAudioTapOb(_pw.IAudioTapOb):
    def __init__(self, owner):
        _pw.IAudioTapOb.__init__(self)
        self._owner = owner
    def onAudioDesc(self, desc):
        self._owner._emit('onAudioDesc', desc)
    def onFrame(self, raw, pts):
        self._owner._emit('onFrame', raw, pts)
