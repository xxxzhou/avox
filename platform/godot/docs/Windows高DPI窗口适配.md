# Windows/Linux 高 DPI 窗口适配 (Godot 4.x 通用)

## 现象

4K 屏 + Windows 显示缩放 150% 下, Godot 工程窗口只有 1280 逻辑宽 (project.godot
`viewport_width/height`), 物理上就是 1280px, 在高分屏上显小。

`display/window/dpi/allow_hidpi=true` 只保证渲染清晰 (不做位图拉伸虚拟化),
**不会**把窗口/内容按系统缩放放大, 需要运行时自己适配。

## 关键认知: canvas_items + expand 下的缩放链

拉伸模式 `canvas_items` + `aspect="expand"` 时, 内容总缩放是:

```
总缩放 = 基准比 min(win.w/基准宽, win.h/基准高) × content_scale_factor
逻辑视口 = 窗口像素 / 总缩放
基准 = Window.content_scale_size (项目默认 = viewport, 2026-08-29 起为 1920x1080;
       1280 设计的旧场景在 _ready 钉定 1280x720, 初始窗口物理尺寸经
       window/size/window_*_override 钉 1280x720 —— 布局/尺寸零改动)
```

**坑: 两者都会放大内容**。窗口放大同时又设 `content_scale_factor` 会二次缩放:
逻辑视口缩水 (如 1280→512), 按 1280 基准设计的布局必然横向溢出、控件巨大。
(2026-08-29 修复, 之前 "窗口×sc 且 factor=sc" 的写法在 250% 屏上总缩放 6.25 倍。)

## 修法 (主场景 _ready)

### 桌面 (Windows/Linux): 只放大窗口, 不动 content_scale_factor

```gdscript
var sc := DisplayServer.screen_get_scale(get_window().current_screen)   # 4.3+, 需 allow_hidpi
if sc <= 1.0:
    sc = DisplayServer.screen_get_dpi(get_window().current_screen) / 96.0
sc = clampf(sc, 1.0, 3.0)
if sc > 1.0:
    var win := get_window()
    win.size = Vector2i(int(win.size.x * sc), int(win.size.y * sc))
    win.move_to_center()   # 放大保持左上角锚点会溢出屏幕右/下, 重居中
```

窗口物理尺寸 ×sc 后, 基准比 = sc, 逻辑视口保持基准尺寸 (播放器 1280x720), 渲染在
物理分辨率上 (allow_hidpi) 文字锐利 —— 与原生应用 250% 缩放的表现一致。

### 移动端 (Android/iOS): factor 除回基准比

手机窗口即全屏, 基准比 ≥1 且随横竖屏变化。目标总缩放 T = dpi/160 (1 逻辑px≈1dp),
上限由「逻辑短边 ≥360」约束 (写死 2.0 时 440dpi 手机控件只有 ~0.7dp/px, 触控目标
远小于 48dp 规范):

```gdscript
var t := clampf(dpi / 160.0, 1.0, 3.0)
var scr := DisplayServer.screen_get_size(get_window().current_screen)
var base := 1.0
if scr.x > 0 and scr.y > 0:
    t = clampf(minf(t, minf(scr.x, scr.y) / 360.0), 1.0, 3.0)
    # 取长边/短边计算, 与启动时横竖屏状态无关 (锁 sensor_landscape 的 app)
    base = minf(maxf(scr.x, scr.y) / 1280.0, minf(scr.x, scr.y) / 720.0)
get_window().content_scale_factor = t / maxf(base, 0.01)
```

### 非 1280x720 基准的小窗口 (hub launcher): 另钉 content_scale_size

「只放大窗口」仅当窗口物理尺寸 = 布局基准 × sc 时成立 (播放器: 初始窗口即基准
尺寸)。hub 的 510x480 launcher 小窗不满足: 不钉基准时画布被 expand 撑到项目基准
(旧 1280 基准时为 1280x1204), 460 宽内容只占窗口 36% (2026-08-29 修复的「图标显小」
根因)。修法: 窗口物理尺寸照常 ×sc, 再把画布基准钉到窗口逻辑尺寸, 总缩放即 = sc:

```gdscript
get_window().content_scale_size = Vector2i(510, 480)   # 画布=510x480, ×sc 窗口下总缩放=sc
# 切回 1280 基准的场景 (如 hub → 播放器) 前必须还原:
get_window().content_scale_size = Vector2i(1280, 720)
```

## 要点

- 前提: `project.godot` 里 `window/dpi/allow_hidpi=true`,
  `window/stretch/mode="canvas_items"` (+`aspect="expand"`)。
- `DisplayServer.screen_get_scale()` (4.3+) 返回 Windows 每显示器缩放
  (1.0/1.25/1.5/2.5/…); 老版本或拿不到时兜底 `screen_get_dpi()/96`。
- 桌面缩放上限 2.0 → 3.0: 250%/300% 缩放屏不再显小。
- 窗口放大后记得 `move_to_center()`, 否则窗口溢出屏幕右/下。
- 移动端 `content_scale_factor` 不是"总缩放", 是基准比之上的附加系数。
