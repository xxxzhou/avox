# avox SDK Python 高层封装 — Video 模块
# ISurfaceRender, IFontLayer, IGeometryLayer, IImageRender

import AvoxWrapper as _pw
from avox._core import _CallbackBridge
from avox._observer import _DefaultSurfaceRenderOb
from avox.image import IImageBuffer

# ─── ISurfaceRender ─────────────────────────────────────

class ISurfaceRender(_CallbackBridge):
    """视频渲染表面封装。

    正规模式: addObserver(ISurfaceRenderOb子类) / removeObserver(ob)
    快捷模式: onFrame(cb) / onSurface(cb) / onWinSizeChange(cb) / onRender(cb)
    """

    def __init__(self, native):
        super().__init__()
        self._native = native
        self._fontLayer = None
        self._geometryLayer = None

    # ── Observer (正规模式) ──

    def _addObserverImpl(self, ob):
        _pw.addSurfaceRenderOb(self._native, ob)

    def _removeObserverImpl(self, ob):
        _pw.removeSurfaceRenderOb(self._native, ob)

    # ── Observer (快捷模式) ──

    def _ensureDefaultObserver(self):
        if self._default_ob is None:
            self._default_ob = _DefaultSurfaceRenderOb(self)
            self._default_ob.__disown__()
            self._observers.append(self._default_ob)
            _pw.addSurfaceRenderOb(self._native, self._default_ob)

    def onFrame(self, callback):
        """注册帧回调: onFrame(YUVFrame)"""
        return self._on('onFrame', callback)

    def onSurface(self, callback):
        """注册 Surface 就绪回调"""
        return self._on('onSurface', callback)

    def onWinSizeChange(self, callback):
        """注册窗口大小变化回调: onWinSizeChange(width, height)"""
        return self._on('onWinSizeChange', callback)

    def onRender(self, callback):
        """注册渲染完成回调"""
        return self._on('onRender', callback)

    # ── 直接代理 ──

    def setVulkan(self, bVulkan):
        """设置渲染后端。bVulkan=True(默认) 使用 Vulkan; False 使用平台原生 (DX11/EGL/Metal)。需在 setSurface 之前调用。"""
        return self._native.setVulkan(bVulkan)

    def setSurface(self, surface):
        """设置渲染窗口句柄。surface 为平台原生窗口: Windows HWND / Android ANativeWindow / iOS CAMetalLayer。"""
        return self._native.setSurface(surface)

    def setOffSurface(self, ytype):
        """设置离屏渲染 (无窗口模式, 适用于 Electron CPU 渲染/截图等)。ytype 为 YuvType, 有值时自动启用 YUV 输出。"""
        return self._native.setOffSurface(ytype)

    def getSurface(self):
        """获取当前渲染窗口句柄 (void*)。"""
        return self._native.getSurface()

    def enableYuvOut(self, ytype):
        """启用 YUV 帧输出 (Electron 等无原生窗口场景)。ytype 为 YuvType。配合 onFrame 回调获取帧数据。"""
        return self._native.enableYuvOut(ytype)

    def disableYuvOut(self):
        """禁用 YUV 帧输出。"""
        return self._native.disableYuvOut()

    def setAutoAspect(self, bEnable):
        """设置自适应长宽比。bEnable=True(默认) 保持原始比例; False 拉伸全屏。"""
        return self._native.setAutoAspect(bEnable)

    def screenShot(self, imageBuffer):
        """截取当前渲染帧到 imageBuffer。imageBuffer 可为 IImageBuffer 封装或原生对象。
        高层用法建议用 screenShotToPath / screenShotToBase64。"""
        native = imageBuffer._native if isinstance(imageBuffer, IImageBuffer) else imageBuffer
        return self._native.screenShot(native)

    def enableSizeChange(self, width, height):
        """设置窗口大小 (像素)。"""
        return self._native.enableSizeChange(width, height)

    def enableSizeScale(self, scale):
        """按比例缩放窗口。scale 如 0.5 = 半尺寸。"""
        return self._native.enableSizeScale(scale)

    def disableSizeChange(self):
        """禁用窗口大小调整。"""
        return self._native.disableSizeChange()

    def disableWatermark(self):
        """禁用水印。"""
        return self._native.disableWatermark()

    def disableLut(self):
        """禁用 LUT 滤镜。"""
        return self._native.disableLut()

    def disableBasicAdjust(self):
        """禁用基础图像调节 (亮度/对比度/饱和度/色相/伽马)。"""
        return self._native.disableBasicAdjust()

    def disableSharpen(self):
        """禁用锐化。"""
        return self._native.disableSharpen()

    def disableAnime4K(self):
        """禁用 Anime4K 超分辨率。"""
        return self._native.disableAnime4K()

    # ── 便利方法 ──

    def screenShotToPath(self, path):
        """截图并保存到文件。自动创建 IImageBuffer。"""
        imageBuffer = _pw.createImageBuffer()
        if self._native.screenShot(imageBuffer):
            return _pw.saveImagePath(path, imageBuffer)
        return False

    def screenShotToBase64(self, encodeType=None, quality=85):
        """截图并返回 Base64 字符串。自动创建 IImageBuffer + IEncodeConfig。"""
        imageBuffer = _pw.createImageBuffer()
        if self._native.screenShot(imageBuffer):
            config = _pw.IEncodeConfig()
            if encodeType is not None:
                config.encodeType = encodeType
            config.quality = quality
            return _pw.getImageBase64(imageBuffer, config)
        return None

    def enableWatermark(self, params, assetsPath):
        """启用水印。params 为 dict: {centerX, centerY, width, height, alaph}。
        自动创建 Watermark + IImageBuffer + loadImageAsset。"""
        wm = _pw.Watermark()
        wm.centerX = params.get('centerX', 0)
        wm.centerY = params.get('centerY', 0)
        wm.width = params.get('width', 0)
        wm.height = params.get('height', 0)
        wm.alaph = params.get('alaph', 1.0)
        imageBuffer = _pw.createImageBuffer()
        if _pw.loadImageAsset(assetsPath, imageBuffer):
            self._native.enableWatermark(wm, imageBuffer)
            return True
        return False

    def enableLut(self, index):
        """启用 LUT 滤镜。index 为 LUT 索引号。自动创建 LutParamet。"""
        paramet = _pw.LutParamet()
        paramet.lutIndex = index
        self._native.enableLut(paramet)

    def enableBasicAdjust(self, params):
        """启用基础调节。params 为 dict: {hue, brightness, contrast, saturation, gamma}。"""
        p = _pw.BasicAdjustParamet()
        if 'hue' in params: p.hue = params['hue']
        if 'brightness' in params: p.brightness = params['brightness']
        if 'contrast' in params: p.contrast = params['contrast']
        if 'saturation' in params: p.saturation = params['saturation']
        if 'gamma' in params: p.gamma = params['gamma']
        self._native.enableBasicAdjust(p)

    def updateSharpen(self, params):
        """更新锐化。params 为 dict: {offset, sharpness}。"""
        p = _pw.SharpenVideo()
        if 'offset' in params: p.offset = params['offset']
        if 'sharpness' in params: p.sharpness = params['sharpness']
        self._native.updateSharpen(p)

    def enableAnime4K(self, params):
        """启用 Anime4K。params 为 dict: {mode, variant, strength, enableClampHighlights}。"""
        p = _pw.Anime4KParamet()
        if 'mode' in params: p.mode = params['mode']
        if 'variant' in params: p.variant = params['variant']
        if 'strength' in params: p.strength = params['strength']
        if 'enableClampHighlights' in params: p.enableClampHighlights = params['enableClampHighlights']
        self._native.enableAnime4K(p)

    def enableFont(self):
        """启用字体层, 返回 IFontLayer 封装。"""
        if self._fontLayer is None:
            nativeFont = _pw.enableRenderFont(self._native)
            if nativeFont:
                self._fontLayer = IFontLayer(nativeFont)
        return self._fontLayer

    def disableFont(self):
        """禁用字体层。"""
        _pw.disableRenderFont(self._native)
        self._fontLayer = None

    def enableGeometry(self):
        """启用几何层, 返回 IGeometryLayer 封装。"""
        if self._geometryLayer is None:
            nativeGeo = _pw.enableRenderGeometry(self._native)
            if nativeGeo:
                self._geometryLayer = IGeometryLayer(nativeGeo)
        return self._geometryLayer

    def disableGeometry(self):
        """禁用几何层。"""
        _pw.disableRenderGeometry(self._native)
        self._geometryLayer = None

    # ── 生命周期 ──

    def destroy(self):
        """清理 observer 和缓存。注意: ISurfaceRender 生命周期由 Player 管理, 通常不需要手动 destroy。"""
        self._cleanupObservers()
        self._fontLayer = None
        self._geometryLayer = None

