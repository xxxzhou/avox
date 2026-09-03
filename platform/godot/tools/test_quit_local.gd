extends SceneTree
## 对照组: 本地文件播放中直接退出, 分类 teardown 段错误是否 torrent 特有。
## 运行: godot --headless --path tools -s res://test_quit_local.gd

const LOCAL := "D:/Work/github/avox/tmp/0202_novoice.mp4"

var _player: MediaPlayer
var _t0 := 0

func _initialize() -> void:
	print("== local play + quit test ==", " file=", LOCAL)
	_player = MediaPlayer.new()
	root.add_child(_player)
	_player.state_changed.connect(_on_state)
	_t0 = Time.get_ticks_msec()
	_player.url = LOCAL
	_player.play()

func _process(_delta: float) -> bool:
	if Time.get_ticks_msec() - _t0 > 30000:
		print("FAIL: 超时")
		quit(1)
	return false

func _on_state(s: int) -> void:
	var pos: int = _player.get_position()
	print("state -> %d pos=%d" % [s, pos])
	if s == 3 and pos > 0:
		print("playing, quit now (模拟播放中退出)")
		quit(0)
