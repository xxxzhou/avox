# avox SDK Python 高层封装 — 内部核心模块
# 导入 AvoxWrapper (SWIG 原始绑定), 定义 _CallbackBridge, 枚举, 异常

import AvoxWrapper as _pw

# ─── buffer ↔ bytes 工具 ────────────────────────────────

def buffer_to_bytes(buf):
    """将 SWIG AvoxData / IImageBuffer 提取为 Python bytes。

    兼容: AvoxData(.data + .size), IImageBuffer(getPointer() + getBufferSize())
    """
    import ctypes
    try:
        ptr = int(buf.getPointer())
        size = buf.getBufferSize()
    except AttributeError:
        ptr = int(buf.data)
        size = buf.size
    return ctypes.string_at(ptr, size) if ptr and size > 0 else b""


def bytes_to_buffer(buf, data):
    """将 Python bytes 写入 SWIG IImageBuffer / AvoxData。

    buf: IImageBuffer(getPointer()+getBufferSize()) 或 AvoxData(.data+.size)
    data: Python bytes, 长度需匹配 buf size
    """
    import ctypes
    try:
        ptr = int(buf.getPointer())
        size = buf.getBufferSize()
    except AttributeError:
        ptr = int(buf.data)
        size = buf.size
    if size != len(data):
        return False
    if ptr and size > 0:
        ctypes.memmove(ptr, data, size)
    return True

# ─── 异常 ───────────────────────────────────────────────

class AvoxError(Exception):
    """avox SDK 基础异常"""
    pass

class AvoxIOError(AvoxError):
    """IO 错误"""
    pass

class AvoxStateError(AvoxError):
    """状态错误 (如在错误状态下调用方法)"""
    pass

# ─── _CallbackBridge ────────────────────────────────────
# 为 onXxx(cb) 快捷模式提供回调分发基础设施

class _CallbackBridge:
    """回调桥接基类, 提供 onXxx(cb)/offXxx(cb) 快捷注册 + _emit 分发。

    子类需定义 _EVENT_MAP: dict[str, str] — 事件名 → 对应的 onXxx 方法名,
    以及 _observer_cls / _add_ob_func / _remove_ob_func 等。
    """

    def __init__(self):
        self._callbacks = {}       # event_name → [callable, ...]
        self._observers = []       # 强引用持有, 防 GC (含 __disown__ 的 observer)
        self._default_ob = None    # onXxx 快捷模式自动创建的 DefaultObserver

    # ── onXxx(cb) / offXxx(cb) 快捷方法 ──
    # 由元类或 __init_subclass__ 动态生成, 这里只提供 _emit

    def _emit(self, event, *args):
        """触发事件, 遍历回调 (异常隔离)"""
        for cb in self._callbacks.get(event, []):
            try:
                cb(*args)
            except Exception:
                import traceback
                traceback.print_exc()

    def _on(self, event, callback):
        """注册回调 (内部)"""
        if not callable(callback):
            raise TypeError(f"callback must be callable, got {type(callback)}")
        if event not in self._callbacks:
            self._callbacks[event] = []
        self._callbacks[event].append(callback)
        # 惰性创建 DefaultObserver
        self._ensureDefaultObserver()
        return self

    def _off(self, event, callback=None):
        """移除回调 (内部)"""
        if event not in self._callbacks:
            return self
        if callback is None:
            del self._callbacks[event]
        else:
            self._callbacks[event] = [cb for cb in self._callbacks[event] if cb is not callback]
        return self

    def _ensureDefaultObserver(self):
        """子类覆写: 惰性创建并注册 DefaultObserver"""
        pass

    # ── addObserver / removeObserver (正规模式) ──

    def addObserver(self, ob):
        """注册 IxxxOb 子类实例 (正规模式)。
        自动 __disown__() + addXxxOb(), 并持有强引用防 GC。
        子类需覆写 _addObserverImpl。
        """
        ob.__disown__()
        self._observers.append(ob)
        self._addObserverImpl(ob)
        return self

    def removeObserver(self, ob):
        """移除 IxxxOb 子类实例。子类需覆写 _removeObserverImpl。"""
        self._removeObserverImpl(ob)
        if ob in self._observers:
            self._observers.remove(ob)
        return self

    def _addObserverImpl(self, ob):
        raise NotImplementedError

    def _removeObserverImpl(self, ob):
        raise NotImplementedError

    # ── 生命周期 ──

    def _cleanupObservers(self):
        """清理所有 observer (destroy 时调用)"""
        # 清理 DefaultObserver
        if self._default_ob is not None:
            try:
                self._removeObserverImpl(self._default_ob)
            except Exception:
                pass
            self._default_ob = None
        # 清理用户注册的 observer
        for ob in self._observers[:]:
            try:
                self._removeObserverImpl(ob)
            except Exception:
                pass
        self._observers.clear()
        self._callbacks.clear()

