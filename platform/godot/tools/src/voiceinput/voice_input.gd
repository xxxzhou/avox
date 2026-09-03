extends Window
## avox 语音输入 (tools/src/voiceinput) —— 无边框状态栏式微型悬浮条
## 顶栏一行: [toggle|hold 选择框] 热键 ● ⚙; 识别文字每次一行追加在下方。
## 单模式开关: 切到哪个模式就只听那个热键; toggle/hold 各记各的热键。
## hold 模式按住热键时热键钮呈按下高亮。
## 关键: 主窗口的 size/borderless 节点属性**不作用于 OS 窗口** —— 必须经 DisplayServer
## 设置 (否则还是 project.godot 默认尺寸的带标题栏窗口)。
## 行为对齐 avox_cli voice (src/avox_cmd/commands/CmdVoice.cpp, Windows only)。

const CONFIG_PATH := "user://voice_settings.cfg"
const DEFAULT_TOGGLE_HK := "f9"
const DEFAULT_HOLD_HK := "f10"
const DEFAULT_PREFIX := ""

const ACCENT := Color("#3B82F6")
const ST_REC := Color("#EF4444")
const ST_IDLE := Color("#22C55E")
const ST_OFF := Color("#6B7280")
const BG_SEG := Color("#1E293B")
const FG_INACT := Color("#94A3B8")

const _BASE_W_DESIGN := 236
const _BASE_H_DESIGN := 34
const _LINE_H_DESIGN := 20
const _MAX_H_DESIGN := 320

# DPI: 子 Window 不吃 project 的 stretch 设置, 用自身 content_scale (canvas_items) 按
# DPI 缩放 —— 布局全部写设计像素 (236x34), 不再手动乘物理因子 (旧做法物理/逻辑坐标
# 混用, 250% 屏上内容缩在左上角一小块)。_dpi_scale 仅用于 DisplayServer 物理坐标
# (窗口定位)。共享主题字体 (Noto Sans SC, src/common/ui_kit.gd)。
const UiKit := preload("res://src/common/ui_kit.gd")

var _dpi_scale: float = 1.0
var _embed := true               # 嵌入宿主窗口 (生产路径) 或独立 OS 窗口
var _want_w := 0                 # 钉宽目标 (嵌入=设计, 独立=设计×factor)
# 嵌入像素尺寸 = 设计 × dpi 因子; 内容布局用设计像素 (visible_rect = size/factor = 设计)
var _BASE_W: int: get = _get_base_w
var _BASE_H: int: get = _get_base_h
var _LINE_H: int: get = _get_line_h
var _MAX_H: int: get = _get_max_h

func _get_base_w() -> int: return int(_BASE_W_DESIGN * _dpi_scale)
func _get_base_h() -> int: return int(_BASE_H_DESIGN * _dpi_scale)
func _get_line_h() -> int: return int(_LINE_H_DESIGN * _dpi_scale)
func _get_max_h() -> int: return int(_MAX_H_DESIGN * _dpi_scale)

func _s(v: float) -> float: return v

var mic: MicCapture
var stt: SttNode
var hotkey: GlobalHotkey
var injector: TextInjector

# 设置 (对齐 cli.json voice 节点; 双热键各自存, 单模式开关)
var _mode := "toggle"            # 当前生效模式: "toggle" | "hold"
var _hotkey_toggle := DEFAULT_TOGGLE_HK
var _hotkey_hold := DEFAULT_HOLD_HK
var _prefix := DEFAULT_PREFIX
var _device_index := 0

# 运行状态
var _recording := false
var _src := ""              # 当前录音由谁发起: "toggle" / "hold" / ""
var _stt_ready := false
var _last_text := ""        # 已注入目标窗口的文字 (增量 diff 基准)
var _last_partial := ""     # 最近一次 partial (去重用, 同 CmdVoice)
var _accumulated := ""      # hold 模式累积文本 (端点分段间加空格)
var _frame_count := 0       # 调试: 音频帧计数
var _last_inject_ms := 0    # partial 注入限流时间戳 (防止高频 backspace+注入卡目标输入)

# 无边框窗口拖动
var _dragging := false
var _drag_offset := Vector2i.ZERO

# UI
var _btn_toggle: Button
var _btn_hold: Button
var _mode_group: ButtonGroup
var _hk_btn: KeyCapture
var _rec_btn: Button
var _status_dot: Label
var _level_bar: ColorRect
var _lines: VBoxContainer
var _partial: Label
var _popup: PopupPanel
var _prefix_edit: LineEdit
var _devices: OptionButton
var _model_state: Label
var _toast_label: Label
var _toast_timer: Timer
var _pulse_tween: Tween
var _dl_btn: Button
var _dl_bar: ProgressBar
var _model_checked := false
var _model_dl_needed := false
var _downloading_model := false


