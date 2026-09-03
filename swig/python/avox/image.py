# avox SDK Python 高层封装 — Image 模块
# IImageBuffer + 图像加载/保存/缩放/裁剪/base64/bytes + 格式工具

import AvoxWrapper as _pw
from avox._core import ImageType, YuvType, IEncodeType, buffer_to_bytes, bytes_to_buffer

# ─── IImageBuffer ────────────────────────────────────────


class IImageBuffer:
    """图像缓冲封装 (AvoxVideo.h IImageBuffer)。

    持有一块连续像素内存 (BGRA8/RGBA8/RGB8/R8/YUV...), 是图像操作与视觉识别的
    基本数据单元。无参构造会 createImageBuffer() 并由 Python GC 释放 (%newobject);
    传入 native 则为借用 (如 ScreenCapture.getBuffer() 返回的, 生命周期归原主)。
    """

    def __init__(self, native=None):
        # native=None: 自创建自拥有; 否则借用外部 native (不负责释放)
        self._native = native if native else _pw.createImageBuffer()

    # ── 格式 ──

    def setFormat(self, width, height, imageType, rowPitch=0):
        """设置图像格式。imageType 为 ImageType 枚举, rowPitch=0 时默认等于 width*像素大小。"""
        fmt = _pw.ImageFormat()
        fmt.width = width
        fmt.height = height
        fmt.imageType = imageType
        fmt.rowPitch = rowPitch
        self._native.setImageFormat(fmt)
        return self

    def getFormat(self):
        """返回原生 ImageFormat (有 .width/.height/.imageType/.rowPitch)。"""
        return self._native.getImageFormat()

    def bufferSize(self):
        """连续缓冲字节数 (= getPointer() 指向的内存大小)。"""
        return self._native.getBufferSize()

    def bDataRef(self):
        """数据是否引用外部数据 (非自有拷贝)。"""
        return self._native.bDataRef()

    # ── 字节读写 ──

    def to_bytes(self):
        """整块像素拷贝出为 Python bytes (长度 = bufferSize())。"""
        return buffer_to_bytes(self._native)

    def from_bytes(self, data):
        """把 bytes 写入缓冲 (要求 format 已设, 且 len(data)==bufferSize())。"""
        return bytes_to_buffer(self._native, data)

    # ── 拷贝 ──

    def copyFrom(self, src, copyData=True):
        """从 src 拷贝。copyData=True 拷数据, False 只拷 dataptr/size/format。"""
        self._native.copyFrom(src._native if isinstance(src, IImageBuffer) else src, copyData)
        return self

    def copyTo(self, dest, copyData=True):
        """拷贝到 dest。dest 可为 IImageBuffer 或原生对象。"""
        nativeDest = dest._native if isinstance(dest, IImageBuffer) else dest
        self._native.copyTo(nativeDest, copyData)
        return self

    def clear(self):
        """清空 (释放/置零数据, 重置格式)。"""
        self._native.clear()
        return self

    # ── 实例便利 (内部走 free function) ──

    def save(self, path):
        """保存到文件 (按后缀选格式: .png/.jpg/.bmp/.tga, 默认 PNG)。"""
        return _pw.saveImagePath(path, self._native)

    def to_base64(self, encodeType=None, quality=85):
        """返回 Base64 字符串。encodeType 为 IEncodeType, 默认 jpg。"""
        config = _pw.IEncodeConfig()
        if encodeType is not None:
            config.encodeType = encodeType
        config.quality = quality
        return _pw.getImageBase64(self._native, config)

    def resize(self, width, height):
        """缩放, 返回新的 IImageBuffer (失败 None)。不要在 tick 中用。"""
        return resize(self, width, height)

    def crop(self, x, y, width, height):
        """从 (x,y) 裁 width*height, 返回新的 IImageBuffer (失败 None)。不要在 tick 中用。"""
        return crop(self, x, y, width, height)

    def toGray(self):
        """转单通道灰度 (0.299R+0.587G+0.114B), 返回新的 IImageBuffer (r8, 失败 None)。"""
        return toGray(self)

    def countInRange(self, roi, lo, hi):
        """ROI 内 "像素所有通道 ∈ [lo,hi]" 的像素占比 [0,1] (像素级掩码语义, 同 cv::inRange)。
        roi=(x,y,w,h) 或 None=整图; lo/hi 为标量(广播三通道)或 (c0,c1,c2);
        通道序跟随原生格式 (bgr8/bgra8=BGR, rgb8/rgba8=RGB, r8 仅 c0)。"""
        return countInRange(self, roi, lo, hi)

    def saveBinary(self, path):
        """以原始二进制保存 (loadImageBufferBinary 的逆)。"""
        return _pw.saveImageBufferBinary(path, self._native)

    # ── 属性 (从 getImageFormat 取) ──

    @property
    def width(self):
        return self._native.getImageFormat().width

    @property
    def height(self):
        return self._native.getImageFormat().height

    @property
    def imageType(self):
        return self._native.getImageFormat().imageType

    @property
    def rowPitch(self):
        return self._native.getImageFormat().rowPitch

    # ── 生命周期 ──

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
        return False

    def destroy(self):
        """释放引用。自创建的由 GC 释放, 这里仅断开 Python 侧引用。"""
        self._native = None


