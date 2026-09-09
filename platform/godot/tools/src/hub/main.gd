extends Control
## avox 工具主界面 (hub) —— 工具 launcher
## 工具列表数据驱动 (加工具 = 加一条 _tools); 语音输入依赖 STT 模型, 缺失自动弹窗提示下载。
## 语音输入 UI 由另一会话开发, 本 hub 只提供入口 + 模型检查/下载; 场景未交付时 toast 提示。
## 依赖: addons/avox_godot 已部署 + AssetManager autoload (project.godot)。

const ACCENT   := Color("#3B82F6")
const PANEL_BG := Color(0.08, 0.10, 0.14, 0.85)
const BG_COLOR := Color("#0B0F16")
const OK_COLOR   := Color(0.35, 0.82, 0.55)
const WARN_COLOR := Color(0.95, 0.75, 0.25)
# ── 共享 UI 基建 (主题字体/高 DPI 缩放/flat stylebox, 与 mediaplayer 共用) ──
const UiKit := preload("res://src/common/ui_kit.gd")

const _tools := [
	{"id": "player", "icon": IconButton.Icon.PLAY, "title": "播放器",   "desc": "沉浸式视频 / 直播", "scene": "res://src/mediaplayer/main.tscn"},
	{"id": "agent",  "icon": IconButton.Icon.BOT,  "title": "Agent",    "desc": "LLM 对话助手",       "scene": "res://src/agent/main.tscn"},
	{"id": "face",   "icon": IconButton.Icon.FACE, "title": "视频驱动 Avatar", "desc": "视频→ARKit52 驱动 3D 模型", "scene": "res://src/avatar/main.tscn"},
	{"id": "voice",  "icon": IconButton.Icon.MIC,  "title": "语音输入", "desc": "STT 模型依赖",       "scene": "res://src/voiceinput/voice_input.tscn", "asset": "sherpa_zh_en"},
]

var _res_list: VBoxContainer
var _deploy_note: Label
var _res_rows := {}        # item_id -> {"status": Label, "dl": Button}
var _card_status := {}     # tool_id -> Label
var _toast: Label
var _dlg: ConfirmationDialog
var _prompted_voice := false
var _dragging := false
var _drag_offset := Vector2i.ZERO
var _res_expanded := false       # 资源面板展开态 (launcher 默认折叠, 点开才见明细)
var _res_panel: PanelContainer
var _res_toggle: Button
var _res_summary := "资源状态"   # 折叠头汇总 (refresh 时更新)
var _last_tool := ""             # 上次使用的工具 (卡片高亮 + 排最前)
const HUB_CFG := "user://hub.cfg"

func _ready() -> void:
	DisplayServer.window_set_title("avox 工具箱")
	theme = UiKit.build_theme()   # 统一字体 (Noto Sans SC/系统回退) + 基准字号
	_load_hub_cfg()
	_build_ui()
	_set_launcher_window()
	AssetManager.download_started.connect(_on_download_started)
	AssetManager.download_finished.connect(_on_download_finished)
	AssetManager.check_failed.connect(func(m: String): _toast_msg("资源检查失败: %s" % m))
	_refresh_resources()
	_update_voice_card()
	# 走查: -- --voice 直接以生产路径 (hub 子窗口) 打开语音输入
	for a in OS.get_cmdline_user_args():
		if a == "--voice":
			var sc := load("res://src/voiceinput/voice_input.tscn") as PackedScene
			if sc:
				add_child(sc.instantiate())
			break
	if AssetManager.is_downloading(AssetManager.VOICE_MODEL_ID):
		_on_download_started(AssetManager.VOICE_MODEL_ID)
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

# ── 无边框拖动 (参照 voiceinput) ──
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
func _launcher_height() -> int:
	return 660 if _res_expanded else 480   # 资源面板折叠时窗口收矮

