extends Control
## avox 沉浸式悬浮播放器 (tools/src/mediaplayer)
## 半透明悬浮控制条 + 鼠标静止自动隐藏 + 单一强调色, 设计稿见 docs/播放器界面设计.md。
## 依赖: addons/avox_godot 已部署 (platform/godot/plugin/deploy_godot.ps1)。
## 播放地址: 命令行 `-- <url>` 首个参数, 否则用 DEFAULT_URL。

const DEFAULT_URL := ""   # 启动自动播放地址 (调试用; 真机联调可临时设 adb reverse 端口的 http 流)
const HARD_DECODE := false                    # true=硬解(DX11)  false=软解
const ACCENT     := Color("#3B82F6")
const PANEL_BG   := Color(0.03, 0.04, 0.055, 0.70)   # alpha 足够压亮画面, 否则磨砂在亮帧上泛灰
const HIDE_DELAY := 3.0
const FADE_TIME  := 0.35
const CFG_PATH   := "user://mediaplayer.cfg"  # 文件对话框记忆/最近打开/设置 持久化
const MAX_RECENT := 10

# ── 设置键与默认值 (cmdPlay 选项的 GDScript 镜像) ──
const SETTING_IO_PLAN     := "io_plan"       # "auto" / "ffmpeg" / "zlmediakit"
const SETTING_HARD_DECODE := "hard_decode"   # bool (软/硬解)
const SETTING_LOG_PACKET  := "log_packet"    # bool (IO 包日志)
const SETTING_LOG_DECODE  := "log_decode"    # bool (解码帧日志)
const SETTING_LOG_RENDER  := "log_render"    # bool (渲染帧日志)
const SETTING_ACRYLIC     := "acrylic"       # bool (磨砂面板, 低配集显可关)
const _DEFAULT_SETTINGS := {
    SETTING_IO_PLAN:     "auto",
    SETTING_HARD_DECODE: HARD_DECODE,
    SETTING_LOG_PACKET:  false,
    SETTING_LOG_DECODE:  false,
    SETTING_LOG_RENDER:  false,
    SETTING_ACRYLIC:     true,
}
# IoPlan 枚举值 (avox/AvoxMuxer.h): zlmediakit=1, ffmpeg=2
const _IO_PLAN_AUTO := -1   # 不调 setIoPlan (由 URL 自动)
const _IO_PLAN_ZLM  := 1
const _IO_PLAN_FFM  := 2

# PlayerState (avox) 0..8
const ST_NONE := 0
const ST_OPENING := 1
const ST_READY := 2
const ST_PLAYING := 3
const ST_PAUSE := 4
const ST_SEEK := 5
const ST_BUFFERING := 6
const ST_STOPPED := 7
const ST_COMPLETED := 8

var player: MediaPlayer

# 运行时状态
var _playing := false
var _is_live := false
var _scrubbing := false
var _state_buffering := false
var _mute_before := 1.0
var _over_bar := false
var _over_menu := false
var _menu_open := false

# UI 节点(代码构建, 结构见设计稿 §6.1)
var _video: TextureRect
var _hud: CanvasLayer
var _hud_root: Control
var _title: Label
var _center: IconButton
var _control_bar: PanelContainer
var _play_btn: IconButton
var _scrub: ScrubBar
var _time: Label
var _mute_btn: IconButton
var _vol: HSlider
var _speed: MenuButton
var _top_bar: PanelContainer
var _live_popup: PanelContainer
var _live_edit: LineEdit
var _live_combo: OptionButton
var _file_dialog: FileDialog
# 磁力/BT 解析(RemoteSource)与文件选择弹窗
var _magnet_popup: PanelContainer
var _magnet_edit: LineEdit
var _magnet_combo: OptionButton
var _torrent_popup: PanelContainer
var _torrent_title: Label
var _torrent_list: ItemList
var _popup_dragging := {}   # popup -> bool: 左键/手指按住拖动中(软键盘盖住弹窗时可拖开)
var _remote: RemoteSource
var _torrent_files: Array = []   # 根列表中的可播媒体条目 {name, token, size, media}
var _probe_url := ""
var _autotest_url := ""   # 非空时起播/出错输出 [AVOX][TEST] 判定行, 供 logcat/stdout 自动化回归抓取
var _autotest_t0 := 0
const TORRENT_META_TIMEOUT := 45000
# 历史记录(持久化于 user://mediaplayer.cfg)
var _last_dir := ""
var _recent_files: PackedStringArray = []
var _recent_popup: PopupMenu
var _live_urls: PackedStringArray = []
var _magnet_urls: PackedStringArray = []
var _magnet_names: PackedStringArray = []   # 与 _magnet_urls 对齐的种子名(探测成功回填), 空串则显示截断 url
var _full_btn: IconButton
var _toast: PanelContainer      # 胶囊底
var _toast_label: Label
var _hide_timer: Timer
var _info_popup: PanelContainer
var _info_title: Label
var _info_grid: GridContainer
# 设置面板
var _settings_popup: PanelContainer
var _settings_io_combo: OptionButton
var _settings_hard_decode: CheckBox
var _settings_log_packet: CheckBox
var _settings_log_decode: CheckBox
var _settings_log_render: CheckBox
var _settings_acrylic: CheckBox
# 当前生效设置 (cfg 加载后填, 设置面板改动即写回, 退出时 cfg 落盘)
var _settings: Dictionary = _DEFAULT_SETTINGS.duplicate()
# Android MVP: AndroidEnv 未接线时强制软解 (_ready 的 android 分支置 true)
var force_soft_decode := false
# ── 触屏/尺寸 token (_ready 按平台填): 1 逻辑px ≈ 1dp, 触屏目标 ≥46 保证可点 ──
var _touch := false
var _btn_h := 38
var _input_h := 34
var _scrub_h := 20
var _theme: Theme
var _last_tap_ms := 0            # 视频区双击判定
var _last_back_ms := 0           # Android 返回键二次退出判定
var _last_dpi_check_ms := 0      # 跨显示器 DPI 跟随轮询
var _dpi_screen := -1            # 已应用的屏幕/缩放 (变化才重排)
var _dpi_scale := 1.0
var _info_scroll: ScrollContainer
var _settings_scroll: ScrollContainer
var _scrim: ColorRect            # 弹窗遮罩 (模态感 + 亮画面可读性)
var _scrim_tween: Tween
var _empty_hint: VBoxContainer   # 空态门面: 大播放图标 + 拖入提示

# ── Acrylic 磨砂面板 (acrylic_panel.gdshader, 采样视频纹理模糊) ──
const ACRYLIC_SHADER := preload("res://src/mediaplayer/acrylic_panel.gdshader")
var _acrylic_entries: Array = []   # {panel, radius, fallback} 已挂磨砂材质的面板

# ── 单色矢量图标按钮 (替代 emoji: Windows 彩色 emoji/Android 缺字, 跨平台不一致) ──
const IconBtn := preload("res://src/common/icon_button.gd")
# ── 共享 UI 基建 (主题字体/高 DPI 缩放/flat stylebox) ──
const UiKit := preload("res://src/common/ui_kit.gd")

# ── 临时诊断: seek 后 get_position 轨迹采样 ──
var _last_seek_ms := -1
var _trace_last_ms := -1
var _trace_last_st := -1

func _ready() -> void:
	# 项目基准已升 1920x1080 (新场景用); 本场景仍按 1280x720 设计 → 钉住画布基准,
	# 基准比/总缩放与旧 1280 项目完全一致, 窗口与布局零改动 (高DPI适配文档"非基准窗口"节)
	get_window().content_scale_size = Vector2i(1280, 720)
	# 触屏优先: 控件基准加大 (触控目标 ≥46 逻辑px≈dp), 音量控件走硬件键隐藏
	_touch = UiKit.is_touch()
	_btn_h = 46 if _touch else 38
	_input_h = 40 if _touch else 34
	_scrub_h = 28 if _touch else 20
	_load_cfg()
	# Android 返回键/手势不直接退出: 走 _notification 的 关弹窗→退全屏→二次退出 层级
	get_tree().quit_on_go_back = false
	# 移动端: 目标总缩放 T = dpi/160 (1 逻辑px ≈ 1dp), 上限由「逻辑短边 ≥360」约束 ——
	# 写死 2.0 时 440dpi 手机实际只有 ~0.7dp/px, 触控目标远小于 48dp 规范。
	# 注意 canvas_items+expand 下总缩放 = 基准比 min(win/base) × content_scale_factor,
	# factor 要除回基准比, 否则二次放大 (逻辑视口缩水, 布局溢出)。
	# 屏幕取长边/短边计算, 与启动时横竖屏状态无关 (本 app 锁 sensor_landscape)。
	# 同时 MVP 强制软解: AndroidEnv(vm) 未接线 (HyperOS 屏蔽 libart dlopen),
	# MediaCodec 硬解依赖 JNI 不可用; 修好后移除 force_soft_decode
	if OS.has_feature("mobile") or OS.has_feature("android"):
		var dpi := DisplayServer.screen_get_dpi()
		var scr := DisplayServer.screen_get_size(get_window().current_screen)
		var t := clampf(dpi / 160.0, 1.0, 3.0)
		var base := 1.0
		if scr.x > 0 and scr.y > 0:
			t = clampf(minf(t, minf(scr.x, scr.y) / 360.0), 1.0, 3.0)
			base = minf(maxf(scr.x, scr.y) / 1280.0, minf(scr.x, scr.y) / 720.0)
		get_window().content_scale_factor = t / maxf(base, 0.01)
		force_soft_decode = true
	elif OS.has_feature("windows") or OS.has_feature("linux"):
		# Windows/Linux 高 DPI: 4K + 150% 缩放下 1280 逻辑宽窗口物理显小;
		# 按系统每显示器缩放同步放大窗口 (screen_get_scale 4.3+, 需要 allow_hidpi)。
		# 只放大窗口即可: canvas_items+expand 下窗口/基准比就是内容缩放,
		# 再设 content_scale_factor 会二次放大 (逻辑视口缩水 → 1280 布局横向溢出)
		var sc := UiKit.desktop_scale(get_window().current_screen)
		var win := get_window()
		if sc > 1.0:
			var base := win.size   # 初始物理尺寸 (hidpi 下未按系统缩放, 即文档所述"显小")
			win.size = Vector2i(int(base.x * sc), int(base.y * sc))
			win.move_to_center()   # 放大保持左上角锚点会溢出屏幕右/下, 重居中
		# 最终逻辑下限: 必须在放大之后设, 否则初始 1280x720 会被顶到下限再被放大出屏
		win.min_size = Vector2i(int(720 * sc), int(405 * sc))
		_dpi_screen = win.current_screen
		_dpi_scale = sc
	# 视频区点按要落到 _unhandled_input (切 HUD/双击手势), 根控件必须放行鼠标
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_theme = UiKit.build_theme()
	_build_ui()
	_set_acrylic_enabled(_settings[SETTING_ACRYLIC])
	_build_player()
	_hide_timer = Timer.new()
	_hide_timer.one_shot = true
	# 触屏没有鼠标移动"唤醒", 停留时长一点再隐
	_hide_timer.wait_time = 4.0 if _touch else HIDE_DELAY
	_hide_timer.timeout.connect(_hide_hud)
	add_child(_hide_timer)
	# 拖文件进窗口直接播放
	get_window().files_dropped.connect(_on_files_dropped)
	_show_hud()
	# 命令行直开磁力/torrent (godot ... -- "magnet:?..." / xx.torrent), 便于自动化与回归
	# avox://<url> 壳在此剥掉, 与 Android 深链同语义
	for arg in OS.get_cmdline_user_args():
		var u := arg.strip_edges()
		if u.begins_with("avox://"):
			u = u.substr(7)
		if u.begins_with("magnet:") or u.ends_with(".torrent"):
			_open_probe_for(u)
			break
		if arg.begins_with("--ui="):
			_open_ui_demo.call_deferred(arg.substr(5))   # UI 走查/截图回归: -- --ui=live|magnet|settings|info|codec
	# Android 深链: magnet: 走解析流程, avox://<url> 剥壳直开 (manifest intent-filter 由 patch_apk 注入)
	if OS.has_feature("android"):
		var links := AppLinks.new()
		var deep := links.take_pending_url()
		if deep.begins_with("avox://"):
			deep = deep.substr(7)
		if deep.begins_with("magnet:") or deep.ends_with(".torrent"):
			_open_probe_for(deep)
		elif not deep.is_empty():
			_load_url(deep)