# ─── 模块级工厂 / 图像操作 ───────────────────────────────


def createImageBuffer():
    """创建空 IImageBuffer (随后需 setFormat 或 loadImage 填充)。"""
    return IImageBuffer()


def loadImage(path):
    """从完整路径加载图像 (BMP/PNG/JPG/TGA), 返回 IImageBuffer 或 None。"""
    buf = IImageBuffer()
    if _pw.loadImagePath(path, buf._native):
        return buf
    return None


def loadImageAsset(name):
    """从 assets/images 加载图像 (传文件名, 自动拼路径), 返回 IImageBuffer 或 None。"""
    buf = IImageBuffer()
    if _pw.loadImageAsset(name, buf._native):
        return buf
    return None


def saveImage(path, buf):
    """保存图像到文件 (按后缀选格式)。buf 为 IImageBuffer。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    return _pw.saveImagePath(path, native)


def loadBinary(path):
    """从 saveImageBufferBinary 保存的二进制装载到新 IImageBuffer。"""
    buf = IImageBuffer()
    if _pw.loadImageBufferBinary(path, buf._native):
        return buf
    return None


def resize(buf, width, height):
    """缩放 buf 到 width*height, 返回新 IImageBuffer (失败 None)。不要在 tick 中用。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    out = IImageBuffer()
    if _pw.resizeImage(native, out._native, width, height):
        return out
    return None


def crop(buf, x, y, width, height):
    """从 buf 的 (x,y) 裁 width*height, 返回新 IImageBuffer (失败 None)。不要在 tick 中用。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    out = IImageBuffer()
    if _pw.cropImage(native, out._native, x, y, width, height):
        return out
    return None


def toGray(buf):
    """buf 转单通道灰度 (r8, 0.299R+0.587G+0.114B), 返回新 IImageBuffer (失败 None)。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    out = _pw.toGrayImage(native)
    return IImageBuffer(out) if out else None


def _normRange3(v):
    """标量(广播)或序列 -> [c0,c1,c2] int。"""
    if isinstance(v, (list, tuple)):
        a = [int(x) for x in v]
        while len(a) < 3:
            a.append(a[-1])
        return a[:3]
    iv = int(v)
    return [iv, iv, iv]


