extends Control
## avox Godot WebRTC 拉流 —— 最小示例
## RtcPlayer 走 HTTP 信令 (ZLM type=play / WHEP) 一键拉流, 远端画面纹理显示到 TextureRect。
## 前提: addons/avox_godot 已按 demo/README.md 部署, 且 bin/plugins/ 里有 avox_webrtc.dll。

const CONN_NAMES := ["new", "connecting", "connected", "disconnected", "failed", "closed"]

var rtc: RtcPlayer
var _tex_rect: TextureRect
var _status: Label

func _ready() -> void:
	# 显示用 TextureRect, 填满整个控件
	_tex_rect = TextureRect.new()
	_tex_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	_tex_rect.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_tex_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	add_child(_tex_rect)
	# 底部状态行: 连接状态 / 首帧 / 错误
	_status = Label.new()
	_status.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	_status.offset_top = -36
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_status)

	# 播放器节点(每帧自动把 avox 远端帧更新到纹理)
	rtc = RtcPlayer.new()
	add_child(rtc)
	# 纯拉流: 只收不发 (默认 sendrecv)
	rtc.set_video_direction(1)
	rtc.set_audio_direction(1)

	# ↓↓↓ ZLM HTTP 信令地址 (type=play); 拉别的流改 stream 名 (如 H265 的 avox) ↓↓↓
	var url := "http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play"
	# 也支持命令行传入: godot --path . res://rtc_main.tscn -- <信令url>
	var args := OS.get_cmdline_user_args()
	if args.size() > 0 and not args[0].begins_with("--"):
		url = args[0]

	rtc.connection_state_changed.connect(func(s): _status.text = "连接: %s" % CONN_NAMES[s])
	rtc.first_video_frame.connect(func(): _status.text = "出图 %dx%d" % [
		rtc.get_texture().get_width(), rtc.get_texture().get_height()])
	rtc.rtc_error.connect(func(c, m): _status.text = "错误 %d: %s" % [c, m])

	# 一键连接: 内部创建 ZLM/WHEP 信令 agent, 自动 POST offer 并回填 answer
	rtc.connect_signaling(url)
	# 自定义信令改用 rtc.open_rtc() + 监听 local_sdp/ice_candidate 信号自己交换

func _process(_delta: float) -> void:
	if rtc:
		var t = rtc.get_texture()
		if t:
			_tex_rect.texture = t