func _open_probe_for(url: String) -> void:
	_probe_url = url
	if _remote == null:
		_remote = RemoteSource.new()
		_remote.open_result.connect(_on_torrent_opened)
		_remote.list_result.connect(_on_torrent_listed)
	if _remote.is_busy():
		return
	_toast_msg("解析磁力元数据中…(最多 %ds)" % (TORRENT_META_TIMEOUT / 1000))
	_remote.open(url, TORRENT_META_TIMEOUT)

# UI 走查入口 (-- --ui=xxx): 启动即开对应弹层, 免键鼠路径截图
func _open_ui_demo(which: String) -> void:
	match which:
		"live":     _open_live()
		"magnet":   _open_magnet()
		"settings": _open_settings()
		"info":     _open_info("media")
		"codec":    _open_info("codec")

func _process(_delta: float) -> void:
	_update_acrylic()
	_avoid_keyboard()
	_check_screen_scale()
	if player == null:
		return
	var t := player.get_texture()
	if t and _video.texture != t:
		_video.texture = t
	var dur := player.get_duration()
	_scrub.duration_ms = maxf(dur, 0.0)
	if not _is_live and dur > 0.0:
		if not _scrubbing:
			_scrub.value = player.get_position() / dur
		if _state_buffering:
			_scrub.buffered = player.get_progress()
		if _playing:
			_time.text = "%s / %s" % [_fmt(player.get_position()), _fmt(dur)]
	# ── 临时诊断: 仅状态变化或每 2s 打印一次(避免每帧刷屏) ──
	var now := Time.get_ticks_msec()
	var cur_st := player.get_state()
	if cur_st != _trace_last_st or now - _trace_last_ms >= 2000:
		_trace_last_st = cur_st
		_trace_last_ms = now
		print("TRC t=%d pos=%d st=%d scrub=%.4f scrubbing=%s seek=%d" % [
			now, int(player.get_position()), cur_st, _scrub.value,
			str(_scrubbing), _last_seek_ms])

# ── 播放器 ──
func _build_player() -> void:
	player = MediaPlayer.new()
	add_child(player)
	_apply_settings_to_player()
	player.state_changed.connect(_on_state)
	player.completed.connect(func(): _on_state(ST_COMPLETED))
	player.io_error.connect(_on_io_error)
	player.decode_error.connect(_on_decode_error)
	var args := OS.get_cmdline_user_args()
	# 首个非 -- 开头参数才是播放 URL (--ui= 等走查开关不是媒体地址)
	var url := ""
	for a in args:
		if not a.begins_with("--"):
			url = a
			break
	if url.begins_with("avox://"):
		url = url.substr(7)   # 深链壳剥掉再路由, magnet 由下方 probe 流程接管
	if url.is_empty():
		url = DEFAULT_URL  # 真机验证临时: 命令行未给 URL 时用默认地址 (原逻辑不回退, 直接空态)
	if url.is_empty():
		# 默认空路径: 不自动播放, 空态门面提示拖入/菜单打开
		_title.text = "avox 播放器"
		_center.visible = false
		_empty_hint.visible = true
		_control_bar.modulate.a = 0.5   # 空态控制条降级 (死控件)
		return
	# IO 方案: "auto" 按 URL 判定 (cmdPlay 同语义), 显式选则按用户选择
	var io_plan := _resolve_io_plan(url)
	if io_plan != _IO_PLAN_AUTO:
		player.set_io_plan(io_plan)
	_autotest_start(url)
	player.url = url
	_title.text = _display_title(url)
	player.play()

# 把 _settings 投影到 player (player 已被 new+add_child)。
# 硬解立即生效 (avox: setHardDecode); IO 方案 / 日志开关 需下次 open() 生效
# (avox: setIoPlan 注释 "下次打开启用"; option.setBool 走 onOptionChange 仅新会话读到)。
func _apply_settings_to_player() -> void:
	if player == null:
		return
	# Android MVP: AndroidEnv(vm) 未接线, MediaCodec 硬解不可用, 强制软解
	var hard: bool = _settings[SETTING_HARD_DECODE] and not force_soft_decode
	player.hard_decode = hard

# URL -> IoPlan 整数。auto: 本地文件→ffmpeg(2), rtmp/rtsp/http(s)→zlmediakit(1); 显式选则返回对应值。
func _resolve_io_plan(url_str: String) -> int:
	match _settings[SETTING_IO_PLAN]:
		"ffmpeg":     return _IO_PLAN_FFM
		"zlmediakit": return _IO_PLAN_ZLM
		_:
			var u := url_str.to_lower()
			if u.begins_with("rtmp://") or u.begins_with("rtsp://") or \
			   u.begins_with("http://")  or u.begins_with("https://"):
				return _IO_PLAN_ZLM
			return _IO_PLAN_FFM
	return _IO_PLAN_AUTO

func _on_state(s: int) -> void:
	_state_buffering = (s == ST_BUFFERING)
	_playing = (s == ST_PLAYING)
	# 播放中保持屏幕常亮 (移动端), 暂停/停止恢复自动熄屏
	DisplayServer.screen_set_keep_on(_playing)
	match s:
		ST_READY:
			_is_live = player.get_duration() <= 0.0
			if not player.url.begins_with("magnet:") or _probe_url.is_empty():
				_title.text = _display_title(player.url)   # 磁力选文件后标题由 _pick_torrent_file 定
			_update_live_ui()
		ST_PLAYING:
			_autotest_report(true)
			pass
		ST_PAUSE, ST_SEEK, ST_OPENING, ST_BUFFERING:
			pass
		ST_STOPPED:
			_is_live = false
			_time.text = ""
			_update_live_ui()
		ST_COMPLETED:
			pass
	_update_play_btn()
	_update_center(s)
	_show_hud()

# ── 自动隐藏 ──
func _show_hud() -> void:
	_hide_timer.stop()
	_fade_hud(1.0)
	if Input.mouse_mode != Input.MOUSE_MODE_VISIBLE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if _playing and not _scrubbing and not _over_bar and not _over_menu and not _menu_open:
		_hide_timer.start()

func _hide_hud() -> void:
	if not _playing or _scrubbing or _over_bar or _over_menu or _menu_open:
		return
	_fade_hud(0.0)
	Input.mouse_mode = Input.MOUSE_MODE_HIDDEN

func _fade_hud(a: float) -> void:
	if a > 0.0:
		# 隐藏态置 visible=false: 透明(modulate.a=0)控件仍会拦截点击, 移动端点视频区会误触隐形按钮
		_hud_root.visible = true
	if is_equal_approx(_hud_root.modulate.a, a):
		return
	var tw := create_tween()
	tw.tween_property(_hud_root, "modulate:a", a, FADE_TIME).set_trans(Tween.TRANS_CUBIC)
	if a <= 0.0:
		tw.finished.connect(func(): _hud_root.visible = _hud_root.modulate.a > 0.01)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_show_hud()
	elif event is InputEventKey and event.pressed:
		_show_hud()

# ── 键盘快捷键 + 视频区手势 ──
func _unhandled_input(event: InputEvent) -> void:
	# 视频区点按 (控制条/弹窗等 STOP 控件命中的事件到不了这里):
	# 单击切换 HUD 显隐, 双击中段全屏、左右 1/3 ±10s; 触摸经 mouse 模拟同样生效
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		_on_video_tap(event.position)
		return
	if event is not InputEventKey or not event.pressed:
		return
	if _live_popup.visible and event.keycode == KEY_ESCAPE:
		_close_live_popup()
		get_viewport().set_input_as_handled()
		return
	if _info_popup.visible and event.keycode == KEY_ESCAPE:
		_close_info()
		get_viewport().set_input_as_handled()
		return
	if _settings_popup and _settings_popup.visible and event.keycode == KEY_ESCAPE:
		_close_settings()
		get_viewport().set_input_as_handled()
		return
	match event.keycode:
		KEY_SPACE:
			_toggle_play()
		KEY_LEFT:
			_seek_relative(-60000 if event.shift_pressed else -5000)
		KEY_RIGHT:
			_seek_relative(60000 if event.shift_pressed else 5000)
		KEY_UP:
			_set_volume(player.volume + 0.1)
		KEY_DOWN:
			_set_volume(player.volume - 0.1)
		KEY_M:
			_toggle_mute()
		KEY_F:
			_toggle_fullscreen()
		KEY_I:
			if event.ctrl_pressed:
				_open_info("media")
		KEY_J:
			if event.ctrl_pressed:
				_open_info("codec")
		KEY_ESCAPE:
			if DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN:
				_toggle_fullscreen()

# Android 返回键/手势 (Godot 默认直接退出, 这里走 UI 层级: 关弹窗 → 退全屏 → 二次退出)
func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_GO_BACK_REQUEST:
		_on_back()

func _on_back() -> void:
	if _close_overlays():
		return
	if DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN:
		_toggle_fullscreen()
		return
	var now := Time.get_ticks_msec()
	if now - _last_back_ms <= 2000:
		get_tree().quit()
	else:
		_last_back_ms = now
		_show_hud()
		_toast_msg("再按一次返回键退出")

# 关掉最上层的弹窗 (信息/设置/直播/磁力/种子); 有弹窗被关返回 true
func _close_overlays() -> bool:
	if _live_popup.visible:
		_close_live_popup()
		return true
	if _magnet_popup.visible:
		_close_magnet_popup()
		return true
	if _torrent_popup.visible:
		_close_torrent_popup()
		return true
	if _info_popup.visible:
		_close_info()
		return true
	if _settings_popup and _settings_popup.visible:
		_close_settings()
		return true
	return false

func _any_overlay() -> bool:
	return _live_popup.visible or _magnet_popup.visible or _torrent_popup.visible \
		or _info_popup.visible or (_settings_popup != null and _settings_popup.visible)

# 遮罩随弹窗显隐淡入淡出
func _update_scrim() -> void:
	var want := _any_overlay()
	if _scrim_tween and _scrim_tween.is_valid():
		_scrim_tween.kill()
	_scrim.visible = true
	_scrim_tween = create_tween()
	_scrim_tween.tween_property(_scrim, "modulate:a", 0.45 if want else 0.0, 0.15)
	_scrim_tween.tween_callback(func(): _scrim.visible = want)

# 弹窗弹入动效: scale 0.94→1 + fade (视觉"浮出"而非硬切)。
# 先置透明, 延迟一帧等容器布局完成再取 size 定 pivot, 避免首开 size=0 缩放错位
func _pop_in(p: Control) -> void:
	p.modulate.a = 0.0
	_pop_in_anim.call_deferred(p)

func _pop_in_anim(p: Control) -> void:
	p.pivot_offset = p.size / 2.0
	p.scale = Vector2.ONE * 0.94
	var tw := create_tween().set_parallel(true)
	tw.tween_property(p, "modulate:a", 1.0, 0.13)
	tw.tween_property(p, "scale", Vector2.ONE, 0.13).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)

