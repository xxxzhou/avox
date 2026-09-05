extends Control
## avox_godot 示例主页 —— 选择演示场景。

const BG := Color("0b0f16")
const ACCENT := Color("3b82f6")
const TEXT_DIM := Color("8b93a3")

const SCENES := [
	{
		"title": "01  本地播放",
		"desc": "本地视频文件 · 硬解 · GPU 直通 · 播放控制/进度/音量/倍速",
		"scene": "res://scenes/local_player.tscn",
	},
	{
		"title": "02  直播流",
		"desc": "RTMP / HLS / RTSP 网络流播放 · 状态与媒体信息",
		"scene": "res://scenes/live_stream.tscn",
	},
	{
		"title": "03  相机采集",
		"desc": "设备枚举与实时预览 · SourcePlayer + DeviceManager",
		"scene": "res://scenes/camera_preview.tscn",
	},
	{
		"title": "04  语音字幕",
		"desc": "麦克风 → 流式语音识别 · MicCapture + SttNode (需 STT 模型)",
		"scene": "res://scenes/mic_subtitle.tscn",
	},
]

func _ready() -> void:
	var bg := ColorRect.new()
	bg.color = BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	box.custom_minimum_size = Vector2(560, 0)
	center.add_child(box)
	var title := Label.new()
	title.text = "avox Godot 示例"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 30)
	box.add_child(title)
	var sub := Label.new()
	sub.text = "MediaPlayer · SourcePlayer · SttNode —— 插件需先部署到本工程"
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.add_theme_font_size_override("font_size", 13)
	sub.add_theme_color_override("font_color", TEXT_DIM)
	box.add_child(sub)
	box.add_child(HSeparator.new())
	if not ClassDB.class_exists("MediaPlayer"):
		var warn := Label.new()
		warn.text = "未检测到 avox_godot 插件 —— 请先运行\nplatform/godot/plugin/deploy_godot.ps1 -GodotProject <本工程>"
		warn.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		warn.add_theme_font_size_override("font_size", 14)
		warn.add_theme_color_override("font_color", Color("f59e0b"))
		box.add_child(warn)
	for entry in SCENES:
		box.add_child(_make_card(entry))
	var foot := Label.new()
	foot.text = "avox_godot · GDExtension · Godot 4.3+ (Vulkan)"
	foot.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	foot.add_theme_font_size_override("font_size", 11)
	foot.add_theme_color_override("font_color", TEXT_DIM)
	box.add_child(foot)

func _make_card(entry: Dictionary) -> Control:
	var card := VBoxContainer.new()
	card.add_theme_constant_override("separation", 2)
	var btn := Button.new()
	btn.text = String(entry["title"])
	btn.custom_minimum_size = Vector2(0, 44)
	btn.add_theme_font_size_override("font_size", 17)
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color("151b26")
	sb.set_corner_radius_all(10)
	sb.set_content_margin_all(8)
	btn.add_theme_stylebox_override("normal", sb)
	var sb_hover := sb.duplicate()
	sb_hover.bg_color = Color("1c2740")
	sb_hover.set_border_width_all(1)
	sb_hover.border_color = ACCENT
	btn.add_theme_stylebox_override("hover", sb_hover)
	btn.add_theme_stylebox_override("pressed", sb_hover)
	btn.pressed.connect(func() -> void: get_tree().change_scene_to_file(String(entry["scene"])))
	card.add_child(btn)
	var desc := Label.new()
	desc.text = String(entry["desc"])
	desc.add_theme_font_size_override("font_size", 12)
	desc.add_theme_color_override("font_color", TEXT_DIM)
	card.add_child(desc)
	return card
