extends Control
## 视频驱动 Avatar 工具 (tools/src/avatar)
## 左: MediaPlayer 视频画面 (GPU 直通零拷贝纹理); 右: 3D avatar (GLB + ARKit52 blendshape),
## 由 VideoFaceNode 实时驱动。脸部 blendshape 来自 avox_avatar mediapipe (视频→52 ARKit)。
## 依赖: addons/avox_godot (MediaPlayer + VideoFaceNode) 已部署 + GPU 直通 (Vulkan) 有 enableImage 帧。
## Avatar: 运行时 GLTFDocument 加载 .glb/.gltf/.vrm, 扫各 mesh 的 blendshape 名按 ARKit52 映射。
##   - 需模型带 ARKit52 blendshape (名: jawOpen/eyeBlinkLeft/mouthSmileLeft…, 同 ARKit 规范)。
##   - VRM 不直接带 ARKit 名 (其预设存于 VRM 扩展), 需先转成 GLB+ARKit (如 VRM→ARKit 转换/Blender 烘焙)。

const ACCENT   := Color("#3B82F6")
const PANEL_BG := Color(0.05, 0.06, 0.08, 0.92)
const BG_DARK  := Color(0.025, 0.03, 0.045)
# 共享 UI 基建 + 播放器组件复用 (图标/进度条)
const UiKit := preload("res://src/common/ui_kit.gd")
const ScrubBarScript := preload("res://src/mediaplayer/scrub_bar.gd")

# 3D avatar 视图 (共享组件): 相机/模型加载/驱动合成链 (base+眨眼+视线+情绪) 全在
# avatar_view.gd 内; blendshape canonical 序见 arkit52.gd (与 SDK getArkit52NamesCsv 一致)
const AvatarView := preload("res://src/avatar/avatar_view.gd")

const DEFAULT_AVATAR_PATHS := ["res://src/avatar/avatar.gltf", "res://src/avatar/avatar.glb", "user://avatar.glb", "res://avatar.glb", "res://assets/avatar.glb"]
const HARD_DECODE := false  # GPU 直通需软解路径; true=DX11 硬解无 enableImage 帧

var player: MediaPlayer
var face: VideoFaceNode
var body: BodyNode

# ── 身体手势 (body_landmarks → retargeting) ──
const BodyRetargetScript = preload("res://src/avatar/retarget.gd")
var _skeleton: Skeleton3D = null   # avatar 骨骼 (retargeting 用)
var _retarget: RefCounted = null   # 33点 → 骨骼旋转 (BodyRetarget 实例)
var _body_ready := false

# ── 左: 视频 ──
var _video: TextureRect

# ── 右: 3D avatar (共享组件 avatar_view.gd) ──
var view: SubViewportContainer   # AvatarView 实例 (set_base/apply_emotion/模型管理)

# ── UI ──
var _status_chip: Label
var _face_chip: Label
var _video_empty: VBoxContainer   # 左侧空态 (拖入视频提示)
var _av_bar: Control              # 视频驱动传输条 (播放/暂停 + 进度)
var _av_play: IconButton
var _av_scrub: ScrubBarScript
var _av_time: Label
var _file_dialog: FileDialog
var _fd_mode := ""  # "video" / "avatar"
var _face_ready := false

# PlayerState (avox) 取值: 播放判定用
const ST_PLAYING := 3

func _ready() -> void:
	# 高 DPI: 钉 1280 画布基准 + 窗口物理 ×sc (与播放器同观感; 项目基准 1920x1080 见 project.godot)
	UiKit.apply_desktop_dpi(get_window())
	DisplayServer.window_set_title("视频驱动 Avatar")
	theme = UiKit.build_theme()
	_build_ui()
	_build_player()
	_build_face()
	_build_body()
	get_window().files_dropped.connect(_on_files_dropped)
	# 默认尝试加载 avatar (用户可后续用「打开 Avatar 模型」换)
	for p in DEFAULT_AVATAR_PATHS:
		if ResourceLoader.exists(p) or FileAccess.file_exists(p):
			_load_avatar(p)
			break
	_refresh_status()

func _process(_delta: float) -> void:
	if player:
		var t := player.get_texture()
		if t and _video.texture != t:
			_video.texture = t
		# 驱动传输条: 进度/时长/时间
		if _av_scrub:
			var dur := player.get_duration()
			_av_scrub.duration_ms = maxf(dur, 0.0)
			if dur > 0.0:
				_av_scrub.value = player.get_position() / dur
				_av_time.text = "%s / %s" % [_fmt_ms(player.get_position()), _fmt_ms(dur)]
			else:
				_av_time.text = "● 直播"
			_av_scrub.disabled = dur <= 0.0
		if _av_play:
			_av_play.icon_kind = IconButton.Icon.PAUSE if player.get_state() == ST_PLAYING else IconButton.Icon.PLAY