# 视频区点按: 弹窗开着点外面 = 关闭(同 ESC); 否则单击切 HUD, 双击按横向 1/3 分段
func _on_video_tap(pos: Vector2) -> void:
	if _close_overlays():
		return
	var now := Time.get_ticks_msec()
	if now - _last_tap_ms <= 350:
		_last_tap_ms = 0
		var w := get_viewport_rect().size.x
		if pos.x < w * 0.33:
			_seek_relative(-10000)
		elif pos.x > w * 0.67:
			_seek_relative(10000)
		else:
			_toggle_fullscreen()
		return
	_last_tap_ms = now
	if _hud_root.modulate.a > 0.5:
		_fade_hud(0.0)
	else:
		_show_hud()

# ── 播放控制 ──
func _toggle_play() -> void:
	if _playing:
		player.pause()
		return
	if player.get_state() == ST_COMPLETED:
		player.seek(0)
	player.resume()

func _seek_relative(delta_ms: int) -> void:
	if _is_live or player.get_duration() <= 0.0:
		return
	_last_seek_ms = int(player.get_position()) + delta_ms
	player.seek(_last_seek_ms)

func _on_scrub_started(_v: float) -> void:
	_scrubbing = true
	_hide_timer.stop()

func _on_scrub_ended(v: float) -> void:
	_scrubbing = false
	if not _is_live:
		_last_seek_ms = int(v * player.get_duration())
		player.seek(_last_seek_ms)
	_show_hud()

func _set_volume(v: float) -> void:
	_vol.value = clampf(v, 0.0, 1.0)

func _toggle_mute() -> void:
	if player.volume > 0.001:
		_mute_before = player.volume
		_set_volume(0.0)
	else:
		_set_volume(_mute_before if _mute_before > 0.001 else 1.0)

func _on_volume_changed(v: float) -> void:
	if player:
		player.volume = v
	_update_vol_icon(v)

func _update_vol_icon(v: float) -> void:
	_mute_btn.icon_kind = IconBtn.Icon.MUTE if v <= 0.001 else IconBtn.Icon.VOL

func _toggle_fullscreen() -> void:
	var fs := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if fs else DisplayServer.WINDOW_MODE_FULLSCREEN)

func _on_speed_id(id: int, speeds: Array) -> void:
	var v: float = speeds[id]
	if player:
		player.set_speed(v)
	_speed.text = "%s ▾" % _speed_str(v)
	var popup := _speed.get_popup()
	for i in speeds.size():
		popup.set_item_checked(i, i == id)

# Godot 的 % 运算符不支持 %g, 用 num() 去尾零并保一位小数
func _speed_str(v: float) -> String:
	return String.num(v, 2).pad_decimals(1) + "×"

# ── 顶部菜单栏 ──
func _style_menu(mb: MenuButton) -> void:
	mb.focus_mode = Control.FOCUS_NONE
	mb.add_theme_font_size_override("font_size", 14)
	mb.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	mb.add_theme_color_override("font_hover_color", Color.WHITE)
	mb.add_theme_color_override("font_pressed_color", Color.WHITE)
	mb.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.20), 5))
	mb.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.30), 5))
	mb.add_theme_stylebox_override("focus", StyleBoxEmpty.new())

# PopupMenu 统一密度: 触屏行高/字号加大 (默认主题行高太小点不中)
func _style_popup_menu(pm: PopupMenu) -> void:
	pm.theme = _theme
	pm.add_theme_font_size_override("font_size", 15 if _touch else 13)
	pm.add_theme_constant_override("v_separation", 9 if _touch else 4)

# hover 中心缩放动效 (以控件中心为轴, Fluent 微动效)
func _hover_scale(b: Button, to: float = 1.08) -> void:
	b.resized.connect(func(): b.pivot_offset = b.size / 2.0)
	b.mouse_entered.connect(func():
		var tw := b.create_tween()
		tw.tween_property(b, "scale", Vector2.ONE * to, 0.12))
	b.mouse_exited.connect(func():
		var tw := b.create_tween()
		tw.tween_property(b, "scale", Vector2.ONE, 0.12))

func _track_popup(pop: PopupMenu) -> void:
	# 菜单开着时保持 HUD 常显
	pop.about_to_popup.connect(func(): _menu_open = true)
	pop.popup_hide.connect(func(): _menu_open = false)

func _on_file_menu(id: int) -> void:
	match id:
		0: _open_file()
		1: _open_live()
		2: _open_magnet()
		10: _back_home()
		9: get_tree().quit()

# 触屏「更多」单菜单 (id 与桌面三菜单不同: 全部平铺)
func _on_more_menu(id: int) -> void:
	match id:
		0: _open_file()
		1: _open_live()
		2: _open_magnet()
		10: _open_info("media")
		11: _open_info("codec")
		12: _open_settings()
		13: _toggle_fullscreen()
		20: _back_home()
		9: get_tree().quit()

func _back_home() -> void:
	# 返回工具主界面 (hub); 场景内节点由各自 NOTIFICATION_EXIT_TREE 清理
	get_tree().change_scene_to_file("res://src/hub/main.tscn")

func _on_tool_menu(id: int) -> void:
	match id:
		0: _open_info("media")
		1: _open_info("codec")
		2: _open_settings()

func _on_view_menu(id: int) -> void:
	match id:
		0: _toggle_fullscreen()

# ── 打开文件 / 直播源 ──

func _open_file() -> void:
	if _file_dialog == null:
		_file_dialog = FileDialog.new()
		_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_file_dialog.filters = PackedStringArray([
			"*.mp4,*.mkv,*.avi,*.mov,*.flv,*.ts,*.m2ts ; 视频文件",
			"*.mp3,*.aac,*.flac,*.wav,*.m4a,*.ogg ; 音频文件",
			"* ; 所有文件",
		])
		_file_dialog.title = "打开媒体文件"
		_file_dialog.theme = _theme
		_file_dialog.file_selected.connect(_on_file_picked)
		add_child(_file_dialog)
	# 记忆上次目录
	if not _last_dir.is_empty() and DirAccess.dir_exists_absolute(_last_dir):
		_file_dialog.current_dir = _last_dir
	_file_dialog.popup_centered_ratio(0.8)

func _open_live() -> void:
	_fit_popup(_live_popup, 400)
	_live_popup.visible = true
	_pop_in(_live_popup)
	_update_scrim()
	_live_edit.text = ""
	# 复位下拉框到表头, 同一项可重复选中
	if _live_combo.get_item_count() > 0:
		_live_combo.select(0)
	_live_edit.call_deferred("grab_focus")

func _on_live_submit(_text: String) -> void:
	_load_live()

func _load_live() -> void:
	var url := _live_edit.text.strip_edges()
	if url.is_empty():
		return
	_close_live_popup()
	_push_live(url)
	_load_url(url)

func _on_live_combo_selected(idx: int) -> void:
	# idx 0 是表头(禁用, 不会被触发); idx>=1 对应 _live_urls[idx-1]
	if idx < 1 or idx - 1 >= _live_urls.size():
		return
	var url := _live_urls[idx - 1]
	_close_live_popup()
	_push_live(url)
	_load_url(url)

func _push_live(url: String) -> void:
	# 去重并提到最前, 超出上限截断(与文件历史同语义)
	var arr: Array = []
	for u in _live_urls:
		if u != url:
			arr.append(u)
	arr.push_front(url)
	if arr.size() > MAX_RECENT:
		arr.resize(MAX_RECENT)
	_live_urls = PackedStringArray(arr)
	_rebuild_live_combo()
	_save_cfg()

func _rebuild_live_combo() -> void:
	if _live_combo == null:
		return
	_live_combo.clear()
	_live_combo.add_item("── 最近直播源 ──", 0)
	_live_combo.set_item_disabled(0, true)
	_live_combo.select(0)
	for i in _live_urls.size():
		_live_combo.add_item(_mid_ellipsis(_live_urls[i]), i + 1)

func _close_live_popup() -> void:
	_live_popup.visible = false
	_update_scrim()

# ── 磁力/BT: 元数据探测 → 文件列表 → 选择播放 ──
# 链路: RemoteSource.open(异步等元数据, 零下载) → open_result 信号 → list("") 列根
# → list_result 信号 → 文件列表弹窗 → resolve(写 torrent.fileIndex) → 走常规 _load_url 播放。

func _open_magnet() -> void:
	_fit_popup(_magnet_popup, 460)
	_magnet_popup.visible = true
	_pop_in(_magnet_popup)
	_update_scrim()
	_magnet_edit.text = ""
	_rebuild_magnet_combo()
	_magnet_edit.call_deferred("grab_focus")

func _close_magnet_popup() -> void:
	_magnet_popup.visible = false
	_update_scrim()

# ── 磁力历史 (与直播源历史同语义: 去重置顶, 上限 MAX_RECENT) ──
# 名字随探测成功回填: 下拉显示种子名而非几百字符的 magnet 串;
# 无名条目显示中段省略 url (btih 头尾保留, 可辨识)

func _mid_ellipsis(s: String, max_len: int = 48) -> String:
	if s.length() <= max_len:
		return s
	var head := int(max_len * 0.6)
	var tail := max_len - head - 1
	return s.substr(0, head) + "…" + s.substr(s.length() - tail)

func _magnet_label(i: int) -> String:
	var seed_nm: String = _magnet_names[i] if i < _magnet_names.size() else ""
	var url: String = _magnet_urls[i] if i < _magnet_urls.size() else ""
	return _mid_ellipsis(url) if seed_nm.is_empty() else seed_nm

func _rebuild_magnet_combo() -> void:
	if _magnet_combo == null:
		return
	_magnet_combo.clear()
	_magnet_combo.add_item("── 最近磁力 ──", 0)
	_magnet_combo.set_item_disabled(0, true)
	_magnet_combo.select(0)
	for i in _magnet_urls.size():
		_magnet_combo.add_item(_mid_ellipsis(_magnet_label(i)), i + 1)

func _on_magnet_combo_selected(idx: int) -> void:
	# idx 0 是表头(禁用); idx>=1 对应 _magnet_urls[idx-1], 选中即解析
	if idx < 1 or idx - 1 >= _magnet_urls.size():
		return
	var url := _magnet_urls[idx - 1]
	_magnet_edit.text = url
	_load_magnet()

func _push_magnet(url: String, seed_name: String = "") -> void:
	if url.is_empty():
		return
	var urls: Array = []
	var names: Array = []
	var known := seed_name
	for i in _magnet_urls.size():
		var u: String = _magnet_urls[i]
		var n: String = _magnet_names[i] if i < _magnet_names.size() else ""
		if u == url:
			if known.is_empty():
				known = n   # 已有条目保留旧名
			continue        # 去重
		urls.append(u)
		names.append(n)
	urls.push_front(url)
	names.push_front(known)
	if urls.size() > MAX_RECENT:
		urls.resize(MAX_RECENT)
		names.resize(MAX_RECENT)
	_magnet_urls = PackedStringArray(urls)
	_magnet_names = PackedStringArray(names)
	_rebuild_magnet_combo()
	_save_cfg()

func _on_magnet_submit(_text: String) -> void:
	_load_magnet()

func _load_magnet() -> void:
	var url := _magnet_edit.text.strip_edges()
	if url.is_empty():
		return
	if _remote == null:
		_remote = RemoteSource.new()
		_remote.open_result.connect(_on_torrent_opened)
		_remote.list_result.connect(_on_torrent_listed)
	if _remote.is_busy():
		_toast_msg("正在解析上一个磁力…")
		return
	_close_magnet_popup()
	_probe_url = url
	_toast_msg("解析磁力元数据中…(最多 %ds)" % (TORRENT_META_TIMEOUT / 1000))
	# cache_dir 传空: 与播放端默认目录一致, 起播可命中元数据缓存免二次等待
	if not _remote.open(url, TORRENT_META_TIMEOUT):
		_toast_msg("解析启动失败(avox_torrent 插件未加载?)")

func _on_torrent_opened(code: int) -> void:
	if code != 0:
		_toast_msg("磁力解析失败: %s" % _remote.get_last_error())
		return
	# 元数据就绪: 列根目录 (open 慢源探测, list 即时枚举)
	if not _remote.list(""):
		_toast_msg("列表失败: %s" % _remote.get_last_error())

