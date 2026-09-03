# avox SDK Python 高层封装 — Audio 模块
# IAudioRender, IWavSave, IAudioStt

import AvoxWrapper as _pw
from avox._core import _CallbackBridge, AudioFormat
from avox._observer import _DefaultAudioTapOb

# ─── IAudioRender ───────────────────────────────────────

class IAudioRender(_CallbackBridge):
    """音频渲染封装。支持音量控制、回声消除、AudioTap(读取播放音频帧)。

    AudioTap 快捷模式: onAudioDesc(cb) / onFrame(cb)
    正规模式: addObserver(IAudioTapOb子类) / removeObserver(ob)
    """

    def __init__(self, native):
        super().__init__()
        self._native = native

    # ── Observer (正规模式) ──

    def _addObserverImpl(self, ob):
        _pw.addAudioTapOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeAudioTapOb(self._native, ob)

    # ── Observer (快捷模式) ──

    def _ensureDefaultObserver(self):
        if self._default_ob is None:
            self._default_ob = _DefaultAudioTapOb(self)
            self._default_ob.__disown__()
            self._observers.append(self._default_ob)
            _pw.addAudioTapOb(self._native, self._default_ob)

    def onAudioDesc(self, callback):
        """注册音频格式就绪回调: onAudioDesc(AudioDesc)。tap 打开后, 源格式就绪时触发一次。"""
        return self._on('onAudioDesc', callback)

    def onFrame(self, callback):
        """注册音频帧回调: onFrame(AvoxData, pts)。tap 打开后, 定长音频块就绪时持续触发。
        帧仅在回调内有效, 需延后处理请自行复制数据。"""
        return self._on('onFrame', callback)

    # ── 直接代理 ──

    def setVolume(self, volume):
        """设置音量。volume 范围 0.0~1.0。"""
        return self._native.setVolume(volume)

    def getVolume(self):
        """获取当前音量 (0.0~1.0)。"""
        return self._native.getVolume()

    def enableAec(self, aec):
        """启用回声消除。aec 为 AudioAec dict: {mobileMode}。"""
        return self._native.enableAec(aec)

    def disableAec(self):
        """禁用回声消除。"""
        return self._native.disableAec()

    # ── AudioTap ──

    def openTap(self, outDesc=None, frameMs=40):
        """打开音频 tap, 从渲染管线备份音频帧供外部读取。

        outDesc: dict {channels, sampleRate, format} 或 None。
            None/空 dict → 跟随源格式不重采样。
            有值 → 按指定格式重采样/切片。
        frameMs: 每帧时长(毫秒), 默认 40ms。

        打开后需注册 onAudioDesc + onFrame 回调接收数据。
        """
        if outDesc:
            desc = _pw.AudioDesc()
            desc.channels = outDesc.get('channels', 0)
            desc.sampleRate = outDesc.get('sampleRate', 0)
            fmt = outDesc.get('format', -1)
            desc.format = int(fmt) if isinstance(fmt, AudioFormat) else fmt
            self._native.openTap(desc, frameMs)
        else:
            desc = _pw.AudioDesc()
            self._native.openTap(desc, frameMs)

    def closeTap(self):
        """关闭音频 tap, 停止备份与回调。"""
        self._native.closeTap()

    # ── 便利: 录音到 WAV ──

    def startWavRecord(self, path, outDesc=None, frameMs=40):
        """打开 tap + 创建 IWavSave, 自动将音频帧写入 WAV 文件。

        path: WAV 文件保存路径。
        outDesc: 同 openTap, None 跟随源格式。
        frameMs: 同 openTap, 默认 40ms。

        返回 IWavSave 实例, 调用 stopWavRecord() 或 ws.close() 结束录音。
        """
        ws = IWavSave()
        ws._openUrl(path)  # 先打开文件, 避免回调时文件未就绪丢帧
        # 注册回调: onAudioDesc 自动设格式, onFrame 自动写帧
        self.onAudioDesc(lambda desc: ws._onAudioDesc(desc))
        self.onFrame(lambda raw, pts: ws._onFrame(raw, pts))
        self.openTap(outDesc, frameMs)
        self._wavSave = ws
        return ws

    def stopWavRecord(self):
        """停止录音: 关闭 IWavSave + 关闭 tap。"""
        if hasattr(self, '_wavSave') and self._wavSave:
            self._wavSave.close()
            self._wavSave = None
        self.closeTap()

    # ── 生命周期 ──

    def destroy(self):
        """清理 observer 和缓存。注意: IAudioRender 生命周期由 Player 管理, 通常不需要手动 destroy。"""
        self._cleanupObservers()
        if hasattr(self, '_wavSave') and self._wavSave:
            self._wavSave.close()
            self._wavSave = None