# ─── 枚举 (从 SWIG 常量创建 IntEnum) ───────────────────

from enum import IntEnum

class PlayerState(IntEnum):
    none = _pw.PlayerState_none
    opening = _pw.PlayerState_opening
    ready = _pw.PlayerState_ready
    playing = _pw.PlayerState_playing
    pause = _pw.PlayerState_pause
    seek = _pw.PlayerState_seek
    buffering = _pw.PlayerState_buffering
    stopped = _pw.PlayerState_stopped
    completed = _pw.PlayerState_completed

class IoPlan(IntEnum):
    none = _pw.IoPlan_none
    zlmediakit = _pw.IoPlan_zlmediakit
    ffmpeg = _pw.IoPlan_ffmpeg

class TrackType(IntEnum):
    none = _pw.TrackType_none
    audio = _pw.TrackType_audio
    video = _pw.TrackType_video
    subtitle = _pw.TrackType_subtitle

class DecodeResult(IntEnum):
    timeout = _pw.DecodeResult_timeout
    noSupport = _pw.DecodeResult_noSupport
    noFind = _pw.DecodeResult_noFind
    openFailed = _pw.DecodeResult_openFailed
    startFailed = _pw.DecodeResult_startFailed
    success = _pw.DecodeResult_success
    noConfig = _pw.DecodeResult_noConfig
    complete = _pw.DecodeResult_complete
    dataNoReady = _pw.DecodeResult_dataNoReady
    dataError = _pw.DecodeResult_dataError

class EncodeResult(IntEnum):
    timeout = _pw.EncodeResult_timeout
    noSupport = _pw.EncodeResult_noSupport
    noFind = _pw.EncodeResult_noFind
    openFailed = _pw.EncodeResult_openFailed
    startFailed = _pw.EncodeResult_startFailed
    success = _pw.EncodeResult_success
    noConfig = _pw.EncodeResult_noConfig
    complete = _pw.EncodeResult_complete
    dataNoReady = _pw.EncodeResult_dataNoReady
    dataError = _pw.EncodeResult_dataError

class ArgType(IntEnum):
    """IOption 值类型 (AvoxBase.h)。"""
    Null = _pw.ArgType_Null
    Boolean = _pw.ArgType_Boolean
    Int = _pw.ArgType_Int
    Number = _pw.ArgType_Number
    String = _pw.ArgType_String
    Array = _pw.ArgType_Array
    Object = _pw.ArgType_Object

class MuxerType(IntEnum):
    none = _pw.MuxerType_none
    ffmpeg = _pw.MuxerType_ffmpeg
    zlmediakit = _pw.MuxerType_zlmediakit
    onvif = _pw.MuxerType_onvif

class RecorderState(IntEnum):
    none = _pw.RecorderState_none
    opening = _pw.RecorderState_opening
    recording = _pw.RecorderState_recording
    completed = _pw.RecorderState_completed

class VCodecId(IntEnum):
    none = _pw.VCodecId_none
    h264 = _pw.VCodecId_h264
    h265 = _pw.VCodecId_h265

class ACodecId(IntEnum):
    none = _pw.ACodecId_none
    aac = _pw.ACodecId_aac
    g711a = _pw.ACodecId_g711a
    g711u = _pw.ACodecId_g711u
    opus = _pw.ACodecId_opus
    pcms16le = _pw.ACodecId_pcms16le
    pcms24le = _pw.ACodecId_pcms24le