func _ready() -> void:
	# DPI 因子仅用于独立 OS 窗口路径与物理定位; 嵌入路径与宿主共享渲染缩放
	var dpi := DisplayServer.screen_get_dpi(DisplayServer.window_get_current_screen())
	_dpi_scale = maxf(float(dpi) / 96.0, 1.0)
	print("[voice_input] dpi=", dpi, " scale=", _dpi_scale)
	_dbg("init dpi=%d scale=%.1f" % [dpi, _dpi_scale])

	# 4 个插件节点必须在场景树里, call_deferred 的信号才安全投递
	mic = MicCapture.new()
	stt = SttNode.new()
	hotkey = GlobalHotkey.new()
	injector = TextInjector.new()
	add_child(mic)
	add_child(stt)
	add_child(hotkey)
	add_child(injector)

	# forward_to: tap 线程直喂 STT (零拷贝, audio_desc 同步设置不丢首帧)
	mic.forward_to(stt)

	_load_config()
	_build_ui()
	_wire_signals()
	_fill_devices()
	_apply_window_shape()

	# 预加载模型 (后台, 不阻塞主线程); 就绪后才注册热键 (对齐 cmdVoice: 先模型后钩子)
	mic.device_index = _device_index
	stt.model_level = 2        # base
	stt.recognizer_type = 1    # streaming
	stt.start()
	_set_model_state("模型加载中...")

	_update_status()
	call_deferred("_check_model_for_download")   # 异步查模型就绪, 不阻塞启动


func _process(_delta: float) -> void:
	# Godot/Windows 在窗口显示时会把窗口异步顶高 (如 34→64, 内容实际只有 34)——
	# 每帧核对宽度, 跑偏就钉回 (无边框窗不可手动缩放, 钉住无副作用)。
	if _want_w > 0 and size.x != _want_w:
		size.x = _want_w

	# 无标题栏 → 按住窗口任意空白拖动; DisplayServer 读 OS 光标,
	# 光标移出窗口也跟得上; 松开 (含窗外) 即停。
	if _dragging:
		if not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
			_dragging = false
			return
		DisplayServer.window_set_position(
			DisplayServer.mouse_get_position() - _drag_offset)

	# 音频电平条 (录音中更新宽度)
	if _level_bar:
		if _recording:
			_level_bar.visible = true
			var lvl := mic.get_audio_level()
			var bar_w := _level_bar.get_parent_control().size.x
			_level_bar.custom_minimum_size.x = bar_w * clampf(lvl * 5.0, 0.02, 1.0)
		else:
			_level_bar.visible = false


# ============================================================
# 窗口形状 (主窗口必须经 DisplayServer, 节点属性不生效)
# ============================================================

func _apply_window_shape() -> void:
	# 生产路径 = 嵌入主窗口 (project embed_subwindows, get_window_id()==0):
	# 与宿主共享画布及其 DPI 渲染缩放 —— 设计像素直排 (factor=1), hub 250% 时 34 逻辑高
	# 实际渲染 85 物理像素, 天然清晰。
	_embed = get_window_id() == 0
	if _embed:
		content_scale_factor = 1.0
		size = Vector2i(_BASE_W_DESIGN, _BASE_H_DESIGN)
		min_size = size
		_want_w = _BASE_W_DESIGN
		# 右下角, 任务栏上方留边 (父 = 宿主根 Control, 逻辑尺寸)
		var pc := get_parent() as Control
		if pc:
			position = Vector2i(int(pc.size.x) - _BASE_W_DESIGN - 24,
				int(pc.size.y) - _BASE_H_DESIGN - 48)
		return
	# 独立 OS 窗口 (仅调试脱离主树时): content_scale 按 DPI, 布局仍是设计像素
	content_scale_mode = Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
	content_scale_aspect = Window.CONTENT_SCALE_ASPECT_EXPAND
	content_scale_factor = _dpi_scale
	var wid := get_window_id()
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, true, wid)
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, true, wid)
	size = Vector2i(_BASE_W, _BASE_H)   # 设计 × factor
	min_size = size
	_want_w = _BASE_W
	# 放到当前屏幕右下角偏上 (任务栏上方, 留边距); DisplayServer 用物理像素
	var scr := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
	DisplayServer.window_set_position(Vector2i(scr.x - _BASE_W - int(60 * _dpi_scale),
		scr.y - _BASE_H - int(120 * _dpi_scale)), wid)


func _resize() -> void:
	# 用实际内容高度而非 child_count * LINE_H (长行换行会更高); 内容是设计像素
	var content_h := _BASE_H_DESIGN
	for c in _lines.get_children():
		if c is Label:
			content_h += int(c.get_combined_minimum_size().y)
		else:
			content_h += _LINE_H_DESIGN
	if _partial.visible:
		content_h += int(_partial.get_combined_minimum_size().y)
	content_h = mini(content_h, _MAX_H_DESIGN)
	size.y = content_h if _embed else int(content_h * _dpi_scale)


# ============================================================
# 信号接线
# ============================================================

