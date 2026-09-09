extends Control
## WebRTC 拉流连接测试 (tools/src/webrtc)
## 输入 ZLM 播放地址建立 HTTP 信令并验证链路:
##   http(s)://host/index/api/webrtc?app=live&stream=xxx&type=play   (ZLM 信令原链)
##   webrtc(s)://host[:port]/app/stream                              (ZLM 播放页链接, 自动转换)
## 两种信令模式 (勾选「自定义信令」切换):
##   内置信令: connect_signaling → C++ createZlTestSdpAgent (TestSdpOb) 自动交换
##   自定义信令: GDScript 参考实现 —— open_rtc 等 local_sdp → HTTP POST → set_remote_sdp,
##     即 TestSdpOb 的 GDScript 镜像 (见 _on_local_sdp/_on_sdp_http_done), 抄走改造即成自有信令
## 判定: first_video_frame = 链路通; rtc_error = 异常。状态栏实时显示 conn/fps/loss/rtt;
## 输出 [AVOX][TEST] 判定行与 test_rtc.gd 同款, 供 collect_verdicts.py 汇总。
## 依赖: addons/avox_godot 已部署且 bin/plugins/ 里有 avox_webrtc 动态插件。

const ACCENT      := Color("#3B82F6")
const BG_COLOR    := Color("#0B0F16")
const OK_COLOR    := Color(0.35, 0.82, 0.55)
const WARN_COLOR  := Color(0.95, 0.75, 0.25)
const ERR_COLOR   := Color(0.94, 0.44, 0.42)
const DIM_COLOR   := Color(0.6, 0.64, 0.69)
const CONN_CONNECTED := 2
# conn 状态名 (RtcConnState 0..5)
const CONN_NAMES := ["new", "connecting", "connected", "disconnected", "failed", "closed"]
const PLACEHOLDER := "http://127.0.0.1/index/api/webrtc?app=live&stream=test&type=play"
const CFG_PATH := "user://webrtc_test.cfg"
const MAX_RECENT := 10

# ── 共享 UI 基建 (主题字体/高 DPI 缩放/flat stylebox, 与 mediaplayer/hub 共用) ──
const UiKit := preload("res://src/common/ui_kit.gd")

var rtc: RtcPlayer

# UI 节点 (代码构建)
var _url_edit: LineEdit
var _recent: OptionButton
var _custom_ck: CheckBox
var _video: TextureRect
var _conn_label: Label
var _stat_label: Label
var _toast: Label
var _btn_h := 38
var _input_h := 34

# 运行状态
var _recent_urls := PackedStringArray()
var _last_url := ""            # 最近一次连接的信令地址 (重连判定行用)
var _stat_accum := 0.0
var _first_frame_ms := 0
# 自定义信令 SDP 交换 (HTTP 参考) 用
var _http: HTTPRequest
var _sdp_busy := false
# 判定行 (每次连接一条: 首帧 PASS / rtc_error FAIL)
var _vurl := ""
var _vt0 := 0
var _vdone := true

func _ready() -> void:
	DisplayServer.window_set_title("avox WebRTC 拉流测试")
	# 本场景按 1280x720 设计: 钉画布基准 + 桌面高 DPI 放大窗口 (UiKit 通用适配)
	UiKit.apply_desktop_dpi(get_window(), Vector2i(1280, 720))
	if UiKit.is_touch():
		_btn_h = 46
		_input_h = 40
	theme = UiKit.build_theme()
	_build_ui()
	_load_cfg()
	_build_rtc()

# ── 播放器 ──
func _build_rtc() -> void:
	rtc = RtcPlayer.new()
	# 纯拉流: 只收不发 (默认 sendrecv)
	rtc.set_video_direction(1)
	rtc.set_audio_direction(1)
	rtc.connection_state_changed.connect(_on_conn_state)
	rtc.first_video_frame.connect(_on_first_frame)
	rtc.rtc_error.connect(_on_rtc_error)
	rtc.local_sdp.connect(_on_local_sdp)   # 自定义信令模式从这里接管 (内置模式忽略)
	add_child(rtc)

