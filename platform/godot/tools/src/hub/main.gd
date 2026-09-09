extends Control
## avox 工具主界面 (hub) —— 工具 launcher
## 工具列表数据驱动 (加工具 = 加一条 _tools); 插件层只含播放级能力
## (播放/WebRTC/采集/录制), 模型类功能 (Agent/Avatar/STT) 走独立交付, 不进工具箱。
## 依赖: addons/avox_godot 已部署。

const ACCENT   := Color("#3B82F6")
const BG_COLOR := Color("#0B0F16")
# ── 共享 UI 基建 (主题字体/高 DPI 缩放/flat stylebox, 与 mediaplayer 共用) ──
const UiKit := preload("res://src/common/ui_kit.gd")

const _tools := [
	{"id": "player", "icon": IconButton.Icon.PLAY, "title": "播放器",   "desc": "视频 / 直播 / WebRTC / 磁力", "scene": "res://src/mediaplayer/main.tscn"},
]

var _toast: Label
var _dragging := false
var _drag_offset := Vector2i.ZERO
var _last_tool := ""             # 上次使用的工具 (卡片高亮 + 排最前)
var _single_card := false        # 只有一个工具时卡片通栏
const HUB_CFG := "user://hub.cfg"

func _ready() -> void:
	DisplayServer.window_set_title("avox 工具箱")
	theme = UiKit.build_theme()   # 统一字体 (Noto Sans SC/系统回退) + 基准字号
	_load_hub_cfg()
	_build_ui()
	_set_launcher_window()
	if OS.has_environment("HUB_SHOT"):
		# 临时诊断截图: HUB_SHOT=1 运行 → 0.5s 后打印窗口/布局信息, 抓 viewport 存 PNG 并退出
		await get_tree().create_timer(0.5).timeout
		print("DBG pos=", DisplayServer.window_get_position(), " size=", DisplayServer.window_get_size(), " screen=", DisplayServer.screen_get_size(DisplayServer.window_get_current_screen()))
		var stack: Array = [[self, 0]]
		while not stack.is_empty():
			var entry: Array = stack.pop_back()
			var node: Node = entry[0]
			var depth: int = entry[1]
			for c in node.get_children():
				if c is Control:
					var ctl := c as Control
					print("DBG ", "  ".repeat(depth), c.name, " rect=", ctl.get_rect(), " min=", ctl.get_combined_minimum_size())
					stack.append([c, depth + 1])
		var img := get_viewport().get_texture().get_image()
		img.save_png("user://hub_shot.png")
		print("DBG shot=", ProjectSettings.globalize_path("user://hub_shot.png"))
		get_tree().quit()

# ── 无边框拖动 ──
func _on_bg_gui_input(ev: InputEvent) -> void:
	if ev is InputEventMouseButton and ev.button_index == MOUSE_BUTTON_LEFT:
		if ev.pressed:
			_dragging = true
			_drag_offset = DisplayServer.mouse_get_position() - DisplayServer.window_get_position()
		else:
			_dragging = false

func _process(_delta: float) -> void:
	if _dragging:
		if not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
			_dragging = false
			return
		DisplayServer.window_set_position(DisplayServer.mouse_get_position() - _drag_offset)

func _unhandled_input(event: InputEvent) -> void:
	# 无边框窗口没有系统关闭钮: Esc 退出 launcher
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		get_tree().quit()

# ── launcher 小窗口 ──
func _set_launcher_window() -> void:
	# 无边框 (无系统标题栏) + 小窗口; 节点属性不作用于 OS 窗口, 必须走 DisplayServer
	# 高 DPI: 总缩放 = 基准比 min(win/画布基准 content_scale_size) × factor。播放器窗口=基准尺寸,
	# 放大窗口即放大内容; hub 窗口 510x480 非基准, 若不钉基准, 画布会被 expand 撑到项目基准
	# 1920x1080, 460 宽内容只占窗口 ~36% (图标/卡片显小的根因)。窗口物理尺寸 ×sc 后把画布
	# 基准钉到 510x480, 总缩放即 = sc (1 设计px = sc 物理px), 与播放器表现一致。
	if DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN:
		return
	var wid := get_window().get_window_id()
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, true, wid)
	var sc := UiKit.desktop_scale(DisplayServer.window_get_current_screen())
	var win_size := Vector2i(int(510 * sc), int(480 * sc))
	DisplayServer.window_set_min_size(Vector2i(int(440 * sc), int(380 * sc)), wid)
	DisplayServer.window_set_size(win_size, wid)
	var screen := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
	DisplayServer.window_set_position((screen - win_size) / 2, wid)
	get_window().content_scale_size = Vector2i(510, 480)