func _on_torrent_listed(code: int) -> void:
	if code != 0:
		_toast_msg("磁力解析失败: %s" % _remote.get_last_error())
		return
	# 根批次含目录与文件, 播放弹窗只列可播媒体 (树形下钻 UI 留给后续)
	_torrent_files.clear()
	for e in _remote.get_entries():
		if e["media"]:
			_torrent_files.append(e)
	if _torrent_files.is_empty():
		_toast_msg("种子内没有可播的媒体文件")
		return
	_rebuild_torrent_list()
	_fit_torrent_popup()
	_torrent_popup.visible = true
	_pop_in(_torrent_popup)
	_update_scrim()
	# 解析成功即入历史 (记录种子名, 下拉里直接显示名字而非长 url)
	_push_magnet(_probe_url, _remote.get_session_field("name"))

func _rebuild_torrent_list() -> void:
	_torrent_list.clear()
	_torrent_title.text = "%s  (%s)" % [_remote.get_session_field("name"), _fmt_size(int(_remote.get_session_field("totalSize")))]
	for f in _torrent_files:
		var label := "%s   [%s]" % [f["name"], _fmt_size(f["size"])]
		var idx := _torrent_list.add_item(label)
		_torrent_list.set_item_tooltip(idx, f["token"])
		if f["media"]:
			_torrent_list.set_item_custom_fg_color(idx, ACCENT)
	_torrent_list.select(0)

# 弹窗自适应: 写死的 min 尺寸 (宽560/列表高320) 在桌面逻辑视口 1280x720 下合适,
# 手机高 content_scale 后逻辑视口可缩到 ~800x360, 会顶满整屏 →
# 宽 ≤ 视口 80%, 列表高 ≤ 视口 45% 且随条目数收缩 (条目多时滚动)
func _fit_torrent_popup() -> void:
	var vp := get_viewport_rect().size
	_torrent_popup.custom_minimum_size.x = minf(560.0, vp.x * 0.8)
	var rows := maxi(1, _torrent_list.item_count)
	_torrent_list.custom_minimum_size.y = minf(320.0, minf(vp.y * 0.45, rows * 34.0 + 10.0))

# 弹窗可拖动: 按住标题/空白边缘拖动即可移出软键盘遮挡区 (触摸经 mouse 模拟同样生效);
# LineEdit/按钮等子控件自己消费输入不受影响, 拖动位移存在 offsets 里, 重开弹窗保持。
# 内层 Margin/VBox 显式 PASS, 保证标题条(IGNORE 的 Label)与空白边的事件能冒泡到面板
func _make_popup_draggable(popup: Control) -> void:
	popup.mouse_filter = Control.MOUSE_FILTER_STOP
	for c in popup.find_children("*", "Control", true, false):
		if c is Container:
			c.mouse_filter = Control.MOUSE_FILTER_PASS
	popup.gui_input.connect(func(ev: InputEvent) -> void:
		if ev is InputEventMouseButton and ev.button_index == MOUSE_BUTTON_LEFT:
			_popup_dragging[popup] = ev.pressed
		elif ev is InputEventMouseMotion and _popup_dragging.get(popup, false) \
				and (ev.button_mask & MOUSE_BUTTON_MASK_LEFT):
			popup.position += ev.relative
	)

func _on_torrent_item_activated(idx: int) -> void:
	_pick_torrent_file(idx)

func _on_torrent_pick_pressed() -> void:
	var sel := _torrent_list.get_selected_items()
	if sel.size() > 0:
		_pick_torrent_file(sel[0])

func _pick_torrent_file(idx: int) -> void:
	if idx < 0 or idx >= _torrent_files.size():
		return
	# resolve 产出播放 URL(磁力即原链), 同时把 torrent.fileIndex 写入独立 option;
	# 首次 play 前 avox 播放器还没创建, get_option() 为空, 故转存 MediaPlayer
	# (set_option 暂存, createPlayer 时落库, open 异步必晚于它)
	var opt := AvoxOption.new()
	var play_url: String = _remote.resolve(idx, opt)
	if play_url.is_empty():
		_toast_msg("选择失败: %s" % _remote.get_last_error())
		return
	player.set_option("torrent.fileIndex", opt.get_int("torrent.fileIndex"))
	_torrent_popup.visible = false
	_update_scrim()
	_load_url(play_url)
	# 标题用种子名+文件名, 比 magnet 原始链接友好
	_title.text = "%s · %s" % [_remote.get_session_field("name"), String(_torrent_files[idx]["name"]).get_file()]

func _close_torrent_popup() -> void:
	_torrent_popup.visible = false
	_update_scrim()

func _fmt_size(bytes: int) -> String:
	if bytes >= 1073741824:
		return "%.2f GB" % (bytes / 1073741824.0)
	if bytes >= 1048576:
		return "%.1f MB" % (bytes / 1048576.0)
	if bytes >= 1024:
		return "%.0f KB" % (bytes / 1024.0)
	return "%d B" % bytes

func _load_url(url: String) -> void:
	if url.is_empty():
		return
	_scrubbing = false
	_scrub.value = 0.0
	_scrub.buffered = 0.0
	_time.text = ""
	_is_live = false
	_update_live_ui()
	_empty_hint.visible = false
	_control_bar.modulate.a = 1.0   # 恢复空态降级
	# 丢弃上一路纹理: 插件重开会释放旧 GPU 导入纹理(RID 失效), 先解绑避免显示冻结帧
	_video.texture = null
	_title.text = _display_title(url)
	_center.icon_kind = IconBtn.Icon.ELLIPSIS
	_center.visible = true
	# IO 方案 per-URL 重判 (与 live 流程同语义): C++ 侧 ioPlan 跨 play 存活 (createPlayer 重放),
	# 不重设的话上一个网络流的 zlmediakit 会残留, ZL 拒绝 file schema 导致本地文件卡 opening
	player.set_io_plan(_resolve_io_plan(url))
	_autotest_start(url)
	player.url = url
	player.stop()
	player.play()

func _on_files_dropped(files: PackedStringArray) -> void:
	if files.size() > 0:
		_record_file(files[0])
		_load_url(files[0])

# ── 历史记录(文件对话框记忆 + 最近打开) ──

func _on_file_picked(path: String) -> void:
	_record_file(path)
	_load_url(path)

func _record_file(path: String) -> void:
	_last_dir = path.get_base_dir()
	_push_recent(path)
	_save_cfg()

func _push_recent(path: String) -> void:
	# 去重并提到最前, 超出上限截断
	var arr: Array = []
	for p in _recent_files:
		if p != path:
			arr.append(p)
	arr.push_front(path)
	if arr.size() > MAX_RECENT:
		arr.resize(MAX_RECENT)
	_recent_files = PackedStringArray(arr)
	_rebuild_recent_menu()

func _rebuild_recent_menu() -> void:
	if _recent_popup == null:
		return
	_recent_popup.clear()
	if _recent_files.is_empty():
		_recent_popup.add_item("（无）", 0)
		_recent_popup.set_item_disabled(0, true)
		return
	for i in _recent_files.size():
		var p := _recent_files[i]
		_recent_popup.add_item(p.get_file(), i)
		_recent_popup.set_item_tooltip(i, p)

func _on_recent_menu(id: int) -> void:
	if id >= 0 and id < _recent_files.size():
		_load_url(_recent_files[id])

func _load_cfg() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(CFG_PATH) == OK:
		_recent_files = cfg.get_value("recent", "files", PackedStringArray())
		_last_dir = cfg.get_value("file_dialog", "last_dir", "")
		_live_urls = cfg.get_value("live", "urls", PackedStringArray())
		_magnet_urls = cfg.get_value("magnet", "urls", PackedStringArray())
		_magnet_names = cfg.get_value("magnet", "names", PackedStringArray())
		# 设置: 缺失字段用默认值补 (新加设置不破坏旧 cfg)
		var saved: Dictionary = cfg.get_value("settings", "values", {})
		for k in _DEFAULT_SETTINGS:
			_settings[k] = saved.get(k, _DEFAULT_SETTINGS[k])

func _save_cfg() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("recent", "files", _recent_files)
	cfg.set_value("file_dialog", "last_dir", _last_dir)
	cfg.set_value("live", "urls", _live_urls)
	cfg.set_value("magnet", "urls", _magnet_urls)
	cfg.set_value("magnet", "names", _magnet_names)
	cfg.set_value("settings", "values", _settings)
	cfg.save(CFG_PATH)

# ── 状态反映到 UI ──
func _update_play_btn() -> void:
	_play_btn.icon_kind = IconBtn.Icon.PAUSE if _playing else IconBtn.Icon.PLAY

func _update_center(s: int) -> void:
	match s:
		ST_NONE, ST_OPENING, ST_BUFFERING:
			_center.icon_kind = IconBtn.Icon.ELLIPSIS
			_center.visible = true
		ST_PLAYING:
			_center.visible = false
		_:
			_center.icon_kind = IconBtn.Icon.PLAY
			_center.visible = true

func _update_live_ui() -> void:
	_scrub.disabled = _is_live
	if _is_live:
		_time.text = "● 直播"
	else:
		_time.text = ""

func _on_io_error(code: int) -> void:
	_autotest_report(false, "io_error=%d" % code)
	_toast_msg("IO 错误: %d" % code)

func _on_decode_error(code: int) -> void:
	_autotest_report(false, "decode_error=%d" % code)
	_toast_msg("解码错误: %d" % code)

# ── 自动化判定行: 首次起播打 PASS, IO/解码错误打 FAIL, 一次播放只报一条 ──
func _autotest_start(url: String) -> void:
	_autotest_url = url
	_autotest_t0 = Time.get_ticks_msec()

func _autotest_report(passed: bool, detail := "") -> void:
	if _autotest_url.is_empty():
		return
	var tail := "" if detail.is_empty() else " " + detail
	print("[AVOX][TEST] case=url-play result=%s url=%s t=%dms%s" % [
		"PASS" if passed else "FAIL", _autotest_url,
		Time.get_ticks_msec() - _autotest_t0, tail])
	_autotest_url = ""

func _toast_msg(msg: String) -> void:
	_toast_label.text = msg
	_toast.modulate.a = 1.0
	var tw := create_tween()
	tw.tween_interval(2.5)
	tw.tween_property(_toast, "modulate:a", 0.0, 0.4)

func _fmt(ms: float) -> String:
	var t := int(ms / 1000.0)
	if t >= 3600:
		return "%d:%02d:%02d" % [t / 3600, (t / 60) % 60, t % 60]
	return "%02d:%02d" % [t / 60, t % 60]

# 播放地址 → 标题: 磁力显示 btih 前 8 位 (原始链接几百字符没法看)
func _display_title(url: String) -> String:
	if url.begins_with("magnet:"):
		var i := url.find("btih:") + 5
		return "磁力下载 · %s…" % url.substr(i, 8)
	return url.get_file() if url.get_extension() != "" else url

