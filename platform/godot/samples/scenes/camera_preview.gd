extends Control
## 03 相机采集 —— DeviceManager 枚举设备, SourcePlayer 实时预览。

const BG := Color("0b0f16")
const ACCENT := Color("3b82f6")
const TEXT_DIM := Color("8b93a3")

var source: SourcePlayer
var video: TextureRect
var device_opt: OptionButton
var status_label: Label
var info_label: Label

func _ready() -> void:
	_build_ui()
	if not ClassDB.class_exists("SourcePlayer"):
		_set_status("未检测到 avox_godot 插件 —— 请先部署 (见 README)", true)
		return
	_refresh_devices()

func _exit_tree() -> void:
	if source != null:
		source.close()

func _refresh_devices() -> void:
	device_opt.clear()
	var devices: Array = DeviceManager.list_video_devices()
	for i in devices.size():
		var d = devices[i]
		var device_name := str(d)
		if d is Dictionary and d.has("name"):
			device_name = str(d["name"])
		device_opt.add_item(device_name, i)
	if devices.is_empty():
		_set_status("未找到视频采集设备", true)
	else:
		_set_status("找到 %d 个设备, 选择后点打开" % devices.size())

func _open() -> void:
	if device_opt.item_count == 0:
		return
	if source == null:
		source = SourcePlayer.new()
		add_child(source)
		source.state_changed.connect(func(state: int) -> void: _set_status("SourcePlayer 状态: %d" % state))
		source.io_error.connect(func(code: int) -> void: _set_status("IO 错误 code=%d" % code, true))
	source.set_video_source_index(device_opt.get_selected_id())
	source.open()

func _close() -> void:
	if source != null:
		source.close()
	video.texture = null
	_set_status("已关闭")

func _process(_delta: float) -> void:
	if source != null and source.is_open():
		video.texture = source.get_texture()

func _set_status(text: String, is_error := false) -> void:
	status_label.text = text
	status_label.add_theme_color_override("font_color", Color("ef4444") if is_error else ACCENT)

# ── UI ──

func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 6)
	add_child(root)
	var top := HBoxContainer.new()
	var back := Button.new()
	back.text = " ← 主页 "
	back.pressed.connect(func() -> void: get_tree().change_scene_to_file("res://scenes/hub.tscn"))
	top.add_child(back)
	var title := Label.new()
	title.text = "  03 相机采集 (SourcePlayer)"
	title.add_theme_font_size_override("font_size", 16)
	top.add_child(title)
	root.add_child(top)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	device_opt = OptionButton.new()
	device_opt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(device_opt)
	var refresh_btn := Button.new()
	refresh_btn.text = "刷新设备"
	refresh_btn.pressed.connect(_refresh_devices)
	row.add_child(refresh_btn)
	var open_btn := Button.new()
	open_btn.text = "打开"
	open_btn.pressed.connect(_open)
	row.add_child(open_btn)
	var close_btn := Button.new()
	close_btn.text = "关闭"
	close_btn.pressed.connect(_close)
	row.add_child(close_btn)
	root.add_child(row)
	video = TextureRect.new()
	video.size_flags_vertical = Control.SIZE_EXPAND_FILL
	video.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	root.add_child(video)
	status_label = Label.new()
	status_label.text = "枚举设备中…"
	status_label.add_theme_font_size_override("font_size", 13)
	root.add_child(status_label)
	info_label = Label.new()
	info_label.text = ""
	info_label.add_theme_font_size_override("font_size", 11)
	info_label.add_theme_color_override("font_color", TEXT_DIM)
	root.add_child(info_label)
