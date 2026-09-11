extends SceneTree
## RemoteSource + 播放 无头链路测试: open(探测磁力元数据) → list(根目录) →
## resolve(选文件写选项) → MediaPlayer 起播 → PLAYING 且进度前进。
## 运行: godot --headless --path tools -s res://tests/test_probe.gd
## 退出码: 0=通过, 1=失败/超时。

const MAGNET := "magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&dn=Big+Buck+Bunny&tr=udp%3A%2F%2Fexplodie.org%3A6969&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337&tr=wss%3A%2F%2Ftracker.btorrent.xyz&tr=wss%3A%2F%2Ftracker.fastcast.nz&tr=wss%3A%2F%2Ftracker.openwebtorrent.com&ws=https%3A%2F%2Fwebtorrent.io%2Ftorrents%2F"
const TIMEOUT_MS := 90000
const ST_PLAYING := 3

var _remote: RemoteSource
var _pick := {}
var _player: MediaPlayer
var _t0 := 0
var _done := false
var _phase := 1

func _initialize() -> void:
	print("== RemoteSource headless test ==")
	_remote = RemoteSource.new()
	if _remote == null:
		print("FAIL: RemoteSource 类不可用 (avox_godot 未加载?)")
		quit(1)
		return
	_remote.open_result.connect(_on_opened)
	_remote.list_result.connect(_on_listed)
	_t0 = Time.get_ticks_msec()
	var ok := _remote.open(MAGNET, 45000)
	print("open() -> ", ok, " busy=", _remote.is_busy())
	if not ok:
		print("FAIL: open 返回 false (avox_torrent 插件未加载?)")
		quit(1)

func _process(_delta: float) -> bool:
	if _done:
		return true
	if Time.get_ticks_msec() - _t0 > TIMEOUT_MS:
		if _phase == 1:
			print("FAIL: 会话超时 last_error=%s" % _remote.get_last_error())
		else:
			print("FAIL: 起播超时 state=%d duration=%d pos=%d" % [
				_player.get_state(), _player.get_duration(), _player.get_position()])
		_done = true
		quit(1)
	return false

func _on_opened(code: int) -> void:
	var elapsed := Time.get_ticks_msec() - _t0
	if code != 0:
		print("FAIL(%dms): open 失败 code=%d: %s" % [elapsed, code, _remote.get_last_error()])
		_done = true
		quit(1)
		return
	print("OPEN OK (%dms) name=%s hash=%s total=%s" % [
		elapsed, _remote.get_session_field("name"), _remote.get_session_field("infoHash"),
		_remote.get_session_field("totalSize")])
	if not _remote.list(""):
		print("FAIL: list 返回 false")
		_done = true
		quit(1)

func _on_listed(code: int) -> void:
	if code != 0:
		print("FAIL: list 失败 code=%d: %s" % [code, _remote.get_last_error()])
		_done = true
		quit(1)
		return
	var entries: Array = _remote.get_entries()
	for e in entries:
		print("  [type=%d] %s  %d bytes  media=%s  token=%s" % [
			e["type"], e["name"], e["size"], e["media"], e["token"]])
	# 选第一个可播媒体
	for e in entries:
		if e["media"]:
			_pick = e
			break
	if _pick.is_empty():
		print("FAIL: 无可播媒体条目")
		_done = true
		quit(1)
		return
	# resolve 验证 API: 产出播放 URL(磁力即原链)并写入独立 option
	var opt_check: AvoxOption = AvoxOption.new()
	var play_url: String = _remote.resolve(entries.find(_pick), opt_check)
	print("resolve() -> %s fileIndex=%s" % [play_url.left(48), opt_check.get("torrent.fileIndex", -999)])
	if play_url.is_empty():
		print("FAIL: resolve 返回空: %s" % _remote.get_last_error())
		_done = true
		quit(1)
		return
	# 起播: 选项走 MediaPlayer 预设(与 UI 同路径: 首次 play 前 get_option 为空)
	_player = MediaPlayer.new()
	root.add_child(_player)
	_player.state_changed.connect(_on_state)
	_player.io_error.connect(func(c): print("io_error code=", c))
	_player.set_option("torrent.fileIndex", opt_check.get_int("torrent.fileIndex"))
	_player.set_option("torrent.metaTimeoutMs", 60000)
	print("set_option(fileIndex=%d) -> play()" % opt_check.get_int("torrent.fileIndex"))
	_phase = 2
	_t0 = Time.get_ticks_msec()
	_player.url = play_url
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
			print("== PASS == open+list+resolve+起播+进度推进 全链路通过 (pos=%d)" % pos)
			_done = true
			quit(0)