func _wire_signals() -> void:
	# 单模式: 钩子已按 active_mode 只发当前模式信号
	hotkey.toggled.connect(func(): _dbg("signal: toggled"); _on_toggle())
	hotkey.pressed.connect(func(): _dbg("signal: pressed"); _on_hold_down())
	hotkey.released.connect(func(): _dbg("signal: released"); _on_hold_up())
	hotkey.error.connect(func(msg): _dbg("signal: error " + msg); _toast(msg))

	# forward_to 模式: 帧直喂 STT, 不经主线程; audio_desc 仍发信号 (UI 可用)
	mic.audio_desc.connect(func(sr, ch):
		_dbg("audio_desc sr=%d ch=%d" % [sr, ch]))
	# 非转发模式回退 (forward_to 未设时才需要):
	# mic.audio_frame.connect(func(data): stt.recognize(data))

	stt.partial_result.connect(_on_partial)
	stt.final_result.connect(_on_final)
	stt.stt_ready.connect(_on_stt_ready)
	stt.error.connect(_on_stt_error)
	# AssetManager (autoload) 模型下载进度
	AssetManager.download_started.connect(_on_dl_started)
	AssetManager.download_progress.connect(_on_dl_progress)
	AssetManager.download_finished.connect(_on_dl_finished)


func _on_stt_ready() -> void:
	_stt_ready = true
	_set_model_state("模型就绪")
	_register_hotkeys()
	_update_status()
	_dbg("stt_ready, hotkeys: toggle=%s hold=%s mode=%s" % [_hotkey_toggle, _hotkey_hold, _mode])


func _register_hotkeys() -> void:
	hotkey.register("toggle", _hotkey_toggle)
	hotkey.register("hold", _hotkey_hold)
	hotkey.set_active_mode(_mode)   # 两套都注册, 只有当前模式热键响应


# ============================================================
# 模式开关 (单模式: 切到哪个模式就只听那个热键)
# ============================================================

func _set_mode(m: String) -> void:
	if _mode == m:
		return
	# 切模式先停当前录音: 否则 toggle 起的录音在 hold 模式下开关/F10 都停不掉 (卡死状态)。
	# 须在改 _mode 前停, 让 _stop_recording 按旧模式正确熄灭 toggle 开关。
	if _recording:
		_stop_recording()
	_mode = m
	# 「录」开关跟随模式: toggle=点按切换, hold=按住才录
	if _rec_btn:
		_rec_btn.toggle_mode = (m == "toggle")
		_rec_btn.set_pressed_no_signal(false)   # 已停录音, 熄灭开关
	_sync_mode_ui()
	_save_config()


func _sync_mode_ui() -> void:
	# ButtonGroup 设置 button_pressed 会触发 pressed 信号 → _set_mode,
	# 必须屏蔽, 否则先设 toggle 再设 hold, hold 覆盖 toggle
	_btn_toggle.set_block_signals(true)
	_btn_hold.set_block_signals(true)
	_btn_toggle.button_pressed = _mode == "toggle"
	_btn_hold.button_pressed = _mode == "hold"
	_btn_toggle.set_block_signals(false)
	_btn_hold.set_block_signals(false)
	_hk_btn.current_name = _hotkey_toggle if _mode == "toggle" else _hotkey_hold
	_hk_btn.refresh()
	if _stt_ready:
		hotkey.set_active_mode(_mode)


# ============================================================
# 录音状态机 (toggle 由 _src 记发起方, hold 释放不打断 toggle 轮)
# ============================================================

func _on_toggle() -> void:
	_dbg("on_toggle recording=%s src=%s" % [_recording, _src])
	if _mode != "toggle":
		return   # hold 模式下「录」开关由 button_down/up 驱动, 不响应点按切换
	if not _stt_ready:
		_toast("模型未就绪")
		if _rec_btn:
			_rec_btn.set_pressed_no_signal(false)   # 手动开关回弹
		return
	if _recording:
		if _src == "toggle":
			_stop_recording()
	else:
		_start_recording("toggle")


func _on_hold_down() -> void:
	if not _stt_ready:
		_toast("模型未就绪")
		return
	_hk_btn.button_pressed = true   # hold 按住状态 (仅视觉, 不触发捕获)
	if not _recording:
		_start_recording("hold")


func _on_hold_up() -> void:
	_hk_btn.button_pressed = false
	if _recording and _src == "hold":
		_stop_recording()


# 「录」开关 hold 模式: 按住开始录音, 松开停止 (同 F10 热键行为)
func _on_rec_btn_down() -> void:
	_dbg("rec_btn down mode=%s" % _mode)
	if _mode != "hold":
		return
	if not _stt_ready:
		_toast("模型未就绪")
		return
	if not _recording:
		_start_recording("hold")


func _on_rec_btn_up() -> void:
	_dbg("rec_btn up mode=%s src=%s" % [_mode, _src])
	if _mode != "hold":
		return
	if _recording and _src == "hold":
		_stop_recording()