# ─── IFontLayer ─────────────────────────────────────────

class IFontLayer:
    """字体层封装。"""

    def __init__(self, native):
        self._native = native

    def setFont(self, fontName, fontSize):
        """设置字体。fontName 字体名, fontSize 字号。"""
        return self._native.setFont(fontName, fontSize)

    def setColor(self, r, g, b, opacity=0):
        """设置文字颜色。r/g/b 范围 0~255, opacity 不透明度 0~255。"""
        return self._native.setColor(r, g, b, opacity)

    def setScale(self, scale):
        """设置缩放比例。影响文字和线宽。"""
        return self._native.setScale(scale)

    def getLayout(self, index):
        """获取第 index 个布局 (原生 FontLayout)。"""
        return self._native.getLayout(index)

    def setTextLayout(self, index):
        """激活第 index 个布局。"""
        return self._native.setTextLayout(index)

    def drawText(self, text):
        """在当前布局绘制文字。"""
        return self._native.drawText(text)

    def updateLayout(self, index, layout):
        """便利: 从 dict 更新布局。layout: {x, y, width, height, horizontal, vertical}。
        自动创建 FontLayout + Alignment。"""
        fl = _pw.FontLayout()
        fl.x = layout.get('x', 0)
        fl.y = layout.get('y', 0)
        fl.width = layout.get('width', 0)
        fl.height = layout.get('height', 0)
        alignment = _pw.Alignment()
        alignment.horizontal = layout.get('horizontal', 0)
        alignment.vertical = layout.get('vertical', 0)
        fl.alignment = alignment
        self._native.updateLayout(index, fl)