func _on_conn_state(s: int) -> void:
	var col := WARN_COLOR
	match s:
		CONN_CONNECTED:
			col = OK_COLOR
		4, 5:
			col = ERR_COLOR
	_set_conn_text("连接: %s" % CONN_NAMES[s], col)

func _on_first_frame() -> void:
	# 真正出图才算链路通 (conn=connected 只是 ICE 层)
	_first_frame_ms = Time.get_ticks_msec() - _vt0
	var tex := rtc.get_texture()
	_set_conn_text("连接: connected · 出图 %dx%d (%dms)" % [
		tex.get_width() if tex else 0, tex.get_height() if tex else 0, _first_frame_ms], OK_COLOR)
	_verdict(true, "first_frame conn=%d" % rtc.get_connection_state())

func _on_rtc_error(code: int, msg: String) -> void:
	_set_conn_text("错误 %d: %s" % [code, msg], ERR_COLOR)
	_verdict(false, "error_code=%d msg=%s" % [code, msg])

# ── 连接控制 ──
func _on_connect() -> void:
	var url := _normalize_url(_url_edit.text)
	if url.is_empty():
		_toast_msg("需要 ZLM 播放地址: http(s)://…/index/api/webrtc?…&type=play 或 webrtc://host/app/stream")
		return
	_url_edit.text = url
	_last_url = url
	_push_recent(url)
	_video.texture = null
	_first_frame_ms = 0
	_stat_label.text = ""
	_verdict_start(url)
	if _use_custom_signaling():
		# 自定义信令: 不挂内置 agent, open 后等 local_sdp 信号自己交换 (见下方参考实现)
		rtc.open_rtc()
		_set_conn_text("连接: open_rtc (等 local_sdp…)", WARN_COLOR)
	else:
		rtc.connect_signaling(url)   # 内部销毁重建, 重复调用安全
		_set_conn_text("连接: connecting (信令交换中…)", WARN_COLOR)

func _on_disconnect() -> void:
	rtc.close_rtc()
	_video.texture = null
	_first_frame_ms = 0
	_stat_label.text = ""
	_set_conn_text("未连接", DIM_COLOR)

func _on_reconnect() -> void:
	if _last_url.is_empty():
		_on_connect()
		return
	_video.texture = null
	_first_frame_ms = 0
	_stat_label.text = ""
	_set_conn_text("连接: reconnecting…", WARN_COLOR)
	_verdict_start(_last_url)
	rtc.reconnect_rtc()   # player 不存在时内部按 signalingUrl 重建

func _back_home() -> void:
	# RtcPlayer 在 EXIT_TREE 自行 close_rtc
	get_tree().change_scene_to_file("res://src/hub/main.tscn")

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		_back_home()

## webrtc(s)://host[:port]/app/stream → ZLM HTTP 信令原链; 其它仅放行 http(s)
func _normalize_url(raw: String) -> String:
	var u := raw.strip_edges()
	if u.is_empty():
		return ""
	if u.begins_with("webrtc://") or u.begins_with("webrtcs://"):
		var secure := u.begins_with("webrtcs://")
		var rest := u.substr(u.find("://") + 3)
		var qpos := rest.find("?")
		var seg := (rest.substr(0, qpos) if qpos >= 0 else rest).split("/", false)
		if seg.size() < 3:
			return ""
		var scheme := "https" if secure or seg[0].ends_with(":443") else "http"
		var out := "%s://%s/index/api/webrtc?app=%s&stream=%s&type=play" % [scheme, seg[0], seg[1], seg[2]]
		if qpos >= 0 and not rest.substr(qpos + 1).is_empty():
			out += "&" + rest.substr(qpos + 1)
		return out
	if u.begins_with("http://") or u.begins_with("https://"):
		return u
	return ""