func _start_recording(src: String) -> void:
	_dbg("start_recording src=%s" % src)
	if _recording:
		return
	stt.start()                       # 确保 STT 任务在跑 (幂等; 上一轮 tail 滞留任务已跑则无操作)
	var ok := mic.start()
	_dbg("mic.start() = %s" % ok)
	if not ok:
		_toast("麦克风启动失败 (检查设备)")
		if _rec_btn and _mode == "toggle":
			_rec_btn.set_pressed_no_signal(false)   # toggle 手动开关回弹
		return
	_src = src
	_last_text = ""                   # injector reset: 新一轮从零注入
	_last_partial = ""                # partial 去重
	_accumulated = ""                 # hold 模式累积文本
	_frame_count = 0
	_last_inject_ms = 0               # 重置限流, 首句立即注入
	_recording = true
	# toggle 模式点亮开关 (hold 模式靠按钮自身按住高亮, 不强制)
	if _rec_btn and _mode == "toggle":
		_rec_btn.set_pressed_no_signal(src == "toggle")
	_partial.text = ""
	_partial.visible = true
	_resize()
	_update_status()


func _stop_recording() -> void:
	_dbg("stop_recording (was recording=%s src=%s)" % [_recording, _src])
	if not _recording:
		return
	mic.stop()        # closeTap 排干尾部帧 (保句尾)
	_recording = false
	# toggle 模式熄灭开关 (hold 模式由按钮自身松开时熄灭)
	if _rec_btn and _mode == "toggle":
		_rec_btn.set_pressed_no_signal(false)
	var had_result := not _last_text.is_empty() or not _accumulated.is_empty()
	_src = ""
	stt.stop()        # 排空 final + 重置 stream (防跨段串话)
	# hold 累积模式: stop 后注入最终累积文本 (同 CmdVoice stopRecording)
	if _mode == "hold" and not _accumulated.is_empty():
		var full := _prefix + _accumulated
		_inject_partial(full)
	if not had_result and _last_partial.is_empty():
		_toast("无识别结果")
	_partial.visible = false
	_resize()
	_update_status()


# ============================================================
# 识别结果 → 增量注入 + 逐行显示 (每次 final 一行)
# ============================================================

func _on_partial(text: String) -> void:
	_dbg("partial: " + text)
	# toggle 模式: 逐句 (prefix + partial); hold 模式: 累积 (prefix + accumulated + partial)
	var full := ""
	if _src == "toggle":
		full = _prefix + text
	else:
		full = _prefix + _accumulated + text
	# 去重 (同 CmdVoice: 无变化不注入, 避免多余 Backspace+inject)
	if full == _last_partial:
		return
	_last_partial = full
	_partial.text = "🎤 " + full
	# 限流: sherpa 流式 partial 高频触发, 每次 backspace+inject 都打断目标输入 → 卡顿。
	# 显示保持实时, 注入限 ~500ms 一次; 差异由 _inject_partial 的 diff 自动修正。
	var now := Time.get_ticks_msec()
	if now - _last_inject_ms >= 500:
		_last_inject_ms = now
		_inject_partial(full)


func _on_final(text: String) -> void:
	_dbg("final: " + text)
	if _src == "toggle":
		# 逐句模式: 注入最终结果后清空, 下一句从零开始 (同 CmdVoice perSentence)
		var full := _prefix + text
		_inject_partial(full)          # 与最后一次 partial 的差异自动修正
		_last_text = ""                # 逐句: 下一句从零开始
		_last_partial = ""
		_partial.text = ""
		_append_line(full)
	else:
		# 累积模式: 端点分段间加空格, 整段注入 (同 CmdVoice accumulated)
		if not _accumulated.is_empty():
			_accumulated += " "
		_accumulated += text
		_last_partial = ""
		var full := _prefix + _accumulated
		_inject_partial(full)
		_append_line(full)


func _inject_partial(full: String) -> void:
	if not injector.can_inject():
		_last_text = full          # 不可注入时同步基准, 恢复后不补发 (同 cmdVoice)
		return
	var common := _common_prefix_len(_last_text, full)
	if _last_text.length() > common:
		injector.send_backspace(_last_text.length() - common)
	if full.length() > common:
		injector.inject_unicode(full.substr(common))
	_last_text = full


func _common_prefix_len(a: String, b: String) -> int:
	var n := mini(a.length(), b.length())
	var i := 0
	while i < n and a[i] == b[i]:
		i += 1
	return i


func _append_line(text: String) -> void:
	# 只显示最新一行: 替换旧行, 窗口保持小巧 (用户要求)
	for c in _lines.get_children():
		_lines.remove_child(c)
		c.queue_free()
	var l := Label.new()
	l.text = "✓ " + text
	l.add_theme_font_size_override("font_size", int(_s(13)))
	l.add_theme_color_override("font_color", Color("#CBD5E1"))
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_lines.add_child(l)
	# 等一帧让 Label 算出换行后的实际高度, 再 resize
	await get_tree().process_frame
	_resize()