# ── 媒体/编解码信息面板 ──
func _open_info(kind: String) -> void:
	if player == null:
		return
	var info: Dictionary = player.get_media_info()
	_info_title.text = "媒体信息" if kind == "media" else "编解码信息"
	for c in _info_grid.get_children():
		c.queue_free()
	var url: String = info.get("url", "")
	var dur: float = float(info.get("duration_ms", 0.0))
	var vids: Array = info.get("videos", [])
	var auds: Array = info.get("audios", [])
	if kind == "media":
		_add_info_row("位置", url)
		_add_info_row("时长", _fmt(dur) if dur > 0.0 else "—")
		_add_info_row("可定位", "是" if bool(info.get("can_seek", false)) else "否")
		_add_info_row("视频流", "%d" % vids.size())
		_add_info_row("音频流", "%d" % auds.size())
		if vids.size() > 0:
			var v0: Dictionary = vids[0]
			_add_info_row("分辨率", "%dx%d" % [int(v0.get("width", 0)), int(v0.get("height", 0))])
			_add_info_row("帧率", "%.2f fps" % float(v0.get("fps", 0)))
	else:
		for i in vids.size():
			var v: Dictionary = vids[i]
			var tag := "视频 #%d" % i if vids.size() > 1 else "视频"
			_add_info_row("%s · 编码" % tag, String(v.get("codec", "")))
			_add_info_row("%s · 分辨率" % tag, "%dx%d" % [int(v.get("width", 0)), int(v.get("height", 0))])
			_add_info_row("%s · 帧率" % tag, "%.2f fps" % float(v.get("fps", 0)))
			_add_info_row("%s · 像素格式" % tag, String(v.get("pixel_format", "")))
			_add_info_row("%s · 色彩空间" % tag, String(v.get("color_space", "")))
		for i in auds.size():
			var a: Dictionary = auds[i]
			var tag := "音频 #%d" % i if auds.size() > 1 else "音频"
			_add_info_row("%s · 编码" % tag, String(a.get("codec", "")))
			_add_info_row("%s · 采样率" % tag, "%d Hz" % int(a.get("sample_rate", 0)))
			_add_info_row("%s · 声道" % tag, "%d" % int(a.get("channels", 0)))
			_add_info_row("%s · 样本格式" % tag, String(a.get("format", "")))
		if vids.is_empty() and auds.is_empty():
			_add_info_row("—", "未就绪, 请先打开媒体")
	# 宽度收缩到内容 (键列+值列+边距), 320..视口内 460 之间; 高度走 _fit_popup
	var cw := _info_grid.get_combined_minimum_size().x + 36 + 30
	_info_popup.custom_minimum_size.x = clampf(cw, 320.0, minf(460.0, get_viewport_rect().size.x * 0.9))
	_fit_popup(_info_popup)
	# 内容少时收缩到实际高度, 超出再限高滚动
	_info_scroll.custom_minimum_size.y = minf(_info_grid.get_combined_minimum_size().y,
		minf(320.0, get_viewport_rect().size.y * 0.5))
	_info_popup.visible = true
	_pop_in(_info_popup)
	_update_scrim()
	_menu_open = true

func _close_info() -> void:
	_info_popup.visible = false
	_update_scrim()
	_menu_open = false

# ── 设置面板 (cmdPlay 选项的 GDScript 镜像) ──
# IO 方案 / 硬解 / 三个日志开关。下次打开/重启生效 (avox 侧 setIoPlan/option
# 注释语义: 仅新会话生效)。写回 _settings + 立即 cfg 落盘。
func _open_settings() -> void:
	if _settings_popup == null:
		_build_settings_popup()
		# 懒构建晚于启动时的磨砂挂载, 这里按当前开关补上
		if _settings[SETTING_ACRYLIC]:
			_apply_acrylic(_settings_popup, 10.0)
	# 用当前 _settings 回填 (重开时与最新值对齐)
	_refresh_settings_popup()
	_fit_popup(_settings_popup, 420)
	# 内容区限高(小屏逻辑视口矮): 内容少收缩到实际高度, 超出滚动
	if _settings_scroll.get_child_count() > 0:
		var content := _settings_scroll.get_child(0) as Control
		_settings_scroll.custom_minimum_size.y = minf(content.get_combined_minimum_size().y,
			minf(360.0, get_viewport_rect().size.y * 0.55))
	_settings_popup.visible = true
	_pop_in(_settings_popup)
	_update_scrim()
	_menu_open = true

func _close_settings() -> void:
	_settings_popup.visible = false
	_update_scrim()
	_menu_open = false

func _refresh_settings_popup() -> void:
	# IO 方案下拉: 0=auto, 1=ffmpeg, 2=zlmediakit (按 _DEFAULT_SETTINGS 顺序)
	var idx := 0
	match _settings[SETTING_IO_PLAN]:
		"ffmpeg":     idx = 1
		"zlmediakit": idx = 2
	_settings_io_combo.select(idx)
	_settings_hard_decode.button_pressed = _settings[SETTING_HARD_DECODE]
	_settings_log_packet.button_pressed = _settings[SETTING_LOG_PACKET]
	_settings_log_decode.button_pressed = _settings[SETTING_LOG_DECODE]
	_settings_log_render.button_pressed = _settings[SETTING_LOG_RENDER]
	_settings_acrylic.button_pressed = _settings[SETTING_ACRYLIC]

func _apply_settings_from_popup() -> void:
	# 读 popup 当前控件值 -> 写回 _settings -> cfg 落盘 -> 提示下次生效
	var plan_name := "auto"
	match _settings_io_combo.get_selected_id():
		1: plan_name = "ffmpeg"
		2: plan_name = "zlmediakit"
	_settings[SETTING_IO_PLAN]     = plan_name
	_settings[SETTING_HARD_DECODE] = _settings_hard_decode.button_pressed
	_settings[SETTING_LOG_PACKET]  = _settings_log_packet.button_pressed
	_settings[SETTING_LOG_DECODE]  = _settings_log_decode.button_pressed
	_settings[SETTING_LOG_RENDER]  = _settings_log_render.button_pressed
	_settings[SETTING_ACRYLIC]     = _settings_acrylic.button_pressed
	# 硬解可即时: player 已存在时立即下发 (若刚构造未 play, _build_player 也会读到新值)
	if player:
		player.hard_decode = _settings[SETTING_HARD_DECODE]
	_set_acrylic_enabled(_settings[SETTING_ACRYLIC])
	_save_cfg()
	_toast_msg("已保存, 多数选项下次打开生效")
	_close_settings()

func _add_info_row(k: String, v: String) -> void:
	var kl := Label.new()
	kl.text = k
	kl.add_theme_font_size_override("font_size", 14)
	kl.add_theme_color_override("font_color", Color(0.6, 0.64, 0.7))
	kl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_info_grid.add_child(kl)
	var vl := Label.new()
	vl.text = v if v != "" else "—"
	vl.add_theme_font_size_override("font_size", 14)
	vl.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	vl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_info_grid.add_child(vl)

# ── UI 构建 ──
func _flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	return UiKit.flat(bg, radius, border, bw)

func _vol_seg(bg: Color, radius: int = 3) -> StyleBoxFlat:
	# HSlider 轨道/已选区高度取自 stylebox 的 content_margin; 默认 0 → 0 高度看不见
	var sb := _flat(bg, radius)
	sb.set_content_margin_all(radius)
	return sb

# ── Acrylic 磨砂面板 ──
# 面板本体 stylebox 置空, 由 shader 画模糊视频 + 色调 + 圆角描边; 关闭时回退原样式。
func _set_acrylic_enabled(on: bool) -> void:
	if on:
		if _acrylic_entries.is_empty():
			_apply_acrylic(_control_bar, 12.0)
			_apply_acrylic(_top_bar, 0.0)
			_apply_acrylic(_info_popup, 10.0)
			_apply_acrylic(_live_popup, 10.0)
			if _settings_popup != null:
				_apply_acrylic(_settings_popup, 10.0)
	else:
		_clear_acrylic()

func _apply_acrylic(panel: PanelContainer, radius: float) -> void:
	var mat := ShaderMaterial.new()
	mat.shader = ACRYLIC_SHADER
	mat.set_shader_parameter("corner_radius", radius)
	mat.set_shader_parameter("tint", PANEL_BG)
	mat.set_shader_parameter("border_color", Color(1, 1, 1, 0.08))
	# stylebox 必须产出绘制几何 (StyleBoxEmpty 零像素 → Control 不触发绘制, shader 永不执行);
	# 输出颜色/圆角/描边全部由 acrylic shader 决定, 这里的颜色只是占位
	panel.add_theme_stylebox_override("panel", _flat(Color.WHITE))
	panel.material = mat
	_acrylic_entries.append({"panel": panel, "radius": radius, "fallback": _acrylic_fallback(panel, radius)})

# 关闭磨砂时回退用的 stylebox (与构建时同款)
func _acrylic_fallback(panel: PanelContainer, radius: float) -> StyleBoxFlat:
	if panel == _control_bar:
		return _flat(PANEL_BG, 12, Color(1, 1, 1, 0.08), 1)
	if panel == _top_bar:
		return _flat(Color(0, 0, 0, 0.35), 0, Color(1, 1, 1, 0.08), 1)
	return _flat(Color(0.05, 0.06, 0.08, 0.96), int(radius), Color(1, 1, 1, 0.12), 1)

func _clear_acrylic() -> void:
	for e in _acrylic_entries:
		var p: PanelContainer = e.panel
		p.material = null
		p.add_theme_stylebox_override("panel", e.fallback)
	_acrylic_entries.clear()

# 每帧同步 shader uniform: 视频纹理 + 面板/视频内容的屏幕归一化矩形
func _update_acrylic() -> void:
	if _acrylic_entries.is_empty():
		return
	var vp_size := get_viewport().get_visible_rect().size
	if vp_size.x <= 0.0 or vp_size.y <= 0.0:
		return
	# 视频内容矩形 (TextureRect 全屏 + KEEP_ASPECT_CENTERED 的实际绘制区)
	var tex := _video.texture
	var content := Rect2(Vector2.ZERO, vp_size)
	if tex != null and tex.get_width() > 0 and tex.get_height() > 0:
		var ta := tex.get_width() / float(tex.get_height())
		var ca := vp_size.x / vp_size.y
		if ta > ca:
			content.size.y = vp_size.x / ta
			content.position.y = (vp_size.y - content.size.y) * 0.5
		else:
			content.size.x = vp_size.y * ta
			content.position.x = (vp_size.x - content.size.x) * 0.5
	var crect := Rect2(content.position / vp_size, content.size / vp_size)
	for e in _acrylic_entries:
		var p: PanelContainer = e.panel
		var mat := p.material as ShaderMaterial
		if mat == null:
			continue
		var gr := p.get_global_rect()
		# HUD 为无变换 CanvasLayer, 全局矩形即屏幕矩形
		mat.set_shader_parameter("has_tex", 1.0 if tex != null else 0.0)
		if tex != null:
			mat.set_shader_parameter("video_tex", tex)
		mat.set_shader_parameter("panel_rect", Rect2(gr.position / vp_size, gr.size / vp_size))
		mat.set_shader_parameter("video_rect", crect)
		mat.set_shader_parameter("pixel_size", gr.size)
		mat.set_shader_parameter("screen_size", vp_size)

# ── 统一主题与样式 helper (实现见 src/common/ui_kit.gd, 与 hub 共用) ──
func _build_theme() -> Theme:
	return UiKit.build_theme()

# 弹窗统一深色圆角面板
func _style_dialog(popup: PanelContainer) -> void:
	popup.add_theme_stylebox_override("panel", _flat(Color(0.05, 0.06, 0.08, 0.96), 12, Color(1, 1, 1, 0.12), 1))

# 弹窗视口自适应: 宽 ≤ 90%、高 ≤ 85% (手机高缩放后逻辑视口小, 写死宽度会溢出)
func _fit_popup(popup: Control, min_w: float = 0.0) -> void:
	var vp := get_viewport_rect().size
	if min_w > 0.0:
		popup.custom_minimum_size.x = minf(min_w, vp.x * 0.9)
	if popup.custom_minimum_size.y > 0.0:
		popup.custom_minimum_size.y = minf(popup.custom_minimum_size.y, vp.y * 0.85)

func _style_primary(b: Button, min_w: float = 88.0) -> void:
	b.custom_minimum_size = Vector2(min_w, _input_h)
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_stylebox_override("normal", _flat(ACCENT, 6))
	b.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 6))
	b.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 6))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	b.add_theme_color_override("font_color", Color.WHITE)

