extends SceneTree
## URL 播放无头链路测试: MediaPlayer 起 URL → PLAYING 且进度推进。
## 运行: godot --headless --path tools -s res://tests/test_url.gd -- <url> [--timeout-ms=30000] [--case=<id>]
## 退出码: 0=通过, 1=失败/超时。判定行 [AVOX][TEST] case=url-<url清洗> 供 collect_verdicts.py 汇总。

const ST_PLAYING := 3

var _player: MediaPlayer
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
		print("[AVOX][TEST] case=url-missing-arg result=FAIL detail=missing_url_arg")
		_done = true   # quit() 后 _process 仍会跑一帧, 置位避免空 _player 解引用
		quit(1)
		return
	if _case.is_empty():
		_case = _case_from_url(_url)
	_player = MediaPlayer.new()
	root.add_child(_player)
	_player.state_changed.connect(_on_state)
	_player.io_error.connect(_on_err)
	_player.decode_error.connect(_on_err)
	_player.url = _url
	_player.play()
	_t0 = Time.get_ticks_msec()
	print("== test_url headless: %s (timeout %dms) ==" % [_url, _timeout_ms])

func _case_from_url(url: String) -> String:
	var re := RegEx.new()
	re.compile("[^a-z0-9]+")
	return "url-" + re.sub(url.to_lower(), "-", true).substr(0, 48)

func _process(_delta: float) -> bool:
	if _done:
		return true
	if Time.get_ticks_msec() - _t0 > _timeout_ms:
		_fail("timeout state=%d" % _player.get_state())
		return true
	if _player.get_state() == ST_PLAYING and _player.get_position() > 0:
		_done = true
		print("[AVOX][TEST] case=%s result=PASS url=%s t=%dms pos=%d" % [
			_case, _url, Time.get_ticks_msec() - _t0, int(_player.get_position())])
		quit(0)
	return false

func _on_state(_s: int) -> void:
	pass   # 状态轮询走 _process, 与 test_probe.gd 的事件驱动区分开

func _on_err(code: int) -> void:
	_fail("error_code=%d" % code)

func _fail(detail: String) -> void:
	if _done:
		return
	_done = true
	print("[AVOX][TEST] case=%s result=FAIL url=%s %s" % [_case, _url, detail])
	quit(1)
