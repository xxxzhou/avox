# avox/Input — 窗口/桌面 截图与输入模块

## 设计理念:**单一职责接口组合,无胖上下文类**

早期版本有个有状态的 `OpsContext` 胖类(截图+识别+动作一步到位,为 JSON step 设计)。**已删除**。现在按职责拆成独立接口,需要的能力直接组合:

- **截图** → `IScreenCapture`(窗口/桌面截图、枚举、激活、坐标转换)
- **输入** → `IInputController`(鼠标点击/移动、键盘输入/按键/组合键)
- **识别** → 不在本模块,走 `ITextRecognizer`(OCR)/ `ITemplateMatcher`(模板匹配)(`avox/AvoxVision.h`)
- **动作落地** → `ActOps`(坐标→输入)自由函数

> 截图、识别、输入三者解耦:截一次图(`IScreenCapture.getBuffer()` 拿 `IImageBuffer*`)→ 喂给识别器 → 拿坐标 → `toScreen` 转屏幕坐标 → `IInputController` 动作。脚本型 skill 就这么组合(完整范例见 `avox_python_api` skill 的"截图→找文字→点击")。

## IScreenCapture(`avox/AvoxInput.h` + `ScreenCapture.cpp`)

薄 OO 封装,内部委托 `ShotOps` 自由函数,不重写 win_capture 逻辑。

```cpp
class IScreenCapture {
 public:
  virtual int32_t getWindowCount() = 0;                 // 枚举可见窗口
  virtual bool getWindowAt(int32_t index, WindowEntry* out) = 0;  // title/winClass/process/hwnd
  virtual bool activateWindow(const char* titleSub) = 0;
  virtual bool activateHwnd(void* hwnd) = 0;
  virtual bool showDesktop() = 0;
  virtual bool setWindow(const char* titleSub) = 0;     // 绑定窗口 + 截图
  virtual bool setScreen(int32_t screenIndex, bool showDesktop) = 0;  // 绑定桌面 + 截图
  virtual bool refresh() = 0;                            // 当前目标重截
  virtual IImageBuffer* getBuffer() const = 0;          // 托管, 随 ctx 释放
  virtual vec2i toScreen(int32_t bufX, int32_t bufY) const = 0;  // buffer 坐标 → 屏幕坐标
  virtual IScreenCapture* crop(int32_t x, int32_t y, int32_t w, int32_t h) const = 0;
  virtual bool save(const char* path) const = 0;
  virtual const char* targetName() const = 0;
  virtual int32_t width() const = 0;
  virtual int32_t height() const = 0;
};
AVOX_EXPORT IScreenCapture* createScreenCapture();   // 返回裸指针, 调用方 delete 或由 SWIG %newobject GC 释放
```

> **所有权**:`create*` 返回裸指针,调用方 `delete`(SWIG 绑定侧由 `%newobject` 标记,目标语言 GC 自动释放)。

## IInputController(`avox/AvoxInput.h`)

```cpp
class IInputController {
 public:
  virtual bool move(int32_t x, int32_t y) = 0;
  virtual bool click(int32_t x, int32_t y) = 0;
  virtual bool doubleClick(int32_t x, int32_t y) = 0;
  virtual bool drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2) = 0;
  virtual bool scroll(int32_t x, int32_t y, int32_t delta) = 0;
  virtual bool typeText(const char* utf8) = 0;          // 输入文本 (Unicode)
  virtual bool pressKey(KeyCode k, int32_t holdMs = 0) = 0;  // enter/esc/tab...
  virtual bool hotkey(const KeyCode* keys, int32_t n) = 0;   // 组合键 (如 Ctrl+L)
};
AVOX_EXPORT IInputController* createInputController();  // 返回裸指针, 外部 release
```

## 底层自由函数

| 模块 | 函数 | 说明 |
|------|------|------|
| `ShotOps.hpp` | `shotWindow` / `shotScreen` / `listWindowDevices` / `activateWindow` | 窗口/桌面截图、枚举、激活(直接调,IScreenCapture 内部也用它) |
| `ActOps.hpp` | `actAt` / `typeTextAt` / `pressKeyAt` / `hotkeyAt` / `parseKeyCode` / `parseKeyList` | 坐标→动作、按键解析 |
| `Ops.cpp` | `toScreen(info, bufPt)` | buffer 坐标 → 屏幕坐标(WindowShotInfo 基准) |

## 组合示例(skill 脚本里)

```python
from avox import Input, Vision              # 高层封装 (优先; 进阶才 import PyWrapper as _pw)
cap = Input.createScreenCapture()
rec = Vision.createTextRecognizer()                  # 识别 (avox/AvoxVision.h; 首次 recognize 按需加载)
inp = Input.createInputController()
cap.setWindow("某窗口")                     # 绑定 + 截图
buf = cap.getBuffer()
n = rec.recognize(buf)                      # OCR
for i in range(n):
    text, r = rec.getMatch(i)
    if "目标" in text:
        c = rec.ocrCenter(r)                # 文字框中心 (buffer 坐标)
        s = cap.toScreen(c.x, c.y)          # → 屏幕坐标
        inp.click(s.x, s.y)                 # 动作
        break
# cap/inp/rec 由 GC 自动释放 (create* 为 %newobject)
```

> 这是脚本型 skill 的实际写法(高层 `avox` 包; 速查见 `avox_python_api` skill)。CLI 侧 `avox_cli ops`/`input` 命令也走这套组合(见 `CmdOps.cpp`/`CmdInput.cpp`)。

## 文件结构

```
src/avox/Input/
├── ScreenCapture.cpp   # IScreenCapture 实现 (委托 ShotOps)
├── ShotOps.{hpp,cpp}   # 底层截图 (shotWindow/shotScreen/listWindowDevices/activateWindow)
├── ActOps.{hpp,cpp}    # 底层动作 (actAt/typeTextAt/pressKeyAt/hotkeyAt/parseKeyCode/parseKeyList)
├── Ops.cpp             # toScreen (buffer→屏幕坐标)
├── Input.cpp           # InputAction 工具
└── README.md
# 接口声明在 src/avox/AvoxInput.h (IScreenCapture / IInputController / KeyCode / WindowEntry ...)
```