func _style_ghost(b: Button, min_w: float = 72.0) -> void:
	b.custom_minimum_size = Vector2(min_w, _input_h)
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.08), 6))
	b.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.16), 6))
	b.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.12), 6))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	b.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))

func _style_combo(c: OptionButton, min_w: float) -> void:
	c.custom_minimum_size = Vector2(min_w, _input_h)
	c.clip_text = true   # 选中项长源不撑宽弹窗
	c.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	c.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.14), 6))
	c.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.10), 6))
	c.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	c.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	c.add_theme_color_override("font_hover_color", Color.WHITE)
	_style_popup_menu(c.get_popup())

# 软键盘避让: Android 键盘弹出时把输入弹窗顶到键盘上方 (键盘高度是窗口像素, 按总缩放转画布坐标)
func _avoid_keyboard() -> void:
	if not OS.has_feature("android"):
		return
	var kb_px := DisplayServer.virtual_keyboard_get_height()
	if kb_px <= 0.0:
		return
	var vp := get_viewport_rect().size
	var total_scale := get_window().size.x / maxf(vp.x, 1.0)
	var kb := kb_px / maxf(total_scale, 0.1)
	for p: Control in [_live_popup, _magnet_popup]:
		if p.visible and p.position.y + p.size.y > vp.y - kb:
			p.position.y = maxf(0.0, vp.y - kb - p.size.y - 8.0)

# 跨显示器 DPI 跟随 (桌面): 窗口拖到不同缩放率的屏幕时按比例重排窗口 (0.5s 轮询);
# 同屏不动 (避免与用户手动调窗打架), 只响应换屏
func _check_screen_scale() -> void:
	if not (OS.has_feature("windows") or OS.has_feature("linux")):
		return
	var now := Time.get_ticks_msec()
	if now - _last_dpi_check_ms < 500:
		return
	_last_dpi_check_ms = now
	var win := get_window()
	if DisplayServer.window_get_mode() != DisplayServer.WINDOW_MODE_WINDOWED:
		return
	var scr := win.current_screen
	if scr == _dpi_screen or scr < 0:
		return
	var sc := UiKit.desktop_scale(scr)
	var ratio := sc / maxf(_dpi_scale, 0.01)
	_dpi_screen = scr
	_dpi_scale = sc
	if win.size.x > 0:

		win.size = Vector2i(int(win.size.x * ratio), int(win.size.y * ratio))
		win.move_to_center()

func _mk_icon_btn(kind: int, tip: String = "", ratio: float = 0.5) -> IconButton:
	var b := IconButton.new()
	b.icon_kind = kind
	b.icon_ratio = ratio
	b.custom_minimum_size = Vector2(_btn_h, _btn_h)
	b.focus_mode = Control.FOCUS_NONE
	b.tooltip_text = tip
	b.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	b.add_theme_color_override("font_hover_color", Color.WHITE)
	b.add_theme_color_override("font_pressed_color", Color.WHITE)
	b.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	b.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.20), 6))
	b.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.32), 6))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_hover_scale(b)
	return b