func _use_custom_signaling() -> bool:
	return _custom_ck.button_pressed

# ── 自定义信令参考实现 (TestSdpOb 的 GDScript 镜像, 抄走改信令协议即可) ──
# 流程: open_rtc → 播放器生成 offer → local_sdp 信号 → POST (body=SDP 明文)
#       → 响应 JSON {code, msg, sdp} → code==0 时 set_remote_sdp 回填 answer。
# ZLM 的 answer 里自带 ICE 候选 (非 trickle), 不用 add_ice_candidate;
# trickle 型服务器再监听 ice_candidate 信号送出、add_ice_candidate 回填。
# 推流方向: set_roll_type(1) (ANSWER 被动) + ZLM type=push, 收到对端 offer 同样 set_remote_sdp 回填。

func _on_local_sdp(sdp: String) -> void:
	# 内置信令模式也发 local_sdp 信号, 这里只认自定义模式
	if not _use_custom_signaling() or sdp.is_empty():
		return
	if _http == null:
		_http = HTTPRequest.new()
		_http.timeout = 10.0
		_http.request_completed.connect(_on_sdp_http_done)
		add_child(_http)
	if _sdp_busy:
		_on_rtc_error(-1, "sdp http busy")
		return
	_sdp_busy = true
	_set_conn_text("连接: POST 信令中…", WARN_COLOR)
	var headers := PackedStringArray([
		"Content-Type: text/plain;charset=utf-8",
		"User-Agent: Mozilla/5.0",
		"Accept: */*"])
	# body = 本地 SDP 明文, 与 C++ TestSdpOb 的 mk_http POST 完全同构
	_http.request(_last_url, headers, HTTPClient.METHOD_POST, sdp)

func _on_sdp_http_done(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_sdp_busy = false
	if result != 0 or response_code != 200:
		_on_rtc_error(response_code, "sdp http failed result=%d code=%d" % [result, response_code])
		return
	var json: Variant = JSON.parse_string(body.get_string_from_utf8())
	if json == null or not (json is Dictionary):
		_on_rtc_error(-2, "sdp http bad json")
		return
	var code := int(json.get("code", -1))
	if code != 0:
		# 如 ZLM -400 stream not found, 上层据此提示或重试
		_on_rtc_error(code, String(json.get("msg", "")))
		return
	var sdp := String(json.get("sdp", ""))
	if sdp.is_empty():
		_on_rtc_error(-3, "sdp http empty sdp")
		return
	print("remote sdp: %d bytes" % sdp.length())   # 要看原始报文改回 print(sdp)
	rtc.set_remote_sdp(sdp)

# ── 帧驱动: 纹理刷新 + 0.5s 节流的媒体统计 ──
func _process(delta: float) -> void:
	if rtc:
		var t := rtc.get_texture()
		if t and _video.texture != t:
			_video.texture = t
	_stat_accum += delta
	if _stat_accum < 0.5 or rtc == null or _first_frame_ms <= 0:
		return
	_stat_accum = 0.0
	var tex := rtc.get_texture()
	_stat_label.text = "fps %.1f · loss %.1f%% · rtt %dms · %dx%d" % [
		rtc.get_fps(), rtc.get_loss_rate() * 100.0, rtc.get_rtt_ms(),
		tex.get_width() if tex else 0, tex.get_height() if tex else 0]

# ── 判定行 ──
func _verdict_start(url: String) -> void:
	_vurl = url
	_vt0 = Time.get_ticks_msec()
	_vdone = false

func _verdict(passed: bool, detail: String) -> void:
	if _vdone or _vurl.is_empty():
		return
	_vdone = true
	print("[AVOX][TEST] case=rtc-ui result=%s url=%s t=%dms %s" % [
		"PASS" if passed else "FAIL", _vurl, Time.get_ticks_msec() - _vt0, detail])

# ── 历史 (最近地址, 持久化 user://webrtc_test.cfg) ──
func _load_cfg() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(CFG_PATH) != OK:
		return
	_recent_urls = cfg.get_value("history", "urls", PackedStringArray())
	_url_edit.text = cfg.get_value("history", "last", "")
	_rebuild_recent()

func _save_cfg() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("history", "urls", _recent_urls)
	cfg.set_value("history", "last", _last_url)
	cfg.save(CFG_PATH)

func _push_recent(url: String) -> void:
	var arr: Array = []
	for u in _recent_urls:
		if u != url:
			arr.append(u)
	arr.push_front(url)
	if arr.size() > MAX_RECENT:
		arr.resize(MAX_RECENT)
	_recent_urls = PackedStringArray(arr)
	_rebuild_recent()
	_save_cfg()

func _rebuild_recent() -> void:
	_recent.clear()
	_recent.add_item("── 最近地址 ──", 0)
	_recent.set_item_disabled(0, true)
	_recent.select(0)
	for i in _recent_urls.size():
		_recent.add_item(_mid_ellipsis(_recent_urls[i]), i + 1)
	_recent.visible = _recent_urls.size() > 0

func _on_recent_selected(idx: int) -> void:
	# idx 0 是表头(禁用); idx>=1 对应 _recent_urls[idx-1], 选中即连
	if idx < 1 or idx - 1 >= _recent_urls.size():
		return
	_url_edit.text = _recent_urls[idx - 1]
	_on_connect()

func _mid_ellipsis(s: String, max_len: int = 52) -> String:
	if s.length() <= max_len:
		return s
	var head := int(max_len * 0.6)
	var tail := max_len - head - 1
	return s.substr(0, head) + "…" + s.substr(s.length() - tail)

# ── UI 构建 ──
func _flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	return UiKit.flat(bg, radius, border, bw)

func _style_primary(b: Button, min_w: float = 88.0) -> void:
	b.custom_minimum_size = Vector2(min_w, _btn_h)
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_stylebox_override("normal", _flat(ACCENT, 6))
	b.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 6))
	b.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 6))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	b.add_theme_color_override("font_color", Color.WHITE)