func _fmt_ms(ms: float) -> String:
	var t := int(ms / 1000.0)
	if t >= 3600:
		return "%d:%02d:%02d" % [t / 3600, (t / 60) % 60, t % 60]
	return "%02d:%02d" % [t / 60, t % 60]

# ============================================================
# UI 构建 (VBox: 顶栏 + HBox[左视频 | 右3D])
# ============================================================
func _build_ui() -> void:
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 0)
	add_child(root)
	# 顶栏
	root.add_child(_build_top_bar())
	# 内容区
	var hbox := HBoxContainer.new()
	hbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hbox.add_theme_constant_override("separation", 2)
	root.add_child(hbox)
	# 左: 视频
	var left := Panel.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_theme_stylebox_override("panel", _flat(BG_DARK))
	hbox.add_child(left)
	_video = TextureRect.new()
	_video.set_anchors_preset(Control.PRESET_FULL_RECT)
	_video.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_video.mouse_filter = Control.MOUSE_FILTER_IGNORE
	left.add_child(_video)
	# 左侧空态 (拖入视频提示, 开播即隐)
	_video_empty = VBoxContainer.new()
	_video_empty.set_anchors_preset(Control.PRESET_CENTER)
	_video_empty.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_video_empty.grow_vertical = Control.GROW_DIRECTION_BOTH
	_video_empty.alignment = BoxContainer.ALIGNMENT_CENTER
	_video_empty.add_theme_constant_override("separation", 10)
	_video_empty.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var ve_icon := IconView.new()
	ve_icon.icon_kind = IconButton.Icon.PLAY
	ve_icon.icon_color = Color(1, 1, 1, 0.14)
	ve_icon.custom_minimum_size = Vector2(64, 64)
	ve_icon.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	ve_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_video_empty.add_child(ve_icon)
	var ve_lbl := Label.new()
	ve_lbl.text = "拖入视频，或「文件」菜单打开"
	ve_lbl.add_theme_color_override("font_color", Color(1, 1, 1, 0.45))
	ve_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	ve_lbl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_video_empty.add_child(ve_lbl)
	left.add_child(_video_empty)
	# 驱动传输条 (播放/暂停 + 进度): 悬浮于左视频区底部
	_av_bar = _build_transport_bar()
	left.add_child(_av_bar)
	# 右: 3D avatar 视图 (相机/灯光/加载/驱动合成链见 avatar_view.gd)
	view = AvatarView.new()
	hbox.add_child(view)
	view.model_loaded.connect(_on_view_model_loaded)

# 左视频区底部驱动传输条: 播放/暂停 + 进度 (复用播放器 ScrubBar/IconButton)
func _build_transport_bar() -> Control:
	var bar := PanelContainer.new()
	bar.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	bar.offset_left = 10
	bar.offset_right = -10
	bar.offset_top = -64
	bar.offset_bottom = -10
	bar.add_theme_stylebox_override("panel", _flat(Color(0.03, 0.04, 0.055, 0.72), 10, Color(1, 1, 1, 0.08), 1))
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 4)
	bar.add_child(rows)
	_av_scrub = ScrubBarScript.new()
	_av_scrub.custom_minimum_size = Vector2(0, 16)
	_av_scrub.scrub_ended.connect(func(v: float):
		var dur := player.get_duration()
		if dur > 0.0:
			player.seek(int(v * dur)))
	rows.add_child(_av_scrub)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	rows.add_child(row)
	_av_play = IconButton.new()
	_av_play.icon_kind = IconButton.Icon.PLAY
	_av_play.icon_ratio = 0.44
	_av_play.custom_minimum_size = Vector2(34, 30)
	_av_play.focus_mode = Control.FOCUS_NONE
	_av_play.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	_av_play.add_theme_color_override("font_hover_color", Color.WHITE)
	_av_play.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	_av_play.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.20), 6))
	_av_play.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.32), 6))
	_av_play.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_av_play.pressed.connect(func():
		if player.get_state() == ST_PLAYING:
			player.pause()
		else:
			player.resume())
	row.add_child(_av_play)
	_av_time = Label.new()
	_av_time.add_theme_font_size_override("font_size", 12)
	_av_time.add_theme_color_override("font_color", Color(0.8, 0.85, 0.92))
	_av_time.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_av_time.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(_av_time)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(spacer)
	return bar