func _set_launcher_window() -> void:
	# 参照 voiceinput: 无边框 (无系统标题栏) + 小窗口; 节点属性不作用于 OS 窗口, 必须走 DisplayServer
	# 高 DPI: 总缩放 = 基准比 min(win/画布基准 content_scale_size) × factor。播放器窗口=基准尺寸,
	# 放大窗口即放大内容; hub 窗口 510x480 非基准, 若不钉基准, 画布会被 expand 撑到项目基准
	# 1920x1080, 460 宽内容只占窗口 ~36% (图标/卡片显小的根因)。窗口物理尺寸 ×sc 后把画布
	# 基准钉到 510x480, 总缩放即 = sc (1 设计px = sc 物理px), 与播放器表现一致。
	if DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN:
		return
	var wid := get_window().get_window_id()
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, true, wid)
	# 双列 2x2 卡 + 折叠资源面板: 一屏收完, launcher 主任务 (点工具) 不受资源明细挤占
	var sc := UiKit.desktop_scale(DisplayServer.window_get_current_screen())
	var win_size := Vector2i(int(510 * sc), int(_launcher_height() * sc))
	DisplayServer.window_set_min_size(Vector2i(int(440 * sc), int(380 * sc)), wid)
	DisplayServer.window_set_size(win_size, wid)
	var screen := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
	DisplayServer.window_set_position((screen - win_size) / 2, wid)
	get_window().content_scale_size = Vector2i(510, 480)   # 展开态窗口变高, 高度方向由 expand 自动补

# ── hub 配置 (上次工具 / 资源面板展开态) ──
func _load_hub_cfg() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(HUB_CFG) == OK:
		_last_tool = str(cfg.get_value("ui", "last_tool", ""))
		_res_expanded = bool(cfg.get_value("ui", "res_expanded", false))

func _save_hub_cfg() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("ui", "last_tool", _last_tool)
	cfg.set_value("ui", "res_expanded", _res_expanded)
	cfg.save(HUB_CFG)

# ── 资源面板 ──
func _refresh_resources() -> void:
	for c in _res_list.get_children():
		_res_list.remove_child(c)
		c.queue_free()
	_res_rows.clear()
	var res := AssetManager.check_all()
	if res.is_empty():
		_deploy_note.text = "资源检查不可用 (插件未部署或缺少 python, 请先运行 plugin/deploy_godot.ps1)"
		_deploy_note.visible = true
		return
	var ready_n := 0
	var total_n := 0
	var missing := []
	for it in AssetManager.items():
		if it.get("method") != "download" or it.get("ready") == null:
			continue
		total_n += 1
		if it["ready"]:
			ready_n += 1
		else:
			missing.append(it)
	# 小窗口 launcher: 只列「缺失/下载中」可操作行, 其余收进汇总; 汇总常显在折叠头上
	_res_summary = "资源状态 · 共 %d 项 · 全部就绪" % total_n if missing.is_empty() \
		else "资源状态 · 共 %d 项 · 缺失 %d" % [total_n, missing.size()]
	_apply_res_expanded()
	if missing.is_empty():
		_deploy_note.text = "全部就绪"
	else:
		_deploy_note.text = "缺失 %d 项:" % missing.size()
		for it in missing:
			_add_res_row(it)
	_deploy_note.visible = _res_expanded   # 汇总行常显 (缺失/就绪都显示)
	_deploy_note.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))

func _add_res_row(it: Dictionary) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	var name_l := Label.new()
	name_l.text = it["name"]
	name_l.custom_minimum_size = Vector2(190, 0)
	name_l.add_theme_font_size_override("font_size", 13)
	name_l.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	name_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(name_l)
	var size_l := Label.new()
	size_l.text = _size_str(it)
	size_l.custom_minimum_size = Vector2(46, 0)
	size_l.add_theme_font_size_override("font_size", 12)
	size_l.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	size_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(size_l)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(spacer)
	var status_l := Label.new()
	status_l.text = "就绪" if it["ready"] else "缺失"
	status_l.add_theme_font_size_override("font_size", 12)
	status_l.add_theme_color_override("font_color", OK_COLOR if it["ready"] else WARN_COLOR)
	status_l.custom_minimum_size = Vector2(36, 0)
	status_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(status_l)
	var dl := Button.new()
	dl.text = "下载"
	dl.visible = not it["ready"]
	dl.custom_minimum_size = Vector2(50, 26)
	dl.focus_mode = Control.FOCUS_NONE
	dl.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.08), 5))
	dl.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 5))
	dl.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 5))
	dl.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	dl.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	dl.pressed.connect(func(): AssetManager.download_item(it["id"]))
	row.add_child(dl)
	_res_list.add_child(row)
	_res_rows[it["id"]] = {"status": status_l, "dl": dl}