func _style_ghost(b: Button, min_w: float = 72.0) -> void:
	b.custom_minimum_size = Vector2(min_w, _btn_h)
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.08), 6))
	b.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.16), 6))
	b.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.12), 6))
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	b.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))

func _set_conn_text(text: String, col: Color) -> void:
	_conn_label.text = text
	_conn_label.add_theme_color_override("font_color", col)

func _toast_msg(msg: String) -> void:
	_toast.text = msg
	_toast.modulate.a = 1.0
	var tw := create_tween()
	tw.tween_interval(2.5)
	tw.tween_property(_toast, "modulate:a", 0.0, 0.4)

func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = BG_COLOR
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(bg)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_bottom", 10)
	add_child(margin)
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 8)
	margin.add_child(rows)

	# 顶栏: 标题 + 返回主页
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 8)
	rows.add_child(top)
	var title := Label.new()
	title.text = "WebRTC 拉流测试"
	title.add_theme_font_override("font", UiKit.font_weight(600))
	title.add_theme_font_size_override("font_size", 17)
	title.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	title.mouse_filter = Control.MOUSE_FILTER_IGNORE
	top.add_child(title)
	var home := Button.new()
	home.text = "返回主页"
	_style_ghost(home, 88)
	home.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	home.pressed.connect(_back_home)
	top.add_child(home)

	# 控制行: 信令地址输入 + 最近历史 + 连接/断开/重连
	var ctrl := HBoxContainer.new()
	ctrl.add_theme_constant_override("separation", 8)
	rows.add_child(ctrl)
	_url_edit = LineEdit.new()
	_url_edit.placeholder_text = PLACEHOLDER
	_url_edit.custom_minimum_size = Vector2(0, _input_h)
	_url_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_url_edit.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_url_edit.text_submitted.connect(func(_t: String): _on_connect())
	ctrl.add_child(_url_edit)
	_recent = OptionButton.new()
	_recent.clip_text = true
	_recent.custom_minimum_size = Vector2(180, _input_h)
	_recent.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_recent.focus_mode = Control.FOCUS_NONE
	_recent.visible = false
	_recent.item_selected.connect(_on_recent_selected)
	ctrl.add_child(_recent)
	var connect_btn := Button.new()
	connect_btn.text = "连接"
	_style_primary(connect_btn, 80)
	connect_btn.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	connect_btn.pressed.connect(_on_connect)
	ctrl.add_child(connect_btn)
	var stop_btn := Button.new()
	stop_btn.text = "断开"
	_style_ghost(stop_btn, 64)
	stop_btn.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	stop_btn.pressed.connect(_on_disconnect)
	ctrl.add_child(stop_btn)
	var retry_btn := Button.new()
	retry_btn.text = "重连"
	_style_ghost(retry_btn, 64)
	retry_btn.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	retry_btn.pressed.connect(_on_reconnect)
	ctrl.add_child(retry_btn)
	# 信令模式: 默认内置 TestSdpOb; 勾选走下方 GDScript 参考实现 (下次连接生效)
	_custom_ck = CheckBox.new()
	_custom_ck.text = "自定义信令"
	_custom_ck.tooltip_text = "用 GDScript 参考实现交换 SDP (open_rtc + local_sdp → HTTP POST → set_remote_sdp), 抄走可改自有信令; 默认走内置 TestSdpOb"
	_custom_ck.focus_mode = Control.FOCUS_NONE
	_custom_ck.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	ctrl.add_child(_custom_ck)

	# 视频区: 深色面板内嵌 TextureRect (信箱式留黑边, 出图前是空态面板)
	var video_panel := PanelContainer.new()
	video_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	video_panel.add_theme_stylebox_override("panel", _flat(Color(1, 1, 1, 0.03), 10, Color(1, 1, 1, 0.08), 1))
	rows.add_child(video_panel)
	var vm := MarginContainer.new()
	vm.add_theme_constant_override("margin_left", 6)
	vm.add_theme_constant_override("margin_right", 6)
	vm.add_theme_constant_override("margin_top", 6)
	vm.add_theme_constant_override("margin_bottom", 6)
	video_panel.add_child(vm)
	_video = TextureRect.new()
	_video.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_video.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	vm.add_child(_video)

	# 状态栏: 连接状态行 + 媒体统计行
	var status := PanelContainer.new()
	status.add_theme_stylebox_override("panel", _flat(Color(1, 1, 1, 0.04), 8))
	rows.add_child(status)
	var sm := MarginContainer.new()
	sm.add_theme_constant_override("margin_left", 12)
	sm.add_theme_constant_override("margin_right", 12)
	sm.add_theme_constant_override("margin_top", 8)
	sm.add_theme_constant_override("margin_bottom", 8)
	status.add_child(sm)
	var sbox := VBoxContainer.new()
	sbox.add_theme_constant_override("separation", 4)
	sm.add_child(sbox)
	_conn_label = Label.new()
	_conn_label.text = "未连接"
	_conn_label.add_theme_font_size_override("font_size", 14)
	_conn_label.add_theme_color_override("font_color", DIM_COLOR)
	_conn_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sbox.add_child(_conn_label)
	_stat_label = Label.new()
	_stat_label.add_theme_font_size_override("font_size", 13)
	_stat_label.add_theme_color_override("font_color", DIM_COLOR)
	_stat_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sbox.add_child(_stat_label)

	# toast (底部居中, 与 hub 同款)
	_toast = Label.new()
	_toast.add_theme_font_size_override("font_size", 13)
	_toast.add_theme_color_override("font_color", Color(1.0, 0.85, 0.55))
	_toast.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.7))
	_toast.add_theme_constant_override("shadow_offset_y", 1)
	_toast.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_toast.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_toast.offset_top = -56
	_toast.offset_bottom = -28
	_toast.modulate.a = 0.0
	add_child(_toast)
