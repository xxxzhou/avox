extends SceneTree
## WebRTC 拉流无头链路测试: RtcPlayer 走 HTTP 信令 → first_video_frame 出图。
## 运行: godot --headless --path tools -s res://tests/test_rtc.gd -- <信令url> [--timeout-ms=30000] [--case=<id>]
## 退出码: 0=通过, 1=失败/超时。判定行 [AVOX][TEST] case=rtc-<url清洗> 供 collect_verdicts.py 汇总。

const CONN_CONNECTED := 2

var _rtc: RtcPlayer
var _url := ""
var _case := ""
var _timeout_ms := 30000
var _t0 := 0
var _done := false

func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--timeout-ms="):
			_timeout_ms = a.get_slice("=", 1).to_int()
		elif a.begins_with("--case="):
			_case = a.get_slice("=", 1)
		elif not a.begins_with("--") and _url.is_empty():
			_url = a
	if _url.is_empty():
		print("[AVOX][TEST] case=rtc-missing-arg result=FAIL detail=missing_url_arg")
		_done = true   # quit() 后 _process 仍会跑一帧, 置位避免空 _rtc 解引用
		quit(1)
		return
	if _case.is_empty():
		_case = _case_from_url(_url)
	_rtc = RtcPlayer.new()
	root.add_child(_rtc)
	# 纯拉流: 只收不发
	_rtc.set_video_direction(1)
	_rtc.set_audio_direction(1)
	_rtc.first_video_frame.connect(_on_first_frame)
	_rtc.rtc_error.connect(_on_err)
	_rtc.connect_signaling(_url)
	_t0 = Time.get_ticks_msec()
	print("== test_rtc headless: %s (timeout %dms) ==" % [_url, _timeout_ms])

func _case_from_url(url: String) -> String:
	var re := RegEx.new()
	re.compile("[^a-z0-9]+")
	return "rtc-" + re.sub(url.to_lower(), "-", true).substr(0, 48)

func _process(_delta: float) -> bool:
	if _done:
		return true
	if Time.get_ticks_msec() - _t0 > _timeout_ms:
		_fail("timeout conn=%d state=%d" % [_rtc.get_connection_state(), _rtc.get_state()])
		return true
	return false

func _on_first_frame() -> void:
	_pass("t=%dms conn=%d fps=%.1f" % [
		Time.get_ticks_msec() - _t0, _rtc.get_connection_state(), _rtc.get_fps()])

func _on_err(code: int, msg: String) -> void:
	_fail("error_code=%d msg=%s" % [code, msg])

func _pass(detail: String) -> void:
	if _done:
		return
	_done = true
	print("[AVOX][TEST] case=%s result=PASS url=%s %s" % [_case, _url, detail])
	quit(0)

func _fail(detail: String) -> void:
	if _done:
		return
	_done = true
	print("[AVOX][TEST] case=%s result=FAIL url=%s %s" % [_case, _url, detail])
	quit(1)
