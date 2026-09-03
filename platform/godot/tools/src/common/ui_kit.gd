extends RefCounted
## UiKit —— avox Godot 工具共享 UI 基建 (mediaplayer / hub 共用, static 无状态)
## 主题字体: 打包的 Noto Sans SC 可变字体 (全平台一致中文渲染), 缺失回退系统字体链。
## desktop_scale: 桌面高 DPI 每显示器缩放 (canvas_items+expand 下只放大窗口即可, 勿再设 content_scale_factor;
## 仅当窗口物理尺寸=布局基准×sc 时成立; 项目基准现为 1920x1080, 1280 旧场景/非基准小窗口
## 须自行钉定 content_scale_size, 见 mediaplayer/_ready 与 hub/_set_launcher_window)。

const FONT_PATH := "res://assets/fonts/NotoSansSC-VF.ttf"
const ACCENT := Color("#3B82F6")

static func is_touch() -> bool:
	return OS.has_feature("mobile") or OS.has_feature("android") or DisplayServer.is_touchscreen_available()

## 桌面高 DPI 缩放系数 (Godot 4.3+ screen_get_scale, 老版本/dpi 兜底, 上限 3.0)
static func desktop_scale(screen: int = -1) -> float:
	var sc := DisplayServer.screen_get_scale(screen)
	if sc <= 1.0:
		sc = DisplayServer.screen_get_dpi(screen) / 96.0
	return clampf(sc, 1.0, 3.0)

## 主窗口场景通用高 DPI 适配 (Windows/Linux): 钉画布基准 + 窗口物理 ×sc + 重居中 + 逻辑最小尺寸。
## 返回 sc 供跨屏跟踪; 移动端全屏走 content_scale_factor 方案, 见 mediaplayer/_ready (仅播放器需要)。
static func apply_desktop_dpi(win: Window, base: Vector2i = Vector2i(1280, 720)) -> float:
	if OS.has_feature("mobile") or OS.has_feature("android"):
		return 1.0
	if not (OS.has_feature("windows") or OS.has_feature("linux")):
		return 1.0
	win.content_scale_size = base
	var sc := desktop_scale(win.current_screen)
	if sc > 1.0:
		# 放大保持左上角锚点会溢出屏幕右/下 → 重居中
		win.size = Vector2i(int(win.size.x * sc), int(win.size.y * sc))
		win.move_to_center()
	win.min_size = Vector2i(int(base.x * 0.5 * sc), int(base.y * 0.5 * sc))
	return sc

static func _default_font() -> Font:
	if ResourceLoader.exists(FONT_PATH):
		var f: Font = load(FONT_PATH)
		if f != null:
			return f
	var sys := SystemFont.new()
	sys.font_names = PackedStringArray(["MiSans", "HarmonyOS Sans SC", "Microsoft YaHei UI",
		"Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", "Source Han Sans SC", "sans-serif"])
	return sys

## 指定字重 (可变字体 wght 轴; 系统字体回退时为伪加粗)
static func font_weight(w: int) -> FontVariation:
	var fv := FontVariation.new()
	fv.base_font = _default_font()
	fv.variation_opentype = {TextServerManager.get_primary_interface().name_to_tag("wght"): w}
	return fv

static func build_theme() -> Theme:
	var th := Theme.new()
	th.default_font = _default_font()
	th.default_font_size = 14
	# LineEdit: 与自绘控件同语言 (深底圆角, ACCENT 焦点描边)
	var le := flat(Color(1, 1, 1, 0.06), 6)
	le.set_content_margin_all(8)
	th.set_stylebox("normal", "LineEdit", le)
	var le_f := flat(Color(1, 1, 1, 0.10), 6, ACCENT, 1)
	le_f.set_content_margin_all(8)
	th.set_stylebox("focus", "LineEdit", le_f)
	th.set_stylebox("read_only", "LineEdit", flat(Color(1, 1, 1, 0.03), 6))
	th.set_color("font_color", "LineEdit", Color(0.92, 0.95, 1.0))
	th.set_color("font_placeholder_color", "LineEdit", Color(0.45, 0.5, 0.58))
	th.set_color("caret_color", "LineEdit", ACCENT)
	th.set_color("selection_color", "LineEdit", Color(ACCENT, 0.35))
	# PopupMenu: 圆角深面板 + ACCENT hover + 细分隔线 (替代系统默认方角蓝 hover)
	th.set_stylebox("panel", "PopupMenu", flat(Color(0.06, 0.08, 0.11, 0.97), 8, Color(1, 1, 1, 0.10), 1))
	th.set_stylebox("hover", "PopupMenu", flat(Color(ACCENT, 0.22), 5))
	var sep := StyleBoxLine.new()
	sep.color = Color(1, 1, 1, 0.08)
	sep.grow_begin = -8.0
	sep.grow_end = -8.0
	th.set_stylebox("separator", "PopupMenu", sep)
	th.set_color("font_color", "PopupMenu", Color(0.88, 0.91, 0.95))
	th.set_color("font_hover_color", "PopupMenu", Color.WHITE)
	th.set_constant("v_separation", "PopupMenu", 6)
	# ScrollBar: 深色细拇指 (默认亮色粗条在暗 UI 里刺眼)
	for t in ["VScrollBar", "HScrollBar"]:
		th.set_stylebox("scroll", t, flat(Color(1, 1, 1, 0.04), 4))
		th.set_stylebox("grabber", t, flat(Color(1, 1, 1, 0.22), 4))
		th.set_stylebox("grabber_highlight", t, flat(Color(1, 1, 1, 0.35), 4))
		th.set_stylebox("grabber_pressed", t, flat(Color(ACCENT, 0.6), 4))
	# CheckBox
	th.set_color("font_color", "CheckBox", Color(0.92, 0.95, 1.0))
	th.set_color("font_hover_color", "CheckBox", Color.WHITE)
	return th

static func flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.set_corner_radius_all(radius)
	sb.border_color = border
	sb.set_border_width_all(bw)
	return sb