# ─── IGeometryLayer ─────────────────────────────────────

class IGeometryLayer:
    """几何层封装 (画线/矩形/圆/点)。"""

    def __init__(self, native):
        self._native = native

    def setColor(self, r, g, b):
        """设置绘图颜色。r/g/b 范围 0~255。"""
        return self._native.setColor(r, g, b)

    def setScale(self, scale):
        """设置缩放比例。影响线宽和图形大小。"""
        return self._native.setScale(scale)

    def setThreshold(self, tau):
        """设置 AA 阈值 tau。仅控制抗锯齿切换, 不影响核心线宽。"""
        return self._native.setThreshold(tau)

    def clear(self):
        """清除所有已绘制的图形。"""
        return self._native.clear()

    def drawPoint(self, x, y, radiusPx):
        """画点。x/y 为归一化坐标 [0,1], radiusPx 为像素半径。"""
        return self._native.drawPoint(x, y, radiusPx)

    def drawLine(self, x0, y0, x1, y1):
        """画线。坐标为归一化 [0,1]。"""
        return self._native.drawLine(x0, y0, x1, y1)

    def drawRect(self, x0, y0, x1, y1, fill=False):
        """画矩形。坐标归一化 [0,1], fill=True 填充。"""
        return self._native.drawRect(x0, y0, x1, y1, fill)

    def drawCircle(self, cx, cy, radiusPx, fill=False):
        """画圆。cx/cy 归一化 [0,1], radiusPx 像素半径, fill=True 填充。"""
        return self._native.drawCircle(cx, cy, radiusPx, fill)

# ─── IImageRender ───────────────────────────────────────

class IImageRender:
    """静态图片渲染器封装。"""

    def __init__(self, native):
        self._native = native
        self._surfaceRender = None

    def getSurfaceRender(self):
        if self._surfaceRender is None:
            nativeSr = self._native.getSurfaceRender()
            if nativeSr:
                self._surfaceRender = ISurfaceRender(nativeSr)
        return self._surfaceRender

    def render(self, frame):
        """更新图像/YUV帧并触发单次渲染+呈现。
        frame 支持:
          - IImageBuffer (Image 模块) 或原生 IImageBuffer: rgba8/bgra8/argb8 等格式
          - _pw.YUVFrame: yuv420P/nv12/yuv422P/yuv444P 等
            (经 onFrame 回调或 image2YUVFrame 得到; data/stride 由 C++ 填, Python 不手填)"""
        # IImageBuffer 封装需解包 ._native; 原生 IImageBuffer / _pw.YUVFrame 直接传
        native = frame._native if isinstance(frame, IImageBuffer) else frame
        return self._native.render(native)

# ─── 模块级工厂函数 ─────────────────────────────────────

def createImageRender():
    """创建 IImageRender 封装实例。"""
    native = _pw.createImageRender()
    return IImageRender(native) if native else None

def canVulkan():
    """检查 Vulkan 是否可用。"""
    return _pw.canVulkan()