# ── hub 配置 (上次工具) ──
func _load_hub_cfg() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(HUB_CFG) == OK:
		_last_tool = str(cfg.get_value("ui", "last_tool", ""))

func _save_hub_cfg() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("ui", "last_tool", _last_tool)
	cfg.save(HUB_CFG)

# ── 工具卡 ──
func _mk_card(tool: Dictionary) -> Button:
	var card := Button.new()
	# 双列网格单元 (单工具时通栏); 高度给图标留呼吸感
	card.custom_minimum_size = Vector2(460 if _single_card else 225, 140)
	card.focus_mode = Control.FOCUS_NONE
	var is_last: bool = tool["id"] == _last_tool
	var border := ACCENT if is_last else Color(1, 1, 1, 0.08)
	card.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.05), 12, border, 1))
	card.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.08), 12, ACCENT, 1))
	card.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.06), 12, ACCENT, 1))
	card.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	card.pressed.connect(_on_card.bind(tool))
	# hover 微抬 1.03 (与播放器图标按钮同语言, 强化"可点"暗示)
	card.pivot_offset = card.custom_minimum_size / 2.0
	card.mouse_entered.connect(func():
		var tw := card.create_tween()
		tw.tween_property(card, "scale", Vector2.ONE * 1.03, 0.12))
	card.mouse_exited.connect(func():
		var tw := card.create_tween()
		tw.tween_property(card, "scale", Vector2.ONE, 0.12))
	var box := VBoxContainer.new()
	# 铺满卡片 + 内容居中: PRESET_CENTER 在 box 为空时只锚到中心点, 内容变多后会向右下溢出卡片
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override("separation", 6)
	card.add_child(box)
	# 矢量图标 (替代 emoji 字符: 跨平台渲染一致); 卡片主视觉, 占位要足
	var glyph := IconView.new()
	glyph.icon_kind = tool["icon"]
	glyph.icon_ratio = 0.58
	glyph.icon_color = ACCENT
	glyph.custom_minimum_size = Vector2(64, 64)
	glyph.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	glyph.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(glyph)
	var title := Label.new()
	title.text = tool["title"]
	if is_last:
		title.text += "  ·  上次"
	title.add_theme_font_override("font", UiKit.font_weight(500))
	title.add_theme_font_size_override("font_size", 14)
	title.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(title)
	var desc := Label.new()
	desc.text = tool["desc"]
	desc.add_theme_font_size_override("font_size", 12)
	desc.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	desc.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	desc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(desc)
	return card

func _on_card(tool: Dictionary) -> void:
	_last_tool = tool["id"]
	_save_hub_cfg()
	if not ResourceLoader.exists(tool["scene"]):
		_toast_msg("%s: 工具场景不存在" % tool["title"])
		return
	# launcher 无边框小窗 → 进入工具恢复常规窗口 (1280x720, 有标题栏)
	var wid := get_window().get_window_id()
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, false, wid)
	DisplayServer.window_set_size(Vector2i(1280, 720), wid)
	DisplayServer.window_set_position((DisplayServer.screen_get_size(DisplayServer.window_get_current_screen()) - Vector2i(1280, 720)) / 2, wid)
	# 画布基准还原 1280: 工具场景均按 1280 设计并在各自 _ready 钉定, 此处先行同步防切景闪动
	get_window().content_scale_size = Vector2i(1280, 720)
	get_tree().change_scene_to_file(tool["scene"])

