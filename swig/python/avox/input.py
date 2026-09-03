# avox SDK Python 高层封装 — Input 模块
# IInputController, IScreenCapture
# (ITemplateMatcher/ITextRecognizer/窗口辅助 已移至 avox.vision; 下方 re-export 保向后兼容)

import AvoxWrapper as _pw
from avox._core import MouseButton

# ─── IInputController ───────────────────────────────────

class IInputController:
    """输入控制器封装 (鼠标/键盘注入)。"""

    def __init__(self, native=None):
        self._native = native if native else _pw.createInputController()

    # ── 鼠标 ──

    def moveTo(self, x, y):
        """移动鼠标到绝对坐标 (x, y)。"""
        return self._native.moveTo(x, y)

    def moveBy(self, dx, dy):
        """移动鼠标相对偏移 (dx, dy)。"""
        return self._native.moveBy(dx, dy)

    def mouseDown(self, button):
        """按下鼠标键。button 为 MouseButton 枚举。"""
        return self._native.mouseDown(button)

    def mouseUp(self, button):
        """释放鼠标键。button 为 MouseButton 枚举。"""
        return self._native.mouseUp(button)

    def click(self, *args):
        """点击。click(button) 或 click(x, y, button)"""
        return self._native.click(*args)

    def doubleClick(self, x, y):
        """双击坐标 (x, y)。"""
        return self._native.doubleClick(x, y)

    def drag(self, *args):
        """拖拽。drag(x1, y1, x2, y2) 或 drag(x1, y1, x2, y2, button)"""
        return self._native.drag(*args)

    def scroll(self, dx, dy):
        """滚动鼠标滚轮。dx 水平, dy 垂直, 正负方向。"""
        return self._native.scroll(dx, dy)

    def getCursorPos(self):
        """获取鼠标位置, 返回 (x, y) 元组。"""
        pos = _pw.vec2i()
        self._native.getCursorPos(pos)
        return (pos.x, pos.y)

    # ── 键盘 ──

    def keyDown(self, key):
        """按下键盘键。key 为 KeyCode 常量 (如 _pw.KeyCode_a)。"""
        return self._native.keyDown(key)

    def keyUp(self, key):
        """释放键盘键。key 为 KeyCode 常量。"""
        return self._native.keyUp(key)

    def press(self, key, holdMs=0):
        """按下并释放键。holdMs 按住时长 (ms), 默认 0。"""
        return self._native.press(key, holdMs)

    def hotkey(self, *keys):
        """组合键。hotkey(key1, key2, ...) 自动转为 C 数组。"""
        n = len(keys)
        arr = _pw.intArray(n)
        for i, k in enumerate(keys):
            arr[i] = k
        self._native.hotkey(arr, n)
        _pw.delete_intArray(arr)

    def typeText(self, text):
        """输入文本 (Unicode)。"""
        return self._native.typeText(text)

    def keyState(self, key):
        """查询按键状态。返回 True 当前按下。key 为 KeyCode 常量。"""
        return self._native.keyState(key)

    # ── 配置 ──

    def setMoveDurationMs(self, ms):
        """设置鼠标移动动画时长 (ms)。"""
        return self._native.setMoveDurationMs(ms)

    def setMoveSteps(self, n):
        """设置鼠标移动插值步数。"""
        return self._native.setMoveSteps(n)

    def setKeyDelayMs(self, ms):
        """设置按键间延迟 (ms)。"""
        return self._native.setKeyDelayMs(ms)

    def screenBounds(self):
        """获取屏幕大小, 返回 (width, height) 元组。"""
        size = _pw.vec2i()
        self._native.screenBounds(size)
        return (size.x, size.y)

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    # ── 生命周期 ──

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
        return False

    def destroy(self):
        self._native = None

# ─── IScreenCapture ─────────────────────────────────────

class IScreenCapture:
    """屏幕截图/窗口捕获封装。"""

    def __init__(self, native=None):
        self._native = native if native else _pw.createScreenCapture()

    def getWindowCount(self):
        """获取窗口数量。"""
        return self._native.getWindowCount()

    def getWindowAt(self, index):
        """获取窗口信息, 返回 WindowEntry。"""
        out = _pw.WindowEntry()
        self._native.getWindowAt(index, out)
        return out

    def activateWindow(self, titleSub):
        """按标题子串激活窗口。titleSub 窗口标题关键词。"""
        return self._native.activateWindow(titleSub)

    def activateHwnd(self, hwnd):
        """按窗口句柄激活。hwnd 为窗口句柄。"""
        return self._native.activateHwnd(hwnd)

    def showDesktop(self):
        """显示桌面 (最小化所有窗口)。"""
        return self._native.showDesktop()

    def undoDesktop(self):
        """还原 showDesktop() 最小化的窗口 (与 showDesktop 配对, 状态对称)。"""
        return self._native.undoDesktop()

    def setWindow(self, titleSub):
        """设置捕获目标窗口。titleSub 窗口标题关键词。"""
        return self._native.setWindow(titleSub)

    def setScreen(self, screenIndex, showDesktop=True):
        """设置捕获目标屏幕。screenIndex 屏幕索引, showDesktop 是否先显示桌面。"""
        return self._native.setScreen(screenIndex, showDesktop)

    def refresh(self):
        """刷新捕获 (重新获取窗口/屏幕缓冲)。"""
        return self._native.refresh()

    def getBuffer(self):
        """取最新一帧, 返回原生 IImageBuffer (借用, 生命周期归本 capture)。
        高层用法建议包一层 avox.Image.IImageBuffer(cap.getBuffer()) 以获得便利方法。"""
        return self._native.getBuffer()

    def toScreen(self, bufX, bufY):
        """将缓冲区坐标转为屏幕坐标, 返回 vec2i。"""
        return self._native.toScreen(bufX, bufY)

    def crop(self, x, y, w, h):
        """裁剪区域, 返回新的 IScreenCapture。"""
        nativeCrop = self._native.crop(x, y, w, h)
        return IScreenCapture(nativeCrop) if nativeCrop else None

    def save(self, path):
        """保存当前帧到文件 (按后缀选格式)。"""
        return self._native.save(path)

    def targetName(self):
        """获取捕获目标名称。"""
        return self._native.targetName()

    def width(self):
        """获取缓冲区宽度 (像素)。"""
        return self._native.width()

    def height(self):
        """获取缓冲区高度 (像素)。"""
        return self._native.height()

    # ── 生命周期 ──

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()
        return False

    def destroy(self):
        self._native = None

# ─── 模块级工厂函数 ─────────────────────────────────────

def createInputController():
    """创建 IInputController 封装实例。"""
    return IInputController()

def createScreenCapture():
    """创建 IScreenCapture 封装实例。"""
    return IScreenCapture()

# ─── 向后兼容: 视觉类已移至 avox.vision ──────────────────
from avox.vision import (  # noqa: E402,F401
    ITemplateMatcher,
    ITextRecognizer,
    createTemplateMatcher,
    createTextRecognizer,
    findWindowByName,
    getActiveWindow,
    getWindowName,
)