func _size_str(it: Dictionary) -> String:
	var files = it.get("files", [])
	if files is Array and files.size() > 0 and files[0] is Dictionary:
		var mb: float = files[0].get("size_mb", 0.0)
		if mb > 0.0:
			return "%.0fMB" % mb
	return ""

# ── 语音卡: 模型状态 + 缺失自动弹窗 ──
func _update_voice_card() -> void:
	if not _card_status.has("voice"):
		return
	var lab: Label = _card_status["voice"]
	var it := AssetManager.check_item(AssetManager.VOICE_MODEL_ID)
	if AssetManager.is_downloading(AssetManager.VOICE_MODEL_ID):
		lab.text = "● 下载中…"
		lab.add_theme_color_override("font_color", ACCENT)
	elif it.is_empty() or it.get("ready") == null:
		lab.text = "● 状态未知"
		lab.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	elif it["ready"]:
		lab.text = "● 模型就绪"
		lab.add_theme_color_override("font_color", OK_COLOR)
	else:
		lab.text = "● 模型缺失"
		lab.add_theme_color_override("font_color", WARN_COLOR)
		if not _prompted_voice:
			_prompted_voice = true
			_prompt_download()

func _prompt_download() -> void:
	if _dlg and _dlg.visible:
		return
	_dlg = ConfirmationDialog.new()
	_dlg.title = "下载 STT 模型"
	_dlg.dialog_text = "语音输入依赖 STT 模型 (sherpa_zh_en, 约168MB) 未下载。\n立即下载? 下载在后台进行, 不阻塞界面。"
	_dlg.ok_button_text = "下载"
	_dlg.cancel_button_text = "稍后"
	_dlg.confirmed.connect(func(): AssetManager.download_item(AssetManager.VOICE_MODEL_ID))
	add_child(_dlg)
	_dlg.popup_centered()

# ── 下载状态刷新 ──
func _on_download_started(item_id: String) -> void:
	if _res_rows.has(item_id):
		_res_rows[item_id]["status"].text = "下载中…"
		_res_rows[item_id]["status"].add_theme_color_override("font_color", ACCENT)
		_res_rows[item_id]["dl"].visible = false
	if item_id == AssetManager.VOICE_MODEL_ID and _card_status.has("voice"):
		_card_status["voice"].text = "● 下载中…"
		_card_status["voice"].add_theme_color_override("font_color", ACCENT)

func _on_download_finished(item_id: String, ok: bool, msg: String) -> void:
	_toast_msg("下载完成: %s" % msg if ok else "下载失败: %s" % msg)
	_refresh_resources()
	_update_voice_card()

# ── 工具卡 ──
func _mk_card(tool: Dictionary) -> Button:
	var card := Button.new()
	# 双列网格单元 (与资源面板同一 460 列内 2 列); 高度给图标留呼吸感
	card.custom_minimum_size = Vector2(225, 140)
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
	if tool.has("asset"):
		var st := Label.new()
		st.text = "…"
		st.add_theme_font_size_override("font_size", 11)
		st.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		st.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.add_child(st)
		_card_status[tool["id"]] = st
	return card

