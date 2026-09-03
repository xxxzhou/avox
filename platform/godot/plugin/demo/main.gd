extends Control
## avox Godot 播放器 —— 最小示例
## MediaPlayer 解码出的视频纹理(GPU 直通 / 零拷贝)桥到 TextureRect 显示。
## 前提: addons/avox_godot 已按 demo/README.md 部署。

var player: MediaPlayer
var _tex_rect: TextureRect

func _ready() -> void:
	# 显示用 TextureRect, 填满整个控件
	_tex_rect = TextureRect.new()
	_tex_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	_tex_rect.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_tex_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	add_child(_tex_rect)

	# 播放器节点(每帧自动把 avox 帧更新到纹理)
	player = MediaPlayer.new()
	add_child(player)

	# ↓↓↓ 改成你要播放的地址: rtmp / rtsp / http(s) / 本地文件路径 ↓↓↓
	player.url = "rtmp://example.com/live"
	player.hard_decode = true

	player.state_changed.connect(func(s): print("state = ", s))
	player.io_error.connect(func(c): push_warning("io_error = %d" % c))
	player.completed.connect(func(): print("completed"))

	player.play()

func _process(_delta: float) -> void:
	if player:
		var t = player.get_texture()
		if t:
			_tex_rect.texture = t