# ─── IWavSave ───────────────────────────────────────────

class IWavSave:
    """WAV 文件保存器。将 PCM 音频数据写入可播放的 WAV 文件。

    planar 格式自动转 interleaved; WAV 头先占位, close 时回填文件大小。
    可独立使用, 也可配合 IAudioRender.startWavRecord() 自动录音。
    """

    def __init__(self, native=None):
        self._native = native or _pw.createWavSave()
        self._desc = None

    def setAudioDesc(self, desc):
        """设置音频格式。desc 为 dict: {channels, sampleRate, format}。
        format 可传 AudioFormat 枚举或 int。addFrame 的数据应与此格式一致。"""
        ad = _pw.AudioDesc()
        ad.channels = desc.get('channels', 1)
        ad.sampleRate = desc.get('sampleRate', 44100)
        fmt = desc.get('format', AudioFormat.s16)
        ad.format = int(fmt) if isinstance(fmt, AudioFormat) else fmt
        self._desc = ad
        return self._native.setAudioDesc(ad)

    def openUrl(self, path):
        """打开 WAV 文件用于写入。"""
        return self._native.openUrl(path)

    def addFrame(self, raw):
        """追加一帧 PCM 数据。raw 为 AvoxData (SWIG 原生对象)。
        planar 格式会自动转 interleaved。"""
        return self._native.addFrame(raw)

    def close(self):
        """关闭文件并回填 WAV 头中的文件大小。"""
        self._native.close()

    # ── 内部: 配合 IAudioRender.startWavRecord 使用 ──

    def _onAudioDesc(self, desc):
        """AudioTap onAudioDesc 回调: 自动设置格式。"""
        if desc:
            self._desc = desc
            self._native.setAudioDesc(desc)

    def _onFrame(self, raw, pts):
        """AudioTap onFrame 回调: 自动追加帧数据。"""
        if raw:
            self._native.addFrame(raw)

    def _openUrl(self, path):
        """内部打开, startWavRecord 使用。"""
        self._native.openUrl(path)

# ─── IAudioStt ──────────────────────────────────────────

class IAudioSttOb:
    """语音识别回调基类。子类化后通过 IAudioStt.addObserver() 注册。

    快捷模式: IAudioStt.onResult(cb) / onPartialResult(cb) / onEndpoint(cb)
    """

    def __init__(self, native):
        self._native = native

class IAudioStt:
    """语音识别器封装。支持流式/离线识别, 通过 Observer 接收结果。"""

    def __init__(self, native):
        self._native = native

    def setAudioDesc(self, desc):
        """设置音频格式。desc 为 dict: {channels, sampleRate, format}。"""
        ad = _pw.AudioDesc()
        ad.channels = desc.get('channels', 1)
        ad.sampleRate = desc.get('sampleRate', 16000)
        fmt = desc.get('format', AudioFormat.flt)
        ad.format = int(fmt) if isinstance(fmt, AudioFormat) else fmt
        return self._native.setAudioDesc(ad)

    def start(self):
        """开始识别任务 (内部按需加载/复用模型)。"""
        return self._native.start()

    def recognize(self, adata, pts):
        """输入一帧音频数据。adata 为 AvoxData, pts 为时间戳。"""
        return self._native.recognize(adata, pts)

    def stop(self):
        """停止识别任务 (join 排空剩余音频, 模型常驻)。"""
        return self._native.stop()

    def loading(self):
        """是否正在运行。"""
        return self._native.loading()

    def setModelLevel(self, level):
        """设置模型精度等级。level: 'mini' / 'base' / 'high'。"""
        return self._native.setModelLevel(level)

    def setRecognizerType(self, rtype):
        """设置识别器类型。rtype: 'streaming' / 'offline'。"""
        return self._native.setRecognizerType(rtype)

    def getRecognizerType(self):
        """获取当前识别器类型。"""
        return self._native.getRecognizerType()

    def addObserver(self, ob):
        """注册 IAudioSttOb 子类实例。"""
        ob.__disown__()
        _pw.addAudioSttOb(self._native, ob)
        return self

    def removeObserver(self, ob):
        """移除 IAudioSttOb 子类实例。"""
        _pw.removeAudioSttOb(self._native, ob)
        return self

# ─── 模块级工厂函数 ─────────────────────────────────────

def createWavSave():
    """创建 IWavSave 封装实例。可将 PCM 音频数据写入可播放的 WAV 文件。"""
    return IWavSave()
