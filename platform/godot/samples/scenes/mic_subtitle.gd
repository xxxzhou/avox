extends Control
## 04 语音字幕 —— 麦克风 → SttNode 流式识别, 实时上屏。
## 依赖: STT 模型 (sherpa_zh_en) 已部署, 见 addons/avox_godot/bin/README.txt。

const BG := Color("0b0f16")
const ACCENT := Color("3b82f6")
const TEXT_DIM := Color("8b93a3")

var mic: MicCapture
var stt: SttNode
var device_opt: OptionButton
var start_btn: Button
var status_label: Label
var transcript: RichTextLabel
var level_bar: ProgressBar
var finals: PackedStringArray = []
var running := false

func _ready() -> void:
	_build_ui()
	if not ClassDB.class_exists("SttNode"):
		_set_status("未检测到 avox_godot 插件 —— 请先部署 (见 README)", true)
		return
	stt = SttNode.new()
	stt.set_model_level(2)
	add_child(stt)
	stt.partial_result.connect(_on_partial)
	stt.final_result.connect(_on_final)
	stt.stt_ready.connect(func() -> void: _set_status("识别就绪, 说话即可"))
	stt.error.connect(func(msg: String) -> void: _set_status("STT: " + msg, true))
	mic = MicCapture.new()
	add_child(mic)
	_refresh_devices()

func _exit_tree() -> void:
	if running:
		mic.stop()
		stt.stop()

func _refresh_devices() -> void:
	device_opt.clear()
	var devices: PackedStringArray = mic.list_devices()
	for i in devices.size():
		device_opt.add_item(devices[i], i)
	if devices.is_empty():
		_set_status("未找到麦克风", true)
	else:
		_set_status("选择麦克风后点开始")

func _start() -> void:
	if device_opt.item_count == 0:
		return
	mic.set_device_index(device_opt.get_selected_id())
	mic.forward_to(stt)
	stt.start()
	mic.start()
	running = true
	start_btn.text = "停止"
	start_btn.disabled = true
	_set_status("模型加载中…")

func _stop() -> void:
	mic.stop()
	stt.stop()
	running = false
	start_btn.text = "开始"
	start_btn.disabled = false
	_set_status("已停止")

func _toggle() -> void:
	if running:
		_stop()
	else:
		_start()

func _process(_delta: float) -> void:
	if not running:
		return
	level_bar.value = mic.get_audio_level() * 100.0
	if stt.loading():
		_set_status("模型加载中…")
	elif start_btn.disabled:
		start_btn.disabled = false

func _on_partial(text: String) -> void:
	_render(text)

func _on_final(text: String) -> void:
	finals.append(text)
	_render("")

func _render(partial: String) -> void:
	transcript.clear()
	for line in finals:
		transcript.append_text(line + "\n")
	if not partial.is_empty():
		transcript.append_text("[color=#8b93a3]" + partial + "[/color]")

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
	title.text = "  04 语音字幕 (MicCapture + SttNode)"
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
	start_btn = Button.new()
	start_btn.text = "开始"
	start_btn.pressed.connect(_toggle)
	row.add_child(start_btn)
	root.add_child(row)
	var level_label := Label.new()
	level_label.text = "音量"
	level_label.add_theme_color_override("font_color", TEXT_DIM)
	root.add_child(level_label)
	level_bar = ProgressBar.new()
	level_bar.min_value = 0.0
	level_bar.max_value = 100.0
	level_bar.show_percentage = false
	level_bar.custom_minimum_size = Vector2(0, 8)
	root.add_child(level_bar)
	transcript = RichTextLabel.new()
	transcript.bbcode_enabled = true
	transcript.scroll_following = true
	transcript.size_flags_vertical = Control.SIZE_EXPAND_FILL
	transcript.add_theme_font_size_override("normal_font_size", 20)
	root.add_child(transcript)
	status_label = Label.new()
	status_label.text = ""
	status_label.add_theme_font_size_override("font_size", 13)
	root.add_child(status_label)