func _build_top_bar() -> Control:
	var bar := PanelContainer.new()
	bar.custom_minimum_size = Vector2(0, 40)
	bar.add_theme_stylebox_override("panel", _flat(PANEL_BG, 0, Color(1, 1, 1, 0.06), 1))
	var inner := MarginContainer.new()
	inner.add_theme_constant_override("margin_left", 10)
	inner.add_theme_constant_override("margin_right", 10)
	bar.add_child(inner)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	inner.add_child(row)
	# 文件菜单
	var m_file := _mk_menu("文件")
	var pf := m_file.get_popup()
	pf.add_item("打开视频…", 0)
	pf.add_item("打开 Avatar 模型…", 1)
	pf.add_separator()
	pf.add_item("重置表情 (blendshape 归零)", 2)
	pf.add_separator()
	pf.add_item("返回主页", 3)
	pf.add_item("退出", 4)
	pf.id_pressed.connect(_on_file_menu)
	row.add_child(m_file)
	# 操作提示收进 tooltip (顶栏只留状态 chip)
	var hint := Label.new()
	hint.text = "视频 → ARKit52 实时驱动"
	hint.add_theme_font_size_override("font_size", 12)
	hint.add_theme_color_override("font_color", Color(0.6, 0.64, 0.72))
	hint.tooltip_text = "拖入: 视频/模型\n右键拖拽旋转 · 滚轮缩放"
	hint.mouse_filter = Control.MOUSE_FILTER_STOP
	row.add_child(hint)
	# 状态 chips (右对齐): Avatar 概况 + 面部就绪
	var chips := HBoxContainer.new()
	chips.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	chips.alignment = BoxContainer.ALIGNMENT_END
	chips.add_theme_constant_override("separation", 6)
	inner.add_child(chips)
	_status_chip = _mk_chip(chips, "Avatar")
	_face_chip = _mk_chip(chips, "面部")
	return bar

# 状态胶囊: 深底圆角小签
func _mk_chip(parent: Control, text: String) -> Label:
	var chip := PanelContainer.new()
	chip.add_theme_stylebox_override("panel", _flat(Color(1, 1, 1, 0.06), 10, Color(1, 1, 1, 0.08), 1))
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 11)
	l.add_theme_color_override("font_color", Color(0.8, 0.85, 0.92))
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	chip.add_child(l)
	parent.add_child(chip)
	return l

func _mk_menu(text: String) -> MenuButton:
	var mb := MenuButton.new()
	mb.text = text
	mb.focus_mode = Control.FOCUS_NONE
	mb.add_theme_font_size_override("font_size", 13)
	mb.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	mb.add_theme_color_override("font_hover_color", Color.WHITE)
	mb.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 5))
	mb.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.14), 5))
	mb.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	return mb

func _flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.set_corner_radius_all(radius)
	sb.border_color = border
	sb.set_border_width_all(bw)
	return sb

# ============================================================
# MediaPlayer (左视频)
# ============================================================
func _build_player() -> void:
	player = MediaPlayer.new()
	add_child(player)
	player.hard_decode = HARD_DECODE
	player.state_changed.connect(func(_s): _refresh_status())
	player.io_error.connect(func(c): _toast("视频 IO 错误: %d" % c))
	var args := OS.get_cmdline_user_args()
	var url := ""
	for a in args:
		if not a.begins_with("--"):
			url = a
			break
	if not url.is_empty():
		_play_video(url)

func _play_video(url: String) -> void:
	if url.is_empty():
		return
	_video.texture = null
	_video_empty.visible = false
	player.url = url
	player.stop()
	player.play()

# ============================================================
# VideoFaceNode (视频→ARKit52 blendshape)
# ============================================================
func _build_face() -> void:
	face = VideoFaceNode.new()
	add_child(face)
	face.face_blendshape.connect(_on_face_blendshape)
	face.face_ready.connect(_on_face_ready)
	face.face_desc.connect(_on_face_desc)
	face.error.connect(func(m): _toast("面部错误: %s" % m))
	# bind_source 绑 MediaPlayer 的 ISurfaceRender (VideoFaceNode 经 enableImage 零拷贝回读 rgba8);
	# tap 在 face _process 等 surface 就绪后自动挂。feed_every 节流防渲染线程饿死。
	if OS.get_environment("AVOX_NO_FACE").to_lower() in ["1", "true"]:
		_toast("AVOX_NO_FACE: 面部驱动已禁用 (诊断)")
		return
	face.bind_source(player)
	face.feed_every = 4
	face.start()

func _on_face_ready() -> void:
	_face_ready = true
	_refresh_status()

# ============================================================
# BodyNode (视频→MediaPipe Pose 33 点身体 landmark)
# ============================================================
func _build_body() -> void:
	body = BodyNode.new()
	add_child(body)
	body.body_landmarks.connect(_on_body_landmarks)
	body.body_ready.connect(_on_body_ready)
	body.error.connect(func(m): _toast("身体错误: %s" % m))
	if OS.get_environment("AVOX_NO_BODY").to_lower() in ["1", "true"]:
		_toast("AVOX_NO_BODY: 身体驱动已禁用 (诊断)")
		return
	body.bind_source(player)
	body.feed_every = 4
	body.start()