class VDeviceKind(IntEnum):
    none = _pw.VDeviceKind_none
    camera = _pw.VDeviceKind_camera
    window = _pw.VDeviceKind_window
    monitor = _pw.VDeviceKind_monitor

class ADeviceKind(IntEnum):
    none = _pw.ADeviceKind_none
    mic = _pw.ADeviceKind_mic
    loopback = _pw.ADeviceKind_loopback

class AudioFormat(IntEnum):
    """音频采样格式 (AvoxAudio.h AVOX_MAP_AUDIO_FMT)。"""
    other = _pw.AudioFormat_other
    u8 = _pw.AudioFormat_AVOX_AUDIO_U8
    s16 = _pw.AudioFormat_AVOX_AUDIO_S16
    s32 = _pw.AudioFormat_AVOX_AUDIO_S32
    s64 = _pw.AudioFormat_AVOX_AUDIO_S64
    flt = _pw.AudioFormat_AVOX_AUDIO_FLT
    dbl = _pw.AudioFormat_AVOX_AUDIO_DBL
    u8p = _pw.AudioFormat_AVOX_AUDIO_U8P
    s16p = _pw.AudioFormat_AVOX_AUDIO_S16P
    s32p = _pw.AudioFormat_AVOX_AUDIO_S32P
    s64p = _pw.AudioFormat_AVOX_AUDIO_S64P
    fltp = _pw.AudioFormat_AVOX_AUDIO_FLTP
    dblp = _pw.AudioFormat_AVOX_AUDIO_DBLP

class MouseButton(IntEnum):
    left = _pw.MouseButton_left
    right = _pw.MouseButton_right
    middle = _pw.MouseButton_middle
    x1 = _pw.MouseButton_x1
    x2 = _pw.MouseButton_x2

# ─── 图像/视频格式枚举 (AvoxVideo.h) ──────────────────────

class ImageType(IntEnum):
    """像素格式 (AvoxVideo.h AVOX_MAP_IMAGE)。"""
    other = _pw.ImageType_other
    r8 = _pw.ImageType_r8
    r16 = _pw.ImageType_r16
    r32 = _pw.ImageType_r32
    rg16f = _pw.ImageType_rg16f
    rgba8 = _pw.ImageType_rgba8
    bgra8 = _pw.ImageType_bgra8
    argb8 = _pw.ImageType_argb8
    rgba16 = _pw.ImageType_rgba16
    rgba32 = _pw.ImageType_rgba32
    r32f = _pw.ImageType_r32f
    rgba32f = _pw.ImageType_rgba32f
    rgb8 = _pw.ImageType_rgb8
    bgr8 = _pw.ImageType_bgr8
    rgb8P = _pw.ImageType_rgb8P
    bgr8P = _pw.ImageType_bgr8P

class YuvType(IntEnum):
    """YUV 格式 (AvoxVideo.h AVOX_MAP_YUV)。"""
    other = _pw.YuvType_other
    gray = _pw.YuvType_gray
    yuv420P = _pw.YuvType_yuv420P
    yuv422P = _pw.YuvType_yuv422P
    yuv444P = _pw.YuvType_yuv444P
    nv12 = _pw.YuvType_nv12
    yuv2I = _pw.YuvType_yuv2I
    yvyuI = _pw.YuvType_yvyuI
    uyvyI = _pw.YuvType_uyvyI
    uyvy422_10B = _pw.YuvType_uyvy422_10B
    yuv420P10 = _pw.YuvType_yuv420P10
    yuyv422A = _pw.YuvType_yuyv422A

class IEncodeType(IntEnum):
    """图像编码格式 (AvoxVideo.h)。"""
    other = _pw.IEncodeType_other
    jpg = _pw.IEncodeType_jpg
    png = _pw.IEncodeType_png
    bmp = _pw.IEncodeType_bmp
    tga = _pw.IEncodeType_tga

# ─── 视觉枚举 (AvoxVision.h) ──────────────────────────────

