extends SceneTree
## SourceProbe + 播放 无头链路测试: 探测磁力元数据 → 文件列表 → 选择 →
## 写播放选项 → MediaPlayer 起播 → PLAYING 且进度前进。
## 运行: godot --headless --path tools -s res://tests/test_probe.gd
## 退出码: 0=通过, 1=失败/超时。

const MAGNET := "magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=Big+Buck+Bunny&tr=udp%3A%2F%2Fexplodie.org%3A6969&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337&tr=wss%3A%2F%2Ftracker.btorrent.xyz&tr=wss%3A%2F%2Ftracker.fastcast.nz&tr=wss%3A%2F%2Ftracker.openwebtorrent.com&ws=https%3A%2F%2Fwebtorrent.io%2Ftorrents%2F"
const TIMEOUT_MS := 90000
const ST_PLAYING := 3

var _probe: SourceProbe
var _pick := {}
var _player: MediaPlayer
var _t0 := 0
var _done := false
var _phase := 1

func _initialize() -> void:
	print("== SourceProbe headless test ==")
	_probe = SourceProbe.new()
	if _probe == null:
		print("FAIL: SourceProbe 类不可用 (avox_godot 未加载?)")
		quit(1)
		return
	_probe.probe_result.connect(_on_probed)
	_t0 = Time.get_ticks_msec()
	var ok := _probe.start(MAGNET, "", 45000)
	print("start() -> ", ok, " probing=", _probe.is_probing())
	if not ok:
		print("FAIL: start 返回 false (avox_torrent 插件未加载?)")
		quit(1)

func _process(_delta: float) -> bool:
	if _done:
		return true
	if Time.get_ticks_msec() - _t0 > TIMEOUT_MS:
		if _phase == 1:
			print("FAIL: 探测超时 last_error=%s" % _probe.get_last_error())
		else:
			print("FAIL: 起播超时 state=%d duration=%d pos=%d" % [
				_player.get_state(), _player.get_duration(), _player.get_position()])
		_done = true
		quit(1)
	return false

func _on_probed(code: int) -> void:
	var elapsed := Time.get_ticks_msec() - _t0
	if code != 0:
		print("FAIL(%dms): 探测失败: %s" % [elapsed, _probe.get_last_error()])
		_done = true
		quit(1)
		return
	print("PROBE OK (%dms) name=%s hash=%s total=%d" % [
		elapsed, _probe.get_name(), _probe.get_info_hash(), _probe.get_total_size()])
	var files: Array = _probe.get_files()
	for f in files:
		print("  [index=%d] %s  %d bytes  media=%s" % [f["index"], f["path"], f["size"], f["media"]])
	if files.is_empty():
		print("FAIL: 文件列表为空")
		_done = true
		quit(1)
		return
	_pick = files[0]
	_probe.select_file(_pick["index"])
	# probe 自身的选择写入独立 option 验证 API(与播放无关)
	var opt_check: AvoxOption = AvoxOption.new()
	var ok_api: bool = _probe.apply_to_option(opt_check)
	print("probe.apply_to_option(独立option)=%s fileIndex=%s" % [ok_api, opt_check.get("torrent.fileIndex", -999)])
	# 起播: 选项走 MediaPlayer 预设(与 UI 同路径: 首次 play 前 get_option 为空)
	_player = MediaPlayer.new()
	root.add_child(_player)
	_player.state_changed.connect(_on_state)
	_player.io_error.connect(func(c): print("io_error code=", c))
	_player.set_option("torrent.fileIndex", _pick["index"])
	_player.set_option("torrent.metaTimeoutMs", 60000)
	print("set_option(fileIndex=%d) -> play()" % _pick["index"])
	_phase = 2
	_t0 = Time.get_ticks_msec()
	_player.url = MAGNET
	_player.play()

func _on_state(s: int) -> void:
	if _phase != 2 or _done:
		return
	var dur: int = _player.get_duration()
	var pos: int = _player.get_position()
	print("state -> %d duration=%d pos=%d" % [s, dur, pos])
	if s == ST_PLAYING and dur > 0:
		# 已起播: 再等进度走起来(流式下载供数正常)
		if pos > 0:
			print("== PASS == 探测+选择+起播+进度推进 全链路通过 (pos=%d)" % pos)
			_done = true
			quit(0)
