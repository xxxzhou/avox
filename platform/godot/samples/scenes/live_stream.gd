extends Control
## 02 直播流 —— RTMP / HLS / RTSP 网络流播放, 状态与媒体信息展示。

const BG := Color("0b0f16")
const ACCENT := Color("3b82f6")
const TEXT_DIM := Color("8b93a3")
const STATE_NAMES := {0: "空闲", 1: "打开中", 2: "就绪", 3: "播放中", 4: "暂停", 5: "跳转", 6: "缓冲", 7: "已停止", 8: "播放完成"}

const PRESETS := [
	{"name": "公网 HLS 测试流", "url": "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"},
	{"name": "局域网 RTMP (自建)", "url": "rtmp://192.168.1.100/live/stream"},
	{"name": "RTSP 摄像头 (示例占位)", "url": "rtsp://your-camera-ip:554/stream"},
]

var player: MediaPlayer
var video: TextureRect
var url_edit: LineEdit
var status_label: Label
var info_label: Label
var fps_label: Label
var play_btn: Button

func _ready() -> void:
	_build_ui()
	if not ClassDB.class_exists("MediaPlayer"):
		_set_status("未检测到 avox_godot 插件 —— 请先部署 (见 README)", true)
		return
	player = MediaPlayer.new()
	add_child(player)
	player.state_changed.connect(_on_state_changed)
	player.io_error.connect(func(code: int) -> void: _set_status("IO 错误 code=%d (检查地址/网络/防火墙)" % code, true))
	player.decode_error.connect(func(code: int) -> void: _set_status("解码错误 code=%d" % code, true))

func _process(_delta: float) -> void:
	if player == null:
		return
	video.texture = player.get_texture()
	if player.get_state() == 3:
		fps_label.text = "%d fps" % player.get_fps()

func _start() -> void:
	var url := url_edit.text.strip_edges()
	if url.is_empty():
		_set_status("请先填入播放地址", true)
		return
	_set_status("连接中: " + url)
	player.play(url)

func _stop() -> void:
	if player != null:
		player.stop()
	_set_status("已停止")

func _on_state_changed(state: int) -> void:
	_set_status(String(STATE_NAMES.get(state, str(state))))
	if state == 2 or state == 3:
		play_btn.text = "播放中"
		info_label.text = JSON.stringify(player.get_media_info(), "  ")
	else:
		play_btn.text = "播放"

func _on_preset(index: int) -> void:
	url_edit.text = String(PRESETS[index]["url"])

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
	# 顶栏
	var top := HBoxContainer.new()
	var back := Button.new()
	back.text = " ← 主页 "
	back.pressed.connect(func() -> void: get_tree().change_scene_to_file("res://scenes/hub.tscn"))
	top.add_child(back)
	var title := Label.new()
	title.text = "  02 直播流 (RTMP / HLS / RTSP)"
	title.add_theme_font_size_override("font_size", 16)
	top.add_child(title)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(spacer)
	fps_label = Label.new()
	fps_label.add_theme_color_override("font_color", TEXT_DIM)
	top.add_child(fps_label)
	root.add_child(top)
	# 地址行
	var url_row := HBoxContainer.new()
	url_row.add_theme_constant_override("separation", 8)
	url_edit = LineEdit.new()
	url_edit.placeholder_text = "rtmp://… / https://…m3u8 / rtsp://…"
	url_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	url_edit.text_submitted.connect(func(_t: String) -> void: _start())
	url_row.add_child(url_edit)
	play_btn = Button.new()
	play_btn.text = "播放"
	play_btn.custom_minimum_size = Vector2(80, 0)
	play_btn.pressed.connect(_start)
	url_row.add_child(play_btn)
	var stop_btn := Button.new()
	stop_btn.text = "停止"
	stop_btn.pressed.connect(_stop)
	url_row.add_child(stop_btn)
	root.add_child(url_row)
	# 预设行
	var preset_row := HBoxContainer.new()
	preset_row.add_theme_constant_override("separation", 8)
	var preset_label := Label.new()
	preset_label.text = "预设:"
	preset_label.add_theme_color_override("font_color", TEXT_DIM)
	preset_row.add_child(preset_label)
	for i in PRESETS.size():
		var b := Button.new()
		b.text = String(PRESETS[i]["name"])
		b.pressed.connect(_on_preset.bind(i))
		preset_row.add_child(b)
	root.add_child(preset_row)
	# 视频区
	video = TextureRect.new()
	video.size_flags_vertical = Control.SIZE_EXPAND_FILL
	video.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	root.add_child(video)
	status_label = Label.new()
	status_label.text = "填入拉流地址后点播放 (HLS 测试流可直接验证)"
	status_label.add_theme_font_size_override("font_size", 13)
	root.add_child(status_label)
	info_label = Label.new()
	info_label.text = ""
	info_label.add_theme_font_size_override("font_size", 11)
	info_label.add_theme_color_override("font_color", TEXT_DIM)
	root.add_child(info_label)