# ============================================================
# 配置 (mode + 双热键 + prefix + device)
# ============================================================

func _load_config() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(CONFIG_PATH) == OK:
		_mode = str(cfg.get_value("voice", "mode", _mode))
		if _mode != "toggle" and _mode != "hold":
			_mode = "toggle"
		_hotkey_toggle = str(cfg.get_value("voice", "hotkey_toggle", _hotkey_toggle))
		_hotkey_hold = str(cfg.get_value("voice", "hotkey_hold", _hotkey_hold))
		_prefix = str(cfg.get_value("voice", "prefix", _prefix))
		_device_index = int(cfg.get_value("voice", "device_index", _device_index))


func _save_config() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("voice", "mode", _mode)
	cfg.set_value("voice", "hotkey_toggle", _hotkey_toggle)
	cfg.set_value("voice", "hotkey_hold", _hotkey_hold)
	cfg.set_value("voice", "prefix", _prefix)
	cfg.set_value("voice", "device_index", _device_index)
	cfg.save(CONFIG_PATH)


# ============================================================
# UI 构建 (代码构建, 同 mediaplayer 惯例)
# ============================================================

func _build_ui() -> void:
	title = "语音输入"
	always_on_top = true
	borderless = true
	size = Vector2i(_BASE_W, _BASE_H)
	min_size = Vector2i(_BASE_W, _BASE_H)
	close_requested.connect(func(): hide())   # 关窗隐藏, 热键仍生效

	# 全窗拖动背景 (无标题栏): 按住窗口任意空白拖动; 深色胶囊 + 细描边 (与全家一致)
	var bg := Panel.new()
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.theme = UiKit.build_theme()
	var bg_sb := StyleBoxFlat.new()
	bg_sb.bg_color = Color(0.043, 0.055, 0.08, 0.97)
	bg_sb.set_corner_radius_all(int(_s(9)))
	bg_sb.border_color = Color(1, 1, 1, 0.10)
	bg_sb.set_border_width_all(1)
	bg.add_theme_stylebox_override("panel", bg_sb)
	bg.mouse_filter = Control.MOUSE_FILTER_STOP
	bg.gui_input.connect(_on_bg_gui_input)
	add_child(bg)

	var root := MarginContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("margin_left", int(_s(6)))
	root.add_theme_constant_override("margin_top", int(_s(4)))
	root.add_theme_constant_override("margin_right", int(_s(6)))
	root.add_theme_constant_override("margin_bottom", int(_s(4)))
	bg.add_child(root)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", int(_s(2)))
	root.add_child(vbox)

	# ── 一行顶栏: [toggle|hold 选择框] 热键 ● ⚙ ──
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", int(_s(5)))
	vbox.add_child(bar)

	# toggle/hold 选择框 (分段控件, 激活段填色)
	var seg := PanelContainer.new()
	seg.add_theme_stylebox_override("panel", _pill_style(BG_SEG))
	seg.add_theme_constant_override("margin_left", int(_s(2)))
	seg.add_theme_constant_override("margin_right", int(_s(2)))
	seg.add_theme_constant_override("margin_top", int(_s(1)))
	seg.add_theme_constant_override("margin_bottom", int(_s(1)))
	bar.add_child(seg)
	var seg_box := HBoxContainer.new()
	seg_box.add_theme_constant_override("separation", 0)
	seg.add_child(seg_box)

	_mode_group = ButtonGroup.new()
	_btn_toggle = _make_seg("toggle")
	_btn_toggle.pressed.connect(func(): _set_mode("toggle"))
	_btn_toggle.tooltip_text = "toggle: 按一次开始录音, 再按一次停止"
	seg_box.add_child(_btn_toggle)
	_btn_hold = _make_seg("hold")
	_btn_hold.pressed.connect(func(): _set_mode("hold"))
	_btn_hold.tooltip_text = "hold: 按住说话, 松开结束"
	seg_box.add_child(_btn_hold)

	# 当前模式热键钮 (单击改绑; hold 按住时呈按下高亮)
	_hk_btn = KeyCapture.new()
	_hk_btn.hotkey = hotkey
	_hk_btn.toggle_mode = true                 # 允许 button_pressed 长驻显示按住状态
	_hk_btn.add_theme_font_size_override("font_size", int(_s(12)))
	_hk_btn.add_theme_color_override("font_color", Color.WHITE)
	_hk_btn.add_theme_color_override("font_pressed_color", Color.WHITE)
	_hk_btn.custom_minimum_size = Vector2(_s(48), _s(22))
	_hk_btn.add_theme_stylebox_override("normal", _pill_style(BG_SEG))
	_hk_btn.add_theme_stylebox_override("hover", _pill_style(BG_SEG.lightened(0.08)))
	_hk_btn.add_theme_stylebox_override("pressed", _pill_style(ACCENT))
	_hk_btn.add_theme_stylebox_override("hover_pressed", _pill_style(ACCENT))
	_hk_btn.add_theme_stylebox_override("focus", _pill_style(BG_SEG))
	_hk_btn.tooltip_text = "当前模式热键, 单击改绑"
	_hk_btn.captured.connect(_on_hk_captured)
	bar.add_child(_hk_btn)

	_status_dot = Label.new()
	_status_dot.text = "●"
	_status_dot.add_theme_font_size_override("font_size", int(_s(12)))
	_status_dot.custom_minimum_size = Vector2(_s(14), 0)
	_status_dot.tooltip_text = "绿=待命 红=录音中 灰=模型未就绪"
	bar.add_child(_status_dot)

	# 手动录音开关 (toggle_mode: 按一次开始, 再按一次停止; 复用 toggle 状态机)
	_rec_btn = Button.new()
	_rec_btn.text = "录"
	_rec_btn.toggle_mode = (_mode == "toggle")   # toggle=点按切换; hold=按住才录
	_rec_btn.add_theme_font_size_override("font_size", int(_s(12)))
	_rec_btn.add_theme_color_override("font_color", Color.WHITE)
	_rec_btn.add_theme_color_override("font_pressed_color", Color.WHITE)
	_rec_btn.custom_minimum_size = Vector2(_s(32), _s(22))
	_rec_btn.add_theme_stylebox_override("normal", _pill_style(BG_SEG))
	_rec_btn.add_theme_stylebox_override("hover", _pill_style(BG_SEG.lightened(0.15)))
	_rec_btn.add_theme_stylebox_override("pressed", _pill_style(ST_REC))
	_rec_btn.add_theme_stylebox_override("hover_pressed", _pill_style(ST_REC.lightened(0.1)))
	_rec_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_rec_btn.tooltip_text = "手动录音: toggle=点按开始/停止, hold=按住开始/松开停止"
	_rec_btn.pressed.connect(_on_toggle)
	_rec_btn.button_down.connect(_on_rec_btn_down)
	_rec_btn.button_up.connect(_on_rec_btn_up)
	bar.add_child(_rec_btn)

	# 音频电平条 (录音中显示麦克风输入强度)
	var level_bg := PanelContainer.new()
	level_bg.custom_minimum_size = Vector2(_s(32), _s(8))
	level_bg.add_theme_stylebox_override("panel", _pill_style(Color(0.15, 0.18, 0.24)))
	level_bg.tooltip_text = "麦克风输入电平"
	level_bg.clip_contents = true
	bar.add_child(level_bg)
	_level_bar = ColorRect.new()
	_level_bar.color = ST_REC
	_level_bar.custom_minimum_size = Vector2(0, _s(6))
	_level_bar.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_level_bar.visible = false
	level_bg.add_child(_level_bar)

	var set_btn := Button.new()
	set_btn.text = "⚙"
	set_btn.add_theme_font_size_override("font_size", int(_s(12)))
	set_btn.add_theme_color_override("font_color", FG_INACT)
	set_btn.add_theme_color_override("font_hover_color", Color.WHITE)
	set_btn.custom_minimum_size = Vector2(_s(24), _s(22))
	set_btn.add_theme_stylebox_override("normal", _pill_style(Color(0, 0, 0, 0)))
	set_btn.add_theme_stylebox_override("hover", _pill_style(BG_SEG))
	set_btn.add_theme_stylebox_override("pressed", _pill_style(BG_SEG.lightened(0.1)))
	set_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	set_btn.tooltip_text = "设置 (前缀/麦克风/模型)"
	set_btn.pressed.connect(func(): _popup.popup_centered())
	bar.add_child(set_btn)

	# ── 识别文字 (每次一行) ──
	_lines = VBoxContainer.new()
	_lines.add_theme_constant_override("separation", 2)
	vbox.add_child(_lines)

	# partial 实时行 (录音中显示)
	_partial = Label.new()
	_partial.add_theme_font_size_override("font_size", int(_s(12)))
	_partial.add_theme_color_override("font_color", ACCENT)
	_partial.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_partial.visible = false
	vbox.add_child(_partial)

	# ── toast ──
	_toast_label = Label.new()
	_toast_label.add_theme_font_size_override("font_size", int(_s(12)))
	_toast_label.add_theme_color_override("font_color", ACCENT)
	_toast_label.visible = false
	vbox.add_child(_toast_label)
	_toast_timer = Timer.new()
	_toast_timer.one_shot = true
	_toast_timer.timeout.connect(func(): _toast_label.visible = false)
	add_child(_toast_timer)

	# ── 设置弹窗 ──
	_popup = PopupPanel.new()
	add_child(_popup)
	var pbox := VBoxContainer.new()
	pbox.add_theme_constant_override("separation", int(_s(6)))
	var pm := MarginContainer.new()
	pm.add_theme_constant_override("margin_left", int(_s(16)))
	pm.add_theme_constant_override("margin_top", int(_s(12)))
	pm.add_theme_constant_override("margin_right", int(_s(16)))
	pm.add_theme_constant_override("margin_bottom", int(_s(12)))
	pm.add_child(pbox)
	_popup.add_child(pm)

	var t := Label.new()
	t.text = "设置"
	t.add_theme_font_size_override("font_size", int(_s(14)))
	pbox.add_child(t)

	# 前缀
	var prefix_row := HBoxContainer.new()
	prefix_row.add_theme_constant_override("separation", int(_s(8)))
	pbox.add_child(prefix_row)
	prefix_row.add_child(_lbl("前缀"))
	_prefix_edit = LineEdit.new()
	_prefix_edit.text = _prefix
	_prefix_edit.custom_minimum_size = Vector2(_s(150), 0)
	_prefix_edit.text_changed.connect(func(s):
		_prefix = s
		_save_config())
	prefix_row.add_child(_prefix_edit)

	# 麦克风
	var dev_row := HBoxContainer.new()
	dev_row.add_theme_constant_override("separation", int(_s(8)))
	pbox.add_child(dev_row)
	dev_row.add_child(_lbl("麦克风"))
	_devices = OptionButton.new()
	_devices.custom_minimum_size = Vector2(_s(150), 0)
	_devices.item_selected.connect(func(idx):
		_device_index = idx
		mic.device_index = idx
		_save_config())
	dev_row.add_child(_devices)

	# 模型状态 + 下载按钮 + 进度条
	var model_row := HBoxContainer.new()
	model_row.add_theme_constant_override("separation", int(_s(8)))
	pbox.add_child(model_row)
	model_row.add_child(_lbl("模型"))
	_model_state = Label.new()
	_model_state.add_theme_color_override("font_color", ACCENT)
	_model_state.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	model_row.add_child(_model_state)
	_dl_btn = Button.new()
	_dl_btn.text = "下载"
	_dl_btn.add_theme_font_size_override("font_size", int(_s(12)))
	_dl_btn.custom_minimum_size = Vector2(_s(56), _s(24))
	_dl_btn.tooltip_text = "下载语音识别模型 (约 168MB, 首次使用)"
	_dl_btn.pressed.connect(_on_download_pressed)
	model_row.add_child(_dl_btn)
	_dl_bar = ProgressBar.new()
	_dl_bar.custom_minimum_size = Vector2(0, int(_s(16)))
	_dl_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_dl_bar.visible = false
	pbox.add_child(_dl_bar)

	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", int(_s(8)))
	pbox.add_child(btn_row)
	var clear_btn := Button.new()
	clear_btn.text = "清空"
	clear_btn.pressed.connect(_clear_history)
	btn_row.add_child(clear_btn)
	var quit_btn := Button.new()
	quit_btn.text = "退出"
	quit_btn.tooltip_text = "完全退出 (无边框窗口没有系统关闭钮)"
	quit_btn.pressed.connect(_quit)
	btn_row.add_child(quit_btn)

	_sync_mode_ui()   # 应用持久化模式到开关/热键钮


