class_name ScrubBar
extends Control
## ScrubBar —— avox 播放器自绘视频进度条(沉浸式悬浮)
## 三段一次画: 轨道 / 已缓冲 / 已播(accent), 胶囊圆角, 点击拖拽即 scrub。
## 拖拽期间只发 scrub_moved; 松手才发 scrub_ended(上层才 seek), 避免拖拽中狂 seek。

signal scrub_started(value: float)
signal scrub_moved(value: float)
signal scrub_ended(value: float)

const ACCENT   := Color("#3B82F6")
const TRACK    := Color(1, 1, 1, 0.22)   # 亮画面下 0.16 几乎不可见
const BUFFERED := Color(1, 1, 1, 0.30)

## 已播进度 0..1 (上层每帧喂)
var value := 0.0:
	set(v):
		value = clampf(v, 0.0, 1.0)
		queue_redraw()

## 已缓冲水位 0..1 (上层喂, 现 API 无精确值, 缓冲时顶到播放头)
var buffered := 0.0:
	set(v):
		buffered = clampf(v, 0.0, 1.0)
		queue_redraw()

## 直播/禁用: 整条灰置灰, 不响应拖拽
var disabled := false:
	set(v):
		disabled = v
		mouse_default_cursor_shape = CURSOR_ARROW if v else CURSOR_POINTING_HAND
		queue_redraw()

## 触屏无 hover: 恒显进度抓取点, 否则看不见当前播放位置
var always_grabber := false:
	set(v):
		always_grabber = v
		queue_redraw()

## 媒体总时长 ms (上层每帧喂; <=0 不显示 hover 时间气泡)
var duration_ms := 0.0:
	set(v):
		duration_ms = v
		queue_redraw()

var _hovered := false
var _dragging := false
var _hover_x := -1.0            # 光标 x (气泡/指示用), <0 表示不在条上
var _bar_h := 4.0:
	set(v):
		_bar_h = v
		queue_redraw()
var _tween: Tween
var _sb_track: StyleBoxFlat
var _sb_buf: StyleBoxFlat
var _sb_play: StyleBoxFlat
var _sb_bubble: StyleBoxFlat

func _ready() -> void:
	custom_minimum_size = Vector2(0, 24)
	mouse_filter = Control.MOUSE_FILTER_STOP
	mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	mouse_entered.connect(_on_mouse_entered)
	mouse_exited.connect(_on_mouse_exited)
	_sb_track = _seg(TRACK)
	_sb_buf = _seg(BUFFERED)
	_sb_play = _seg(ACCENT)
	_sb_bubble = _seg(Color(0.06, 0.08, 0.11, 0.96))
	_sb_bubble.set_corner_radius_all(6)
	_sb_bubble.border_color = Color(1, 1, 1, 0.10)
	_sb_bubble.set_border_width_all(1)

func _seg(c: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = c
	sb.set_corner_radius_all(5)
	return sb

func _on_mouse_entered() -> void:
	_hovered = true
	_animate_h()

func _on_mouse_exited() -> void:
	_hovered = false
	_hover_x = -1.0
	queue_redraw()
	if not _dragging:
		_animate_h()

func _animate_h() -> void:
	if _tween and _tween.is_valid():
		_tween.kill()
	_tween = create_tween()
	_tween.tween_property(self, "_bar_h", 10.0 if (_hovered or _dragging) else 4.0, 0.12)

func _gui_input(event: InputEvent) -> void:
	if disabled:
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_dragging = true
			_set_from_pos(event.position.x)
			scrub_started.emit(value)
		elif _dragging:
			_dragging = false
			_set_from_pos(event.position.x)
			_animate_h()
			scrub_ended.emit(value)
		accept_event()
	elif event is InputEventMouseMotion:
		_hover_x = clampf(event.position.x, 0.0, size.x)
		queue_redraw()
		if _dragging:
			_set_from_pos(event.position.x)
			scrub_moved.emit(value)
		accept_event()

func _set_from_pos(px: float) -> void:
	if size.x > 0.0:
		value = px / size.x

func _draw() -> void:
	var w := size.x
	var h := size.y
	var bar_h := 4.0 if disabled else _bar_h
	var y := (h - bar_h) * 0.5
	draw_style_box(_sb_track, Rect2(0, y, w, bar_h))
	if disabled:
		return
	if buffered > 0.0:
		draw_style_box(_sb_buf, Rect2(0, y, w * buffered, bar_h))
	if value > 0.0:
		draw_style_box(_sb_play, Rect2(0, y, w * value, bar_h))
	if (_hovered or _dragging or always_grabber) and not disabled:
		var tx := w * value
		draw_circle(Vector2(tx, h * 0.5), 7.0, ACCENT)
		draw_circle(Vector2(tx, h * 0.5), 3.0, Color.WHITE)
	if (_hovered or _dragging) and _hover_x >= 0.0 and duration_ms > 0.0:
		_draw_bubble(_hover_x, _hover_x / maxf(w, 1.0))

# hover 时间气泡: 圆角胶囊 + 当前指向时刻 (放不下时翻到条下方)
func _draw_bubble(px: float, frac: float) -> void:
	var txt := _fmt_ms(frac * duration_ms)
	var font := get_theme_default_font()
	var fsize := 12
	var ts := font.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize)
	var pad := 8.0
	var bw := ts.x + pad * 2.0
	var bh := ts.y + 5.0
	var bx := clampf(px - bw * 0.5, 2.0, maxf(size.x - bw - 2.0, 2.0))
	var by := size.y * 0.5 - _bar_h * 0.5 - bh - 8.0
	if by < 0.0:
		by = size.y * 0.5 + _bar_h * 0.5 + 8.0
	draw_style_box(_sb_bubble, Rect2(bx, by, bw, bh))
	draw_string(font, Vector2(bx + pad, by + bh - 5.0), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fsize, Color.WHITE)
	# 气泡到条的指示竖线
	draw_line(Vector2(px, by + bh), Vector2(px, size.y * 0.5 - _bar_h * 0.5 - 1.0), Color(1, 1, 1, 0.25), 1.0)

func _fmt_ms(ms: float) -> String:
	var t := int(ms / 1000.0)
	if t >= 3600:
		return "%d:%02d:%02d" % [t / 3600, (t / 60) % 60, t % 60]
	return "%02d:%02d" % [t / 60, t % 60]
