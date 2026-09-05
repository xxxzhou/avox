extends Control
## 01 本地播放 —— 文件选择 → MediaPlayer 硬解 + GPU 直通, 播放控制与媒体信息。

const BG := Color("0b0f16")
const ACCENT := Color("3b82f6")
const TEXT_DIM := Color("8b93a3")
const STATE_NAMES := {0: "空闲", 1: "打开中", 2: "就绪", 3: "播放中", 4: "暂停", 5: "跳转", 6: "缓冲", 7: "已停止", 8: "播放完成"}
const SPEEDS := [0.5, 1.0, 1.5, 2.0]

var player: MediaPlayer
var video: TextureRect
var status_label: Label
var info_label: Label
var pos_label: Label
var fps_label: Label
var progress: HSlider
var play_btn: Button
var volume_slider: HSlider
var hard_cb: CheckBox
var gpu_cb: CheckBox
var speed_opt: OptionButton
var seeking := false

func _ready() -> void:
	_build_ui()
	if not ClassDB.class_exists("MediaPlayer"):
		_set_status("未检测到 avox_godot 插件 —— 请先部署 (见 README)", true)
		return
	player = MediaPlayer.new()
	add_child(player)
	player.state_changed.connect(_on_state_changed)
	player.completed.connect(func() -> void: _set_status("播放完成"))
	player.io_error.connect(func(code: int) -> void: _set_status("IO 错误 code=%d" % code, true))
	player.decode_error.connect(func(code: int) -> void: _set_status("解码错误 code=%d" % code, true))
	player.set_hard_decode(hard_cb.button_pressed)
	player.set_gpu_passthrough(gpu_cb.button_pressed)
	player.set_volume(volume_slider.value / 100.0)

func _process(_delta: float) -> void:
	if player == null:
		return
	video.texture = player.get_texture()
	var duration := player.get_duration()
	pos_label.text = "%s / %s" % [_fmt_ms(player.get_position()), _fmt_ms(duration)]
	fps_label.text = "%d fps" % player.get_fps()
	if not seeking and duration > 0.0:
		progress.set_value_no_signal(player.get_progress() * 1000.0)

func _open_file() -> void:
	var dialog := FileDialog.new()
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dialog.filters = PackedStringArray(["*.mp4 ; MP4", "*.mkv ; MKV", "*.flv ; FLV", "*.mov ; MOV", "*.ts ; TS", "*.* ; 所有文件"])
	dialog.file_selected.connect(_play_file)
	add_child(dialog)
	dialog.popup_centered(Vector2i(900, 620))

func _play_file(path: String) -> void:
	_set_status("打开: " + path)
	player.play(path)

func _on_state_changed(state: int) -> void:
	_set_status(String(STATE_NAMES.get(state, str(state))))
	if state == 2 or state == 3:
		play_btn.text = "暂停"
		_refresh_info()
	elif state == 4:
		play_btn.text = "继续"

func _refresh_info() -> void:
	info_label.text = JSON.stringify(player.get_media_info(), "  ")

func _toggle_play() -> void:
	if player == null:
		return
	var s := player.get_state()
	if s == 4:
		player.resume()
	elif s == 3:
		player.pause()

func _on_seek_started() -> void:
	seeking = true

func _on_seek_ended(changed: bool) -> void:
	seeking = false
	if changed and player != null and player.get_duration() > 0.0:
		player.seek(int(progress.value / 1000.0 * player.get_duration()))

func _on_volume_changed(value: float) -> void:
	if player != null:
		player.set_volume(value / 100.0)

func _on_speed_selected(index: int) -> void:
	if player != null:
		player.set_speed(SPEEDS[index])

func _set_status(text: String, is_error := false) -> void:
	status_label.text = text
	status_label.add_theme_color_override("font_color", Color("ef4444") if is_error else ACCENT)

func _fmt_ms(ms: float) -> String:
	var total := int(ms / 1000.0)
	var m := int(floor(total / 60.0))
	return "%02d:%02d" % [m, total % 60]

# ── UI (代码构建, .tscn 只挂根节点) ──

func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 6)
	add_child(root)
	# 顶栏
	var top := HBoxContainer.new()
	var back := Button.new()
	back.text = " ← 主页 "
	back.pressed.connect(func() -> void: get_tree().change_scene_to_file("res://scenes/hub.tscn"))
	top.add_child(back)
	var title := Label.new()
	title.text = "  01 本地播放 (硬解 + GPU 直通)"
	title.add_theme_font_size_override("font_size", 16)
	top.add_child(title)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(spacer)
	var open_btn := Button.new()
	open_btn.text = "打开文件…"
	open_btn.pressed.connect(_open_file)
	top.add_child(open_btn)
	root.add_child(top)
	# 视频区
	video = TextureRect.new()
	video.size_flags_vertical = Control.SIZE_EXPAND_FILL
	video.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	root.add_child(video)
	# 进度行
	var prow := HBoxContainer.new()
	pos_label = Label.new()
	pos_label.text = "00:00 / 00:00"
	pos_label.custom_minimum_size = Vector2(110, 0)
	prow.add_child(pos_label)
	progress = HSlider.new()
	progress.min_value = 0.0
	progress.max_value = 1000.0
	progress.step = 1.0
	progress.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	progress.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	progress.drag_started.connect(_on_seek_started)
	progress.drag_ended.connect(_on_seek_ended)
	prow.add_child(progress)
	fps_label = Label.new()
	fps_label.text = ""
	fps_label.custom_minimum_size = Vector2(64, 0)
	fps_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	prow.add_child(fps_label)
	root.add_child(prow)
	# 控制行
	var crow := HBoxContainer.new()
	crow.add_theme_constant_override("separation", 10)
	play_btn = Button.new()
	play_btn.text = "暂停"
	play_btn.pressed.connect(_toggle_play)
	crow.add_child(play_btn)
	var stop_btn := Button.new()
	stop_btn.text = "停止"
	stop_btn.pressed.connect(func() -> void:
		if player != null:
			player.stop())
	crow.add_child(stop_btn)
	var speed_label := Label.new()
	speed_label.text = " 倍速"
	crow.add_child(speed_label)
	speed_opt = OptionButton.new()
	for i in SPEEDS.size():
		speed_opt.add_item("%sx" % SPEEDS[i], i)
	speed_opt.select(1)
	speed_opt.item_selected.connect(_on_speed_selected)
	crow.add_child(speed_opt)
	var vol_label := Label.new()
	vol_label.text = " 音量"
	crow.add_child(vol_label)
	volume_slider = HSlider.new()
	volume_slider.min_value = 0.0
	volume_slider.max_value = 100.0
	volume_slider.value = 100.0
	volume_slider.custom_minimum_size = Vector2(120, 0)
	volume_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	volume_slider.value_changed.connect(_on_volume_changed)
	crow.add_child(volume_slider)
	hard_cb = CheckBox.new()
	hard_cb.text = "硬解"
	hard_cb.button_pressed = true
	crow.add_child(hard_cb)
	gpu_cb = CheckBox.new()
	gpu_cb.text = "GPU直通 (下次播放生效)"
	gpu_cb.button_pressed = true
	crow.add_child(gpu_cb)
	root.add_child(crow)
	status_label = Label.new()
	status_label.text = "选择一个视频文件开始"
	status_label.add_theme_font_size_override("font_size", 13)
	root.add_child(status_label)
	info_label = Label.new()
	info_label.text = ""
	info_label.add_theme_font_size_override("font_size", 11)
	info_label.add_theme_color_override("font_color", TEXT_DIM)
	root.add_child(info_label)