def countInRange(buf, roi, lo, hi):
    """ROI 内 "像素所有通道 ∈ [lo,hi]" 的像素占比 [0,1] (像素级掩码语义, 同 cv::inRange)。
    roi=(x,y,w,h) 或 None=整图; lo/hi 为标量(广播三通道)或 (c0,c1,c2);
    通道序跟随原生格式 (bgr8/bgra8=BGR, rgb8/rgba8=RGB, r8 仅 c0)。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    if roi is None:
        fmt = native.getImageFormat()
        x, y, w, h = 0, 0, fmt.width, fmt.height
    else:
        x, y, w, h = int(roi[0]), int(roi[1]), int(roi[2]), int(roi[3])
    lo3 = _normRange3(lo)
    hi3 = _normRange3(hi)
    return _pw.countInRange(native, x, y, w, h,
                            lo3[0], lo3[1], lo3[2], hi3[0], hi3[1], hi3[2])


def toBase64(buf, encodeType=None, quality=85):
    """返回 buf 的 Base64 字符串。encodeType 为 IEncodeType, 默认 jpg。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    config = _pw.IEncodeConfig()
    if encodeType is not None:
        config.encodeType = encodeType
    config.quality = quality
    return _pw.getImageBase64(native, config)


def toBytes(buf):
    """buf 整块像素 → bytes。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    return buffer_to_bytes(native)


def fromBytes(buf, data):
    """bytes → buf (format 需已设, len(data)==bufferSize())。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    return bytes_to_buffer(native, data)


# ─── 格式 / 尺寸工具 ──────────────────────────────────────


def getPixelSize(imageType):
    """ImageType 单像素字节数。"""
    return _pw.getPixelSize(imageType)


def getImageSize(imageFormat):
    """ImageFormat 的缓冲字节数。imageFormat 可为原生 ImageFormat。"""
    return _pw.getImageSize(imageFormat)


def getImageTypeStr(imageType):
    """ImageType → 字符串。"""
    return _pw.getImageTypeStr(imageType)


def getYuvTypeStr(yuvType):
    """YuvType → 字符串。"""
    return _pw.getYuvTypeStr(yuvType)


def bVPlaneFormat(yuvType):
    """是否平面 YUV 格式 (420P/422P/444P/NV12)。"""
    return _pw.bVPlaneFormat(yuvType)


# ─── 进阶 YUV 转换 ──────────────────────────────────────
# 链路1: ISurfaceRender.onFrame(YUVFrame) → yuvframe2Rgba(frame, buf) → IImageBuffer(RGBA)
# 链路2: IImageBuffer(YUV) --image2YUVFrame--> YUVFrame --yuvframe2Rgba--> IImageBuffer(RGBA)
# unpackGpuYUV: GPU 渲染输出的 YUV IImageBuffer, UV padding 重排给 FFmpeg 读 (原地)
# YUVFrame.data/stride 由 C++ 填 (onFrame 回调 / image2YUVFrame), Python 不手填。

def getYuvFrameSize(yuvFormat, rowPitch):
    """计算 YUV 帧字节数。yuvFormat 为 _pw.YUVFormat。"""
    return _pw.getYuvFrameSize(yuvFormat, rowPitch)


def image2YUVFrame(buf, yuvType):
    """从 IImageBuffer (R8) 导出 _pw.YUVFrame; 返回 (ok, yuvFrame)。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    frame = _pw.YUVFrame()
    ok = _pw.image2YUVFrame(native, frame, yuvType)
    return ok, frame


def yuvframe2Rgba(frame, buf):
    """YUVFrame → IImageBuffer(RGBA)。
    frame 为 _pw.YUVFrame (常用 image2YUVFrame 导出的, data 指向某 IImageBuffer 内部);
    buf 为目标 IImageBuffer (会被内部 setFormat 为 width×height rgba8)。
    返回 bool。仅支持 yuv420P / nv12。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    return _pw.yuvframe2Rgba(frame, native)


def unpackGpuYUV(buf, yuvType):
    """GPU 渲染输出 YUV 的 UV padding 重排 (原地, 不可逆)。
    buf 为含 YUV 平面数据的 IImageBuffer (rowPitch != width 时生效);
    yuvType 为 YuvType (yuv420P/yuv422P)。重排后 stride=rowPitch/2, FFmpeg 等距读取。"""
    native = buf._native if isinstance(buf, IImageBuffer) else buf
    _pw.unpackGpuYUV(native, yuvType)