func _on_card(tool: Dictionary) -> void:
	_last_tool = tool["id"]
	_save_hub_cfg()
	if tool.has("asset"):
		# 模型缺失时点语音卡 → 弹下载 (而非切到不可用的场景)
		var it := AssetManager.check_item(tool["asset"])
		if it.get("ready") == false and not AssetManager.is_downloading(tool["asset"]):
			_prompt_download()
			return
	if not ResourceLoader.exists(tool["scene"]):
		_toast_msg("%s: 工具场景开发中 (由另一会话交付)" % tool["title"])
		return
	# VoiceInput 根节点是 Window, change_scene_to_file 会替换主场景根导致窗口冲突卡死;
	# 实例化 add_child 即可: Window 子节点自动弹出独立 OS 窗口, 主窗口不变。
	if tool["id"] == "voice":
		var scene := load(tool["scene"]) as PackedScene
		if scene:
			var vi := scene.instantiate()
			add_child(vi)
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
	sub.text = "播放器 · Agent · 语音输入 · 运行时资源"
	sub.add_theme_font_size_override("font_size", 13)
	sub.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
	center.add_child(sub)

	# 双列 2x2 网格: 上次使用的工具排最前 (高频路径)
	var cards := GridContainer.new()
	cards.columns = 2
	cards.add_theme_constant_override("h_separation", 10)
	cards.add_theme_constant_override("v_separation", 10)
	center.add_child(cards)
	var ordered := (_tools.duplicate()) as Array
	if not _last_tool.is_empty():
		for i in ordered.size():
			if ordered[i]["id"] == _last_tool and i > 0:
				var first = ordered.pop_at(i)
				ordered.push_front(first)
				break
	for tool in ordered:
		cards.add_child(_mk_card(tool))

	var panel := PanelContainer.new()
	_res_panel = panel
	panel.add_theme_stylebox_override("panel", _flat(PANEL_BG, 12, Color(1, 1, 1, 0.08), 1))
	center.add_child(panel)
	var p_inner := MarginContainer.new()
	p_inner.add_theme_constant_override("margin_left", 16)
	p_inner.add_theme_constant_override("margin_right", 16)
	p_inner.add_theme_constant_override("margin_top", 10)
	p_inner.add_theme_constant_override("margin_bottom", 12)
	panel.add_child(p_inner)
	var p_box := VBoxContainer.new()
	p_box.add_theme_constant_override("separation", 6)
	p_inner.add_child(p_box)
	# 折叠头: 点击展开/收起明细 (launcher 默认收起, 主任务是点工具)
	_res_toggle = Button.new()
	_res_toggle.focus_mode = Control.FOCUS_NONE
	_res_toggle.add_theme_font_size_override("font_size", 13)
	_res_toggle.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	_res_toggle.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
	_res_toggle.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.05), 6))
	_res_toggle.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.03), 6))
	_res_toggle.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_res_toggle.pressed.connect(_toggle_res_panel)
	p_box.add_child(_res_toggle)
	_deploy_note = Label.new()
	_deploy_note.add_theme_font_size_override("font_size", 12)
	_deploy_note.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	_deploy_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_deploy_note.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_deploy_note.visible = false
	p_box.add_child(_deploy_note)
	_res_list = VBoxContainer.new()
	_res_list.add_theme_constant_override("separation", 4)
	p_box.add_child(_res_list)
	_apply_res_expanded()

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

# 折叠头文案 + 明细显隐; 窗口高度跟随 ( DisplayServer 物理像素 × DPI)
func _apply_res_expanded() -> void:
	if _res_toggle == null:
		return
	_res_toggle.text = ("▼ " if _res_expanded else "▶ ") + _res_summary
	_res_list.visible = _res_expanded
	_deploy_note.visible = _res_expanded
	if DisplayServer.window_get_mode() != DisplayServer.WINDOW_MODE_FULLSCREEN:
		var wid := get_window().get_window_id()
		var sc := UiKit.desktop_scale(DisplayServer.window_get_current_screen())
		var win_size := Vector2i(int(510 * sc), int(_launcher_height() * sc))
		DisplayServer.window_set_size(win_size, wid)
		var screen := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
		DisplayServer.window_set_position((screen - win_size) / 2, wid)

func _toggle_res_panel() -> void:
	_res_expanded = not _res_expanded
	_save_hub_cfg()
	_apply_res_expanded()

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