func _build_ui() -> void:
	# 深色衬底: 无视频帧时窗口不再是默认灰, 与品牌深色一致
	var bg := ColorRect.new()
	bg.color = Color("#0B0F16")
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(bg)

	# 视频层
	_video = TextureRect.new()
	_video.set_anchors_preset(Control.PRESET_FULL_RECT)
	_video.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_video.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_video)

	# 底部渐变压暗(衬控制条, 纯 alpha 渐变零开销); 0.62 起坡早, 亮画面下控制条仍可读
	var vig := TextureRect.new()
	vig.set_anchors_preset(Control.PRESET_FULL_RECT)
	var grad := Gradient.new()
	grad.offsets = PackedFloat32Array([0.0, 0.45, 1.0])
	grad.colors = PackedColorArray([Color(0, 0, 0, 0.0), Color(0, 0, 0, 0.0), Color(0, 0, 0, 0.62)])
	var gtex := GradientTexture2D.new()
	gtex.gradient = grad
	gtex.fill_from = Vector2(0, 0)
	gtex.fill_to = Vector2(0, 1)
	vig.texture = gtex
	vig.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(vig)

	# 空态门面: 居中大播放图标 + 提示 (有媒体即隐藏)
	_empty_hint = VBoxContainer.new()
	_empty_hint.set_anchors_preset(Control.PRESET_CENTER)
	_empty_hint.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_empty_hint.grow_vertical = Control.GROW_DIRECTION_BOTH
	_empty_hint.alignment = BoxContainer.ALIGNMENT_CENTER
	_empty_hint.add_theme_constant_override("separation", 14)
	_empty_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_empty_hint.visible = false
	var eh_icon := IconView.new()
	eh_icon.icon_kind = IconButton.Icon.PLAY
	eh_icon.icon_ratio = 0.5
	eh_icon.icon_color = Color(1, 1, 1, 0.14)
	eh_icon.custom_minimum_size = Vector2(88, 88)
	eh_icon.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	eh_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_empty_hint.add_child(eh_icon)
	var eh_lbl := Label.new()
	eh_lbl.text = "拖入文件，或用「文件」菜单打开"
	eh_lbl.add_theme_color_override("font_color", Color(1, 1, 1, 0.5))
	eh_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	eh_lbl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_empty_hint.add_child(eh_lbl)
	add_child(_empty_hint)

	# HUD 层
	_hud = CanvasLayer.new()
	_hud.layer = 10
	add_child(_hud)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.theme = _theme   # 默认字体(SystemFont 中文字体链) + 基准字号, 弹窗/菜单经此继承
	_hud_root = root
	_hud.add_child(root)

	# 顶部 Fluent 标题条: 全宽磨砂细条, 左标题右菜单, 随 HUD 一起淡入淡出
	_top_bar = PanelContainer.new()
	_top_bar.set_anchors_preset(Control.PRESET_TOP_WIDE)
	_top_bar.offset_bottom = 48 if _touch else 44
	_top_bar.add_theme_stylebox_override("panel", _flat(Color(0, 0, 0, 0.35), 0, Color(1, 1, 1, 0.08), 1))
	_top_bar.mouse_entered.connect(func(): _over_menu = true)
	_top_bar.mouse_exited.connect(func(): _over_menu = false)
	root.add_child(_top_bar)
	var top_inner := MarginContainer.new()
	top_inner.add_theme_constant_override("margin_left", 14)
	top_inner.add_theme_constant_override("margin_right", 8)
	top_inner.add_theme_constant_override("margin_top", 6)
	top_inner.add_theme_constant_override("margin_bottom", 6)
	_top_bar.add_child(top_inner)
	var top_hbox := HBoxContainer.new()
	top_hbox.add_theme_constant_override("separation", 4)
	top_inner.add_child(top_hbox)
	_title = Label.new()
	_title.add_theme_font_override("font", UiKit.font_weight(500))
	_title.add_theme_font_size_override("font_size", 15)
	_title.add_theme_color_override("font_color", Color(1, 1, 1, 0.92))
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_title.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_title.mouse_filter = Control.MOUSE_FILTER_IGNORE
	top_hbox.add_child(_title)
	var menu_bar := HBoxContainer.new()
	menu_bar.add_theme_constant_override("separation", 2)
	top_hbox.add_child(menu_bar)
	if _touch:
		# 触屏: 文件/工具/视图 三菜单是桌面范式 (小目标难点), 收敛为单个「更多」大行弹层
		_recent_popup = PopupMenu.new()
		_recent_popup.name = "RecentFiles"
		_style_popup_menu(_recent_popup)
		_rebuild_recent_menu()
		_recent_popup.id_pressed.connect(_on_recent_menu)
		var m_more := MenuButton.new()
		m_more.text = "更多"
		_style_menu(m_more)
		var pm := m_more.get_popup()
		_style_popup_menu(pm)
		pm.add_item("打开文件…", 0)
		pm.add_item("打开直播源…", 1)
		pm.add_item("打开磁力/BT…", 2)
		pm.add_child(_recent_popup)
		pm.add_submenu_item("最近打开", "RecentFiles")
		pm.add_separator()
		pm.add_item("媒体信息", 10)
		pm.add_item("编解码信息", 11)
		pm.add_item("设置...", 12)
		pm.add_item("全屏", 13)
		pm.add_separator()
		pm.add_item("返回主页", 20)
		pm.add_item("退出", 9)
		pm.id_pressed.connect(_on_more_menu)
		_track_popup(pm)
		menu_bar.add_child(m_more)
	else:
		# 文件
		var m_file := MenuButton.new()
		m_file.text = "文件"
		_style_menu(m_file)
		var pf := m_file.get_popup()
		_style_popup_menu(pf)
		pf.add_item("打开文件…", 0)
		pf.add_item("打开直播源…", 1)
		pf.add_item("打开磁力/BT…", 2)
		# 最近打开(持久化历史, 点名直接重开)
		_recent_popup = PopupMenu.new()
		_recent_popup.name = "RecentFiles"
		_style_popup_menu(_recent_popup)
		pf.add_child(_recent_popup)
		_rebuild_recent_menu()
		_recent_popup.id_pressed.connect(_on_recent_menu)
		pf.add_submenu_item("最近打开", "RecentFiles")
		pf.add_separator()
		pf.add_item("返回主页", 10)
		pf.add_separator()
		pf.add_item("退出", 9)
		pf.id_pressed.connect(_on_file_menu)
		_track_popup(pf)
		menu_bar.add_child(m_file)
		# 工具
		var m_tool := MenuButton.new()
		m_tool.text = "工具"
		_style_menu(m_tool)
		var pt := m_tool.get_popup()
		_style_popup_menu(pt)
		pt.add_item("媒体信息", 0)
		pt.add_item("编解码信息", 1)
		pt.add_separator()
		pt.add_item("设置...", 2)
		pt.id_pressed.connect(_on_tool_menu)
		_track_popup(pt)
		menu_bar.add_child(m_tool)
		# 视图
		var m_view := MenuButton.new()
		m_view.text = "视图"
		_style_menu(m_view)
		var pv := m_view.get_popup()
		_style_popup_menu(pv)
		pv.add_item("全屏", 0)
		pv.id_pressed.connect(_on_view_menu)
		_track_popup(pv)
		menu_bar.add_child(m_view)

	# 中心大播放钮
	_center = IconButton.new()
	_center.icon_kind = IconBtn.Icon.ELLIPSIS
	_center.icon_ratio = 0.42
	_center.custom_minimum_size = Vector2(72, 72)
	_center.set_anchors_preset(Control.PRESET_CENTER)
	_center.focus_mode = Control.FOCUS_NONE
	_center.add_theme_color_override("font_color", Color.WHITE)
	_center.add_theme_color_override("font_hover_color", Color.WHITE)
	_center.add_theme_color_override("font_pressed_color", Color.WHITE)
	_center.add_theme_stylebox_override("normal", _flat(Color(0, 0, 0, 0.35), 36, Color(1, 1, 1, 0.25), 1))
	_center.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 36))
	_center.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 36))
	_center.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_hover_scale(_center, 1.05)
	_center.pressed.connect(_toggle_play)
	root.add_child(_center)

	# 控制条
	_control_bar = PanelContainer.new()
	_control_bar.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_control_bar.offset_left = 12
	_control_bar.offset_right = -12
	# 两行(通栏进度行+按钮行): 高度按 token 算 (边距12*2 + 进度 + 间距6 + 按钮)
	_control_bar.offset_top = -(24 + _scrub_h + 6 + _btn_h)
	_control_bar.offset_bottom = -12
	_control_bar.add_theme_stylebox_override("panel", _flat(PANEL_BG, 12, Color(1, 1, 1, 0.08), 1))
	_control_bar.mouse_entered.connect(func(): _over_bar = true)
	_control_bar.mouse_exited.connect(func(): _over_bar = false)
	root.add_child(_control_bar)

	var inner := MarginContainer.new()
	inner.add_theme_constant_override("margin_left", 12)
	inner.add_theme_constant_override("margin_right", 12)
	inner.add_theme_constant_override("margin_top", 12)
	inner.add_theme_constant_override("margin_bottom", 12)
	_control_bar.add_child(inner)

	# 两行布局: 上行进度条通栏, 下行按钮(传输/时间/音量/速度/...)
	# (移动端横向空间紧, 进度条挤在按钮行里被压得太短难拖)
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 6)
	inner.add_child(rows)

	# 进度条: 独立首行通栏(含缓冲进度), 拖动热区大
	_scrub = ScrubBar.new()
	_scrub.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scrub.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_scrub.scrub_started.connect(_on_scrub_started)
	_scrub.scrub_moved.connect(func(v: float): _scrub.value = v)
	_scrub.scrub_ended.connect(_on_scrub_ended)
	_scrub.always_grabber = _touch
	rows.add_child(_scrub)
	_scrub.custom_minimum_size = Vector2(0, _scrub_h)   # add_child 后设, 覆盖 ScrubBar._ready 的默认 24

	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 12)
	rows.add_child(hbox)

	# 后退 5s
	var back_btn := _mk_icon_btn(IconBtn.Icon.REW, "后退 5s (Shift 60s / 双击左侧)")
	back_btn.pressed.connect(_seek_relative.bind(-5000))
	hbox.add_child(back_btn)

	# 播放/暂停
	_play_btn = _mk_icon_btn(IconBtn.Icon.PLAY)
	_play_btn.pressed.connect(_toggle_play)
	hbox.add_child(_play_btn)

	# 前进 5s
	var fwd_btn := _mk_icon_btn(IconBtn.Icon.FFWD, "前进 5s (Shift 60s / 双击右侧)")
	fwd_btn.pressed.connect(_seek_relative.bind(5000))
	hbox.add_child(fwd_btn)

	# 时间
	_time = Label.new()
	_time.custom_minimum_size = Vector2(126, 0)
	_time.add_theme_font_override("font", UiKit.font_weight(500))
	_time.add_theme_font_size_override("font_size", 14)
	_time.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	_time.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_time.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(_time)

	# 弹性 spacer: 传输簇靠左, 次级控件簇(音量/倍速/更多/全屏)靠右 —— 标准播放器布局
	var t_spacer := Control.new()
	t_spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	t_spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(t_spacer)

	# 静音/音量: 触屏走硬件键, 隐藏省横向空间 (滑条/grabber 触屏也难点)
	_mute_btn = _mk_icon_btn(IconBtn.Icon.VOL)
	_mute_btn.pressed.connect(_toggle_mute)
	hbox.add_child(_mute_btn)

	_vol = HSlider.new()
	_vol.custom_minimum_size = Vector2(96, 28)
	_vol.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_vol.min_value = 0.0
	_vol.max_value = 1.0
	_vol.step = 0.01
	_vol.value = 1.0
	_vol.focus_mode = Control.FOCUS_NONE
	_vol.add_theme_stylebox_override("slider", _vol_seg(Color(1, 1, 1, 0.22)))
	# 已选区用 ACCENT(与进度条已播色一致), 否则与轨道同色看不清
	_vol.add_theme_stylebox_override("grabber_area", _vol_seg(ACCENT))
	_vol.add_theme_stylebox_override("grabber_area_highlight", _vol_seg(ACCENT))
	# 滑块儿: content_margin 给尺寸(默认 0 会消失); 深色描边保证亮/暗背景下都可见
	var _grab := _flat(Color.WHITE, 7, Color(0.06, 0.09, 0.15, 0.9), 2)
	_grab.set_content_margin_all(7)
	_vol.add_theme_stylebox_override("grabber", _grab)
	var _grab_h := _flat(Color.WHITE, 8, Color(0.06, 0.09, 0.15, 0.9), 2)
	_grab_h.set_content_margin_all(8)
	_vol.add_theme_stylebox_override("grabber_highlight", _grab_h)
	_vol.value_changed.connect(_on_volume_changed)
	hbox.add_child(_vol)
	if _touch:
		_vol.visible = false
		_mute_btn.visible = false

	# 倍速
	_speed = MenuButton.new()
	_speed.text = "1.0× ▾"
	_speed.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_speed.focus_mode = Control.FOCUS_NONE
	_speed.add_theme_font_size_override("font_size", 14)
	_speed.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	_speed.add_theme_color_override("font_hover_color", Color.WHITE)
	_speed.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	_speed.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.20), 6))
	_speed.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.32), 6))
	_speed.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	var speeds := [0.5, 1.0, 2.0, 4.0, 8.0, 16.0]
	_style_popup_menu(_speed.get_popup())
	for i in speeds.size():
		_speed.get_popup().add_radio_check_item(_speed_str(speeds[i]), i)
	_speed.get_popup().set_item_checked(1, true)
	_speed.get_popup().id_pressed.connect(_on_speed_id.bind(speeds))
	hbox.add_child(_speed)

	# 更多(占位)
	var more_btn := _mk_icon_btn(IconBtn.Icon.MORE, "更多: 画质/音轨/字幕(规划中)")
	more_btn.pressed.connect(func(): _toast_msg("更多: 后续接画质/音轨/字幕"))
	hbox.add_child(more_btn)

	# 全屏
	_full_btn = _mk_icon_btn(IconBtn.Icon.FULLSCREEN, "全屏 (F)")
	_full_btn.pressed.connect(_toggle_fullscreen)
	hbox.add_child(_full_btn)

	# 弹窗遮罩: 模态感 + 亮画面下弹窗可读性; 点击关闭 (在弹窗前 add, 绘制序在其下)
	_scrim = ColorRect.new()
	_scrim.color = Color(0, 0, 0, 0.45)
	_scrim.set_anchors_preset(Control.PRESET_FULL_RECT)
	_scrim.mouse_filter = Control.MOUSE_FILTER_STOP
	_scrim.gui_input.connect(func(ev: InputEvent) -> void:
		if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT:
			_close_overlays())
	_scrim.visible = false
	_scrim.modulate.a = 0.0
	root.add_child(_scrim)

	# 直播源输入弹窗
	_live_popup = PanelContainer.new()
	_live_popup.visible = false
	_live_popup.set_anchors_preset(Control.PRESET_CENTER)
	_live_popup.grow_horizontal = Control.GROW_DIRECTION_BOTH   # 内容尺寸变化时围绕中心对称生长(否则向右下长出去)
	_live_popup.grow_vertical = Control.GROW_DIRECTION_BOTH
	_style_dialog(_live_popup)
	var lp_inner := MarginContainer.new()
	lp_inner.add_theme_constant_override("margin_left", 20)
	lp_inner.add_theme_constant_override("margin_right", 20)
	lp_inner.add_theme_constant_override("margin_top", 16)
	lp_inner.add_theme_constant_override("margin_bottom", 16)
	_live_popup.add_child(lp_inner)
	var lp_box := VBoxContainer.new()
	lp_box.add_theme_constant_override("separation", 10)
	lp_inner.add_child(lp_box)
	var lp_title := Label.new()
	lp_title.text = "打开直播源"
	lp_title.add_theme_font_override("font", UiKit.font_weight(600))
	lp_title.add_theme_font_size_override("font_size", 16)
	lp_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	lp_box.add_child(lp_title)
	# 最近直播源下拉框(最新置顶, 选定即播放); 无历史时只有表头
	_live_combo = OptionButton.new()
	_style_combo(_live_combo, 360)
	_live_combo.item_selected.connect(_on_live_combo_selected)
	lp_box.add_child(_live_combo)
	_rebuild_live_combo()
	_live_edit = LineEdit.new()
	_live_edit.placeholder_text = "rtmp:// / rtsp:// / http(s):// 地址"
	_live_edit.custom_minimum_size = Vector2(360, _input_h)
	_live_edit.text_submitted.connect(_on_live_submit)
	lp_box.add_child(_live_edit)
	var lp_btns := HBoxContainer.new()
	lp_btns.alignment = BoxContainer.ALIGNMENT_END
	lp_btns.add_theme_constant_override("separation", 8)
	lp_box.add_child(lp_btns)
	var play_live := Button.new()
	play_live.text = "播放"
	_style_primary(play_live)
	play_live.pressed.connect(_load_live)
	lp_btns.add_child(play_live)
	var cancel_live := Button.new()
	cancel_live.text = "取消"
	_style_ghost(cancel_live)
	cancel_live.pressed.connect(_close_live_popup)
	lp_btns.add_child(cancel_live)
	root.add_child(_live_popup)

	# 磁力/BT 输入弹窗
	_magnet_popup = PanelContainer.new()
	_magnet_popup.visible = false
	_magnet_popup.set_anchors_preset(Control.PRESET_CENTER)
	_magnet_popup.grow_horizontal = Control.GROW_DIRECTION_BOTH   # 内容尺寸变化时围绕中心对称生长(否则向右下长出去)
	_magnet_popup.grow_vertical = Control.GROW_DIRECTION_BOTH
	_style_dialog(_magnet_popup)
	var mp_inner := MarginContainer.new()
	mp_inner.mouse_filter = Control.MOUSE_FILTER_PASS
	mp_inner.add_theme_constant_override("margin_left", 20)
	mp_inner.add_theme_constant_override("margin_right", 20)
	mp_inner.add_theme_constant_override("margin_top", 16)
	mp_inner.add_theme_constant_override("margin_bottom", 16)
	_magnet_popup.add_child(mp_inner)
	var mp_box := VBoxContainer.new()
	mp_box.add_theme_constant_override("separation", 10)
	mp_inner.add_child(mp_box)
	var mp_title := Label.new()
	mp_title.text = "打开磁力/BT"
	mp_title.add_theme_font_override("font", UiKit.font_weight(600))
	mp_title.add_theme_font_size_override("font_size", 16)
	mp_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	mp_box.add_child(mp_title)
	# 最近磁力下拉(最新置顶, 选中即解析); 显示种子名(探测成功回填), 无名截断显示 url
	_magnet_combo = OptionButton.new()
	_style_combo(_magnet_combo, 420)
	_magnet_combo.item_selected.connect(_on_magnet_combo_selected)
	mp_box.add_child(_magnet_combo)
	_rebuild_magnet_combo()
	_magnet_edit = LineEdit.new()
	_magnet_edit.placeholder_text = "magnet:?xt=urn:btih:… 或本地 .torrent 路径"
	_magnet_edit.custom_minimum_size = Vector2(420, _input_h)
	_magnet_edit.text_submitted.connect(_on_magnet_submit)
	mp_box.add_child(_magnet_edit)
	var mp_btns := HBoxContainer.new()
	mp_btns.alignment = BoxContainer.ALIGNMENT_END
	mp_btns.add_theme_constant_override("separation", 8)
	mp_box.add_child(mp_btns)
	var probe_btn := Button.new()
	probe_btn.text = "解析文件列表"
	_style_primary(probe_btn, 108)
	probe_btn.pressed.connect(_load_magnet)
	mp_btns.add_child(probe_btn)
	var cancel_magnet := Button.new()
	cancel_magnet.text = "取消"
	_style_ghost(cancel_magnet)
	cancel_magnet.pressed.connect(_close_magnet_popup)
	mp_btns.add_child(cancel_magnet)
	root.add_child(_magnet_popup)
	_make_popup_draggable(_magnet_popup)

	# 磁力文件选择弹窗(探测结果; 双击或"播放选中"开流)
	_torrent_popup = PanelContainer.new()
	_torrent_popup.visible = false
	_torrent_popup.set_anchors_preset(Control.PRESET_CENTER)
	_torrent_popup.grow_horizontal = Control.GROW_DIRECTION_BOTH   # 内容尺寸变化时围绕中心对称生长(否则向右下长出去)
	_torrent_popup.grow_vertical = Control.GROW_DIRECTION_BOTH
	_torrent_popup.custom_minimum_size = Vector2(560, 0)
	_style_dialog(_torrent_popup)
	var tp_inner := MarginContainer.new()
	tp_inner.mouse_filter = Control.MOUSE_FILTER_PASS
	tp_inner.add_theme_constant_override("margin_left", 20)
	tp_inner.add_theme_constant_override("margin_right", 20)
	tp_inner.add_theme_constant_override("margin_top", 16)
	tp_inner.add_theme_constant_override("margin_bottom", 16)
	_torrent_popup.add_child(tp_inner)
	var tp_box := VBoxContainer.new()
	tp_box.add_theme_constant_override("separation", 10)
	tp_inner.add_child(tp_box)
	_torrent_title = Label.new()
	_torrent_title.add_theme_font_override("font", UiKit.font_weight(600))
	_torrent_title.add_theme_font_size_override("font_size", 16)
	_torrent_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	_torrent_title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	tp_box.add_child(_torrent_title)
	_torrent_list = ItemList.new()
	_torrent_list.custom_minimum_size = Vector2(0, 320)
	_torrent_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_torrent_list.select_mode = ItemList.SELECT_SINGLE
	_torrent_list.add_theme_stylebox_override("panel", _flat(Color(1, 1, 1, 0.04), 6))
	_torrent_list.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_torrent_list.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	_torrent_list.item_activated.connect(_on_torrent_item_activated)
	tp_box.add_child(_torrent_list)
	var tp_btns := HBoxContainer.new()
	tp_btns.alignment = BoxContainer.ALIGNMENT_END
	tp_btns.add_theme_constant_override("separation", 8)
	tp_box.add_child(tp_btns)
	var pick_btn := Button.new()
	pick_btn.text = "播放选中"
	_style_primary(pick_btn, 108)
	pick_btn.pressed.connect(_on_torrent_pick_pressed)
	tp_btns.add_child(pick_btn)
	var cancel_torrent := Button.new()
	cancel_torrent.text = "取消"
	_style_ghost(cancel_torrent)
	cancel_torrent.pressed.connect(_close_torrent_popup)
	tp_btns.add_child(cancel_torrent)
	root.add_child(_torrent_popup)
	_make_popup_draggable(_torrent_popup)

	# 错误 toast: 胶囊深底 (亮画面上裸文字读不清), 底部居中悬浮于控制条上方
	_toast = PanelContainer.new()
	var toast_sb := _flat(Color(0.05, 0.08, 0.12, 0.92), 16, Color(1, 1, 1, 0.08), 1)
	toast_sb.content_margin_left = 16
	toast_sb.content_margin_right = 16
	toast_sb.content_margin_top = 8
	toast_sb.content_margin_bottom = 8
	_toast.add_theme_stylebox_override("panel", toast_sb)
	_toast_label = Label.new()
	_toast_label.add_theme_font_size_override("font_size", 14)
	_toast_label.add_theme_color_override("font_color", Color(1.0, 0.62, 0.62))
	_toast_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_toast_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_toast.add_child(_toast_label)
	_toast.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	_toast.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_toast.grow_vertical = Control.GROW_DIRECTION_BEGIN
	var bar_top := -(24.0 + _scrub_h + 6 + _btn_h)   # 控制条顶缘, toast 悬在其上方
	_toast.offset_bottom = bar_top - 10
	_toast.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_toast.modulate.a = 0.0
	root.add_child(_toast)

	# 媒体/编解码信息弹窗(工具菜单)
	_info_popup = PanelContainer.new()
	_info_popup.visible = false
	_info_popup.set_anchors_preset(Control.PRESET_CENTER)
	_info_popup.grow_horizontal = Control.GROW_DIRECTION_BOTH   # 内容尺寸变化时围绕中心对称生长(否则向右下长出去)
	_info_popup.grow_vertical = Control.GROW_DIRECTION_BOTH
	_info_popup.custom_minimum_size = Vector2(460, 0)
	_style_dialog(_info_popup)
	var ip_inner := MarginContainer.new()
	ip_inner.add_theme_constant_override("margin_left", 20)
	ip_inner.add_theme_constant_override("margin_right", 20)
	ip_inner.add_theme_constant_override("margin_top", 14)
	ip_inner.add_theme_constant_override("margin_bottom", 14)
	_info_popup.add_child(ip_inner)
	var ip_box := VBoxContainer.new()
	ip_box.add_theme_constant_override("separation", 10)
	ip_inner.add_child(ip_box)
	var ip_head := HBoxContainer.new()
	ip_box.add_child(ip_head)
	_info_title = Label.new()
	_info_title.add_theme_font_override("font", UiKit.font_weight(600))
	_info_title.add_theme_font_size_override("font_size", 16)
	_info_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	_info_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ip_head.add_child(_info_title)
	var ip_close := _mk_icon_btn(IconBtn.Icon.CLOSE, "", 0.4)
	ip_close.custom_minimum_size = Vector2(30, 26)
	ip_close.pressed.connect(_close_info)
	ip_head.add_child(ip_close)
	_info_scroll = ScrollContainer.new()
	_info_scroll.custom_minimum_size = Vector2(0, 280)
	_info_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	ip_box.add_child(_info_scroll)
	_info_grid = GridContainer.new()
	_info_grid.columns = 2
	_info_grid.add_theme_constant_override("h_separation", 18)
	_info_grid.add_theme_constant_override("v_separation", 6)
	_info_scroll.add_child(_info_grid)
	root.add_child(_info_popup)