func _on_bg_gui_input(ev: InputEvent) -> void:
	if ev is InputEventMouseButton and ev.button_index == MOUSE_BUTTON_LEFT:
		if ev.pressed:
			_dragging = true
			_drag_offset = DisplayServer.mouse_get_position() - DisplayServer.window_get_position()
		else:
			_dragging = false


# 分段控件段样式 (激活段填 ACCENT, 非激活透明)
func _make_seg(text: String) -> Button:
	var b := Button.new()
	b.text = text
	b.toggle_mode = true
	b.button_group = _mode_group
	b.add_theme_font_size_override("font_size", int(_s(12)))
	b.add_theme_color_override("font_color", FG_INACT)
	b.add_theme_color_override("font_hover_color", Color.WHITE)
	b.add_theme_color_override("font_pressed_color", Color.WHITE)
	b.custom_minimum_size = Vector2(_s(56), _s(22))
	b.add_theme_stylebox_override("normal", _pill_style(Color(0, 0, 0, 0)))
	b.add_theme_stylebox_override("hover", _pill_style(BG_SEG.lightened(0.15)))
	b.add_theme_stylebox_override("pressed", _pill_style(ACCENT))
	b.add_theme_stylebox_override("hover_pressed", _pill_style(ACCENT))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	return b


func _pill_style(color: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = color
	sb.corner_radius_top_left = int(_s(5))
	sb.corner_radius_top_right = int(_s(5))
	sb.corner_radius_bottom_left = int(_s(5))
	sb.corner_radius_bottom_right = int(_s(5))
	sb.set_content_margin_all(0)
	return sb


func _clear_history() -> void:
	for c in _lines.get_children():
		c.queue_free()
	_partial.text = ""
	_resize()


func _lbl(s: String) -> Label:
	var l := Label.new()
	l.text = s
	l.custom_minimum_size = Vector2(_s(48), 0)
	return l


func _fill_devices() -> void:
	_devices.clear()
	var names := mic.list_devices()
	for i in names.size():
		_devices.add_item(names[i])
	_devices.select(clampi(_device_index, 0, maxi(names.size() - 1, 0)))


# ============================================================
# 热键改绑 (单击热键钮 = KeyCapture, 捕获后回填当前模式)
# ============================================================

func _on_hk_captured(name: String) -> void:
	if _mode == "toggle":
		_hotkey_toggle = name
	else:
		_hotkey_hold = name
	if _stt_ready:
		hotkey.register(_mode, name)
	_save_config()
	_toast("%s 热键: %s" % [_mode, _hk_btn.display_name(name)])


# ============================================================
# 状态
# ============================================================

func _update_status() -> void:
	if _recording:
		_status_dot.add_theme_color_override("font_color", ST_REC)
		_start_pulse()
	elif _stt_ready:
		_status_dot.add_theme_color_override("font_color", ST_IDLE)
		_stop_pulse()
	else:
		_status_dot.add_theme_color_override("font_color", ST_OFF)
		_stop_pulse()


func _start_pulse() -> void:
	if _pulse_tween and _pulse_tween.is_running():
		return
	_pulse_tween = create_tween().set_loops()
	_pulse_tween.tween_property(_status_dot, "modulate:a", 0.4, 0.5).set_trans(Tween.TRANS_SINE)
	_pulse_tween.tween_property(_status_dot, "modulate:a", 1.0, 0.5).set_trans(Tween.TRANS_SINE)


func _stop_pulse() -> void:
	if _pulse_tween:
		_pulse_tween.kill()
		_pulse_tween = null
	_status_dot.modulate.a = 1.0


func _set_model_state(s: String) -> void:
	if _model_state:
		_model_state.text = s


# ============================================================
# 模型下载 (AssetManager autoload: 检查 / 下载 / 进度)
# ============================================================

func _check_model_for_download() -> void:
	# 查模型就绪; 未就绪且 STT 没起来 → 提示下载 (不阻塞 stt.start: 模型可能在别处/avox 自带)
	var info := AssetManager.check_item(AssetManager.VOICE_MODEL_ID, true)
	_model_checked = true
	_model_dl_needed = not bool(info.get("ready", true))   # check 失败(info 空)默认就绪, 不误报
	if _model_dl_needed and not _stt_ready and not _downloading_model:
		_set_model_state("模型未下载 — 点 ⚙ 下载")


func _on_stt_error(msg: String) -> void:
	_dbg("stt error: " + msg)
	if _model_dl_needed and not _downloading_model:
		_set_model_state("模型未下载 — 点 ⚙ 下载")
	else:
		_set_model_state(msg)


func _on_download_pressed() -> void:
	if _downloading_model:
		return
	if not AssetManager.download_item(AssetManager.VOICE_MODEL_ID):
		_toast("无法下载: 找不到 python 或 fetch_assets.py")
		return
	# download_started 信号会接管 UI; 这里先兜底
	_downloading_model = true
	_dl_btn.disabled = true
	_dl_bar.visible = true
	_dl_bar.value = 0
	_set_model_state("准备下载...")


func _on_dl_started(id: String) -> void:
	if id != AssetManager.VOICE_MODEL_ID:
		return
	_downloading_model = true
	_dl_btn.disabled = true
	_dl_bar.visible = true
	_dl_bar.value = 0
	_set_model_state("下载中...")


func _on_dl_progress(id: String, pct: float, name: String) -> void:
	if id != AssetManager.VOICE_MODEL_ID:
		return
	_dl_bar.value = pct
	_set_model_state("下载中 %.0f%% — %s" % [pct, name])


func _on_dl_finished(id: String, ok: bool, msg: String) -> void:
	if id != AssetManager.VOICE_MODEL_ID:
		return
	_downloading_model = false
	_dl_btn.disabled = false
	_dl_bar.visible = false
	if ok:
		_set_model_state("下载完成, 加载模型...")
		stt.start()   # 模型已就位, 重新加载 (幂等)
	else:
		_set_model_state("下载失败: " + msg)


func _toast(msg: String) -> void:
	_toast_label.text = msg
	_toast_label.visible = true
	_toast_timer.start(2.0)


func _dbg(msg: String) -> void:
	var f := FileAccess.open("user://voice_debug.log", FileAccess.READ_WRITE)
	if not f:
		f = FileAccess.open("user://voice_debug.log", FileAccess.WRITE_READ)
	if f:
		f.seek_end()
		var ts := Time.get_ticks_msec()
		f.store_line("%d %s" % [ts, msg])


# ============================================================
# 有序退出 (对齐 CmdVoice 清理顺序: 录音 → 热键 → STT)
# ============================================================

func _quit() -> void:
	_dbg("quit: stopping recording=%s" % _recording)
	if _recording:
		_stop_recording()
	_dbg("quit: unregistering hotkey")
	hotkey.unregisterHotkey()   # join 钩子线程
	_dbg("quit: stopping stt")
	stt.stop()                  # 排空 + 重置
	_dbg("quit: quit")
	get_tree().quit()