# ── UI 构建 ──
func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = BG_COLOR
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.gui_input.connect(_on_bg_gui_input)   # 无边框: 按住空白拖动
	add_child(bg)
	# 顶部 accent 光晕 (径向渐变, 打破大平灰)
	var glow := TextureRect.new()
	glow.set_anchors_preset(Control.PRESET_TOP_WIDE)
	glow.offset_bottom = 260
	var gg := Gradient.new()
	gg.offsets = PackedFloat32Array([0.0, 1.0])
	gg.colors = PackedColorArray([Color(ACCENT, 0.07), Color(ACCENT, 0.0)])
	var ggt := GradientTexture2D.new()
	ggt.gradient = gg
	ggt.fill = GradientTexture2D.FILL_RADIAL
	ggt.fill_from = Vector2(0.5, -0.6)
	ggt.fill_to = Vector2(0.5, 1.1)
	glow.texture = ggt
	glow.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(glow)

	# 无边框窗口没有系统关闭钮 → 右上角 ✕ (矢量)
	var close_btn := IconButton.new()
	close_btn.icon_kind = IconButton.Icon.CLOSE
	close_btn.icon_ratio = 0.4
	close_btn.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	close_btn.offset_left = -42
	close_btn.offset_top = 10
	close_btn.offset_right = -14
	close_btn.offset_bottom = 36
	close_btn.focus_mode = Control.FOCUS_NONE
	close_btn.add_theme_color_override("font_color", Color(0.7, 0.74, 0.8))
	close_btn.add_theme_color_override("font_hover_color", Color.WHITE)
	close_btn.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
	close_btn.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.12), 6))
	close_btn.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.08), 6))
	close_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	close_btn.tooltip_text = "退出 (Esc)"
	close_btn.pressed.connect(func(): get_tree().quit())
	add_child(close_btn)

	# CenterContainer 全屏自动居中 (比 PRESET_CENTER + anchor 更稳, 窗口 resize 不跑偏)
	var cc := CenterContainer.new()
	cc.set_anchors_preset(Control.PRESET_FULL_RECT)
	cc.mouse_filter = Control.MOUSE_FILTER_IGNORE   # 空白处事件透到 bg 拖动
	add_child(cc)
	var center := VBoxContainer.new()
	center.custom_minimum_size = Vector2(460, 0)
	center.add_theme_constant_override("separation", 12)
	cc.add_child(center)

	var title := Label.new()
	title.text = "avox 工具箱"
	title.add_theme_font_override("font", UiKit.font_weight(600))
	title.add_theme_font_size_override("font_size", 28)
	title.add_theme_color_override("font_color", Color.WHITE)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.mouse_filter = Control.MOUSE_FILTER_IGNORE
	center.add_child(title)

	var sub := Label.new()
	sub.text = "媒体播放 · 直播 · WebRTC"
	sub.add_theme_font_size_override("font_size", 13)
	sub.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
	center.add_child(sub)

	# 工具卡网格: 上次使用的工具排最前 (高频路径)
	var cards := GridContainer.new()
	var ordered := (_tools.duplicate()) as Array
	if not _last_tool.is_empty():
		for i in ordered.size():
			if ordered[i]["id"] == _last_tool and i > 0:
				var first = ordered.pop_at(i)
				ordered.push_front(first)
				break
	_single_card = ordered.size() == 1
	cards.columns = 1 if _single_card else 2
	cards.add_theme_constant_override("h_separation", 10)
	cards.add_theme_constant_override("v_separation", 10)
	center.add_child(cards)
	for tool in ordered:
		cards.add_child(_mk_card(tool))

	# 底部版本行 (avox.dll 修改日期, 排障对版本用)
	var footer := Label.new()
	footer.text = "avox_godot · GDExtension · avox.dll %s" % _avox_build_date()
	footer.add_theme_font_size_override("font_size", 12)
	footer.add_theme_color_override("font_color", Color(0.4, 0.44, 0.5))
	footer.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	footer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	center.add_child(footer)

	_toast = Label.new()
	_toast.add_theme_font_size_override("font_size", 13)
	_toast.add_theme_color_override("font_color", Color(1.0, 0.85, 0.55))
	_toast.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.7))
	_toast.add_theme_constant_override("shadow_offset_y", 1)
	_toast.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_toast.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_toast.offset_top = -96
	_toast.offset_bottom = -68
	_toast.modulate.a = 0.0
	add_child(_toast)

# ── 工具 ──
func _avox_build_date() -> String:
	# avox.dll 修改时间 (排障对版本用); 取不到就省略
	for p in ["res://addons/avox_godot/bin/avox.dll", "user://avox.dll"]:
		if FileAccess.file_exists(p):
			var t := FileAccess.get_modified_time(p)
			var d := Time.get_datetime_dict_from_unix_time(t)
			return "%04d-%02d-%02d" % [d.year, d.month, d.day]
	return ""

func _flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	return UiKit.flat(bg, radius, border, bw)

func _toast_msg(msg: String) -> void:
	_toast.text = msg
	_toast.modulate.a = 1.0
	var tw := create_tween()
	tw.tween_interval(2.5)
	tw.tween_property(_toast, "modulate:a", 0.0, 0.4)