func _build_settings_popup() -> void:
	# 与 info_popup 同款面板 (标题栏 + 滚动内容区 + 底部按钮), 内容用 VBox 一行一项;
	# 内容包 ScrollContainer: 小屏逻辑视口矮, 不滚动会顶满溢出
	_settings_popup = PanelContainer.new()
	_settings_popup.visible = false
	_settings_popup.set_anchors_preset(Control.PRESET_CENTER)
	_settings_popup.grow_horizontal = Control.GROW_DIRECTION_BOTH   # 内容尺寸变化时围绕中心对称生长(否则向右下长出去)
	_settings_popup.grow_vertical = Control.GROW_DIRECTION_BOTH
	_settings_popup.custom_minimum_size = Vector2(420, 0)
	_style_dialog(_settings_popup)
	var sp_inner := MarginContainer.new()
	sp_inner.add_theme_constant_override("margin_left", 20)
	sp_inner.add_theme_constant_override("margin_right", 20)
	sp_inner.add_theme_constant_override("margin_top", 14)
	sp_inner.add_theme_constant_override("margin_bottom", 14)
	_settings_popup.add_child(sp_inner)
	var sp_box := VBoxContainer.new()
	sp_box.add_theme_constant_override("separation", 10)
	sp_inner.add_child(sp_box)
	# 顶栏: 标题 + 关闭
	var sp_head := HBoxContainer.new()
	sp_box.add_child(sp_head)
	var sp_title := Label.new()
	sp_title.text = "设置"
	sp_title.add_theme_font_override("font", UiKit.font_weight(600))
	sp_title.add_theme_font_size_override("font_size", 16)
	sp_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	sp_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sp_head.add_child(sp_title)
	var sp_close := _mk_icon_btn(IconBtn.Icon.CLOSE, "", 0.4)
	sp_close.custom_minimum_size = Vector2(30, 26)
	sp_close.pressed.connect(_close_settings)
	sp_head.add_child(sp_close)
	# 内容区: IO 方案 / 硬解 / 三个日志开关 / 外观
	_settings_scroll = ScrollContainer.new()
	_settings_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	sp_box.add_child(_settings_scroll)
	var sp_content := VBoxContainer.new()
	sp_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sp_content.add_theme_constant_override("separation", 10)
	_settings_scroll.add_child(sp_content)
	_settings_io_combo = OptionButton.new()
	_settings_io_combo.add_item("自动 (本地 ffmpeg, 网络 zlmediakit)", 0)
	_settings_io_combo.add_item("ffmpeg", 1)
	_settings_io_combo.add_item("zlmediakit", 2)
	_style_combo(_settings_io_combo, 0)
	sp_content.add_child(_add_setting_row("IO 方案", _settings_io_combo,
		"控制 ffmpeg / zlmediakit 选择; auto 按 URL scheme 自动判定"))
	_settings_hard_decode = CheckBox.new()
	_settings_hard_decode.text = "硬解 (DX11/MediaCodec/VideoToolbox)"
	_settings_hard_decode.add_theme_font_size_override("font_size", 14)
	_settings_hard_decode.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	sp_content.add_child(_add_setting_row("解码模式", _settings_hard_decode,
		"未勾选 = 软解。硬解需硬件支持, 失败会自动回退"))
	_settings_log_packet = CheckBox.new()
	_settings_log_packet.text = "打印 IO 包日志"
	_settings_log_packet.add_theme_font_size_override("font_size", 14)
	_settings_log_packet.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	sp_content.add_child(_add_setting_row("日志 · 包", _settings_log_packet,
		"对应 cmdPlay -log-packet / mp.log.source.packet"))
	_settings_log_decode = CheckBox.new()
	_settings_log_decode.text = "打印解码帧日志"
	_settings_log_decode.add_theme_font_size_override("font_size", 14)
	_settings_log_decode.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	sp_content.add_child(_add_setting_row("日志 · 解码", _settings_log_decode,
		"对应 cmdPlay -log-decode / mp.log.decoder.frame"))
	_settings_log_render = CheckBox.new()
	_settings_log_render.text = "打印渲染帧日志"
	_settings_log_render.add_theme_font_size_override("font_size", 14)
	_settings_log_render.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	sp_content.add_child(_add_setting_row("日志 · 渲染", _settings_log_render,
		"对应 cmdPlay -log-render / mp.log.render.frame"))
	_settings_acrylic = CheckBox.new()
	_settings_acrylic.text = "磨砂面板 (Acrylic 模糊)"
	_settings_acrylic.add_theme_font_size_override("font_size", 14)
	_settings_acrylic.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	sp_content.add_child(_add_setting_row("外观", _settings_acrylic,
		"控制条/标题条/弹层的磨砂模糊; 低配集显可关闭, 即时生效"))
	# 底部按钮
	var sp_btn_row := HBoxContainer.new()
	sp_btn_row.alignment = BoxContainer.ALIGNMENT_END
	sp_btn_row.add_theme_constant_override("separation", 8)
	sp_box.add_child(sp_btn_row)
	var cancel_btn := Button.new()
	cancel_btn.text = "取消"
	_style_ghost(cancel_btn)
	cancel_btn.pressed.connect(_close_settings)
	sp_btn_row.add_child(cancel_btn)
	var save_btn := Button.new()
	save_btn.text = "保存"
	_style_primary(save_btn, 88)
	save_btn.pressed.connect(_apply_settings_from_popup)
	sp_btn_row.add_child(save_btn)
	_settings_popup.theme = _theme   # 挂在 _hud (非 _hud_root) 下, 需单独给主题
	_hud.add_child(_settings_popup)

# 一行设置: 左侧标题 + 右侧控件 + 下方说明 (横向 HBox + 下方 hint Label)
func _add_setting_row(label_text: String, ctrl: Control, hint: String) -> Control:
	var row := VBoxContainer.new()
	row.add_theme_constant_override("separation", 2)
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 12)
	row.add_child(hbox)
	var lbl := Label.new()
	lbl.text = label_text
	lbl.add_theme_font_size_override("font_size", 14)
	lbl.add_theme_color_override("font_color", Color(0.7, 0.74, 0.8))
	lbl.custom_minimum_size = Vector2(96, 0)
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	hbox.add_child(lbl)
	ctrl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(ctrl)
	if not hint.is_empty():
		var h := Label.new()
		h.text = hint
		h.add_theme_font_size_override("font_size", 12)
		h.add_theme_color_override("font_color", Color(0.5, 0.54, 0.6))
		h.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		row.add_child(h)
	return row