class TemplateMatchMethod(IntEnum):
    """模板匹配方法 (对应 cv::TemplateMatchModes 的归一化变体)。"""
    none = _pw.TemplateMatchMethod_none
    sqdiffNormed = _pw.TemplateMatchMethod_sqdiffNormed
    ccorrNormed = _pw.TemplateMatchMethod_ccorrNormed
    ccoeffNormed = _pw.TemplateMatchMethod_ccoeffNormed

class MatchOrderBy(IntEnum):
    """模板匹配结果排序方式。"""
    none = _pw.MatchOrderBy_none
    horizontal = _pw.MatchOrderBy_horizontal
    vertical = _pw.MatchOrderBy_vertical
    score = _pw.MatchOrderBy_score
    area = _pw.MatchOrderBy_area

class FeatureMethod(IntEnum):
    """特征匹配描述子方法 (AvoxVision.h)。"""
    sift = _pw.FeatureMethod_sift
    orb = _pw.FeatureMethod_orb
    akaze = _pw.FeatureMethod_akaze

class ColorSpace(IntEnum):
    """颜色区域检测的颜色空间 (AvoxVision.h)。"""
    bgr = _pw.ColorSpace_bgr
    rgb = _pw.ColorSpace_rgb
    hsv = _pw.ColorSpace_hsv

# ─── 语音/翻译/语言 枚举 ─────────────────────────────────

class RecognizerType(IntEnum):
    """文字识别类型 (createTextRecognizer 入参)。streaming=中英(不支持日语), offline=离线。"""
    none = _pw.RecognizerType_none
    streaming = _pw.RecognizerType_streaming
    offline = _pw.RecognizerType_offline

class AudioSttType(IntEnum):
    """语音识别(STT)类型 (createAudioStt 入参)。"""
    none = _pw.AudioSttType_none
    sherpa = _pw.AudioSttType_sherpa

class TranslatorType(IntEnum):
    """翻译类型 (createTranslator 入参)。http=腾讯云, onnx=本地 ja→zh。"""
    none = _pw.TranslatorType_none
    http = _pw.TranslatorType_http
    onnx = _pw.TranslatorType_onnx

class Language(IntEnum):
    """语言 (setTargetLanguage 等)。"""
    none = _pw.Language_none
    zh = _pw.Language_zh
    en = _pw.Language_en
    ja = _pw.Language_ja
    other = _pw.Language_other

# ─── KeyCode: 成员多 (68), 从 SWIG 常量自动构造, 零漂移 ───
# 不手写 68 行赋值; SWIG 新增键时 _swig_enum 自动跟上。用法 KeyCode.enter / KeyCode.ctrl。

def _swig_enum(name, prefix):
    """从 _pw.<prefix>_<member> 裸常量动态构造 IntEnum (适用成员多的 SWIG 枚举)。"""
    members = {}
    for _n in dir(_pw):
        if _n.startswith(prefix + '_'):
            members[_n[len(prefix) + 1:]] = getattr(_pw, _n)
    return IntEnum(name, members)

KeyCode = _swig_enum('KeyCode', 'KeyCode')

# ─── 便利: ISourceInfo → dict ───────────────────────────

def _sourceInfoToDict(info):
    """将 SWIG ISourceInfo 转为 Python dict"""
    if not info:
        return None
    result = {
        'videoSize': info.videoSize(),
        'audioSize': info.audioSize(),
        'canSeek': info.canSeek(),
        'videoTracks': [],
        'audioTracks': [],
    }
    for i in range(info.videoSize()):
        vtd = info.getVideoDesc(i)
        result['videoTracks'].append({
            'trackId': vtd.trackId,
            'codecId': vtd.codecId,
            'desc': {
                'width': vtd.desc.width,
                'height': vtd.desc.height,
                'fps': vtd.desc.fps,
                'type': vtd.desc.type,
            }
        })
    for i in range(info.audioSize()):
        atd = info.getAudioDesc(i)
        result['audioTracks'].append({
            'trackId': atd.trackId,
            'codecId': atd.codecId,
            'desc': {
                'channels': atd.desc.channels,
                'sampleRate': atd.desc.sampleRate,
                'format': atd.desc.format,
            }
        })
    return result