func _on_body_ready() -> void:
	_body_ready = true
	# avatar 已加载时建立 retarget (若模型带骨骼); 传 avatar 根, 供 retarget 用 mesh 蒙皮几何求骨骼解剖长轴
	if _skeleton != null:
		_retarget = BodyRetargetScript.new(_skeleton, view.get_avatar_root(), view.vrm_humanoid())
	_toast("身体就绪 (%s)" % ("骨骼可驱动" if _retarget and _retarget.has_skeleton() else "模型无骨骼, 仅面部驱动"))
	_refresh_status()

func _on_body_landmarks(lm: PackedVector3Array, conf: PackedFloat32Array, _img_w: int, _img_h: int, _pts: int) -> void:
	if lm.size() < 33:
		return
	if _retarget:
		# 信号回调里 get_process_delta_time() 为 0, 用固定平滑 (body 约 30fps, 渐进插值)
		_retarget.drive(lm, conf, 0.033)

func _on_face_desc(_fps: int, bs_count: int, lm_count: int) -> void:
	_face_chip.text = "面部就绪 (bs=%d lm=%d)" % [bs_count, lm_count]

func _on_face_blendshape(bs: PackedFloat32Array, _pts: int, _final: bool) -> void:
	if bs.size() < 52:
		return
	# 采集帧进视图 base 通道 (合成/写 mesh 在 avatar_view 内部 60fps 做)
	view.set_base("video", bs)

# ============================================================
# Avatar 加载 (委托 avatar_view) + 骨骼扫描 (身体 retargeting 用)
# ============================================================
func _load_avatar(path: String) -> bool:
	return view.load_model(path)

func _on_view_model_loaded(ok: bool, info: String) -> void:
	_toast(info)
	if ok:
		_scan_skeleton()

# 缓存 avatar 骨骼 (Skeleton3D), 供身体 retargeting 驱动
func _scan_skeleton() -> void:
	_skeleton = null
	_retarget = null
	if view == null or not view.is_loaded():
		return
	_skeleton = view.get_skeleton()
	if _skeleton and _body_ready:
		_retarget = BodyRetargetScript.new(_skeleton, view.get_avatar_root(), view.vrm_humanoid())

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		_back_home()

# ============================================================
# 菜单 / 文件对话框 / 拖拽
# ============================================================
func _on_file_menu(id: int) -> void:
	match id:
		0: _open_dialog("video")
		1: _open_dialog("avatar")
		2: view.reset_blendshapes()
		3: _back_home()
		4: get_tree().quit()

func _open_dialog(mode: String) -> void:
	_fd_mode = mode
	if _file_dialog == null:
		_file_dialog = FileDialog.new()
		_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_file_dialog.theme = UiKit.build_theme()
		_file_dialog.file_selected.connect(_on_file_picked)
		add_child(_file_dialog)
	if mode == "video":
		_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_file_dialog.filters = PackedStringArray([
			"*.mp4,*.mkv,*.avi,*.mov,*.flv,*.ts,*.m2ts ; 视频文件", "* ; 所有文件"])
		_file_dialog.title = "打开视频文件"
	else:
		_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_file_dialog.filters = PackedStringArray([
			"*.glb,*.gltf ; GLB/GLTF 模型", "*.vrm ; VRM (需带 ARKit 名)", "* ; 所有文件"])
		_file_dialog.title = "打开 Avatar 模型"
	_file_dialog.popup_centered_ratio(0.7)

func _on_file_picked(path: String) -> void:
	if _fd_mode == "avatar":
		_load_avatar(path)
	else:
		_play_video(path)

func _on_files_dropped(files: PackedStringArray) -> void:
	if files.is_empty():
		return
	var p := files[0]
	var ext := p.get_extension().to_lower()
	if ext in ["glb", "gltf", "vrm"]:
		_load_avatar(p)
	else:
		_play_video(p)

func _back_home() -> void:
	get_tree().change_scene_to_file("res://src/hub/main.tscn")

# ============================================================
# 状态 / Toast
# ============================================================
func _refresh_status() -> void:
	var parts: Array = []
	if view == null or not view.is_loaded():
		parts.append("未加载 Avatar")
	else:
		parts.append("Avatar: %d mesh / %d bs" % [view.mesh_count(), view.matched_count()])
	parts.append("面部: " + ("就绪" if _face_ready else "加载中…"))
	_status_chip.text = "  ·  ".join(parts)
	_face_chip.text = "● " + ("就绪" if _face_ready else "加载中…")
	_face_chip.add_theme_color_override("font_color",
		Color(0.35, 0.82, 0.55) if _face_ready else Color(0.95, 0.75, 0.25))

func _toast(msg: String) -> void:
	print("[avatar] ", msg)
	_status_chip.text = msg
	var tw := create_tween()
	tw.tween_interval(2.5)
	tw.tween_callback(Callable(self, "_refresh_status"))
