extends SceneTree
## 商店截图生成器 (一次性工具, 不进主场景)。
## 用法: godot --path platform/godot/samples --resolution 1280x720 -s res://tools/screenshot_gen.gd
## 逐场景切换 → 等待渲染 → (local_player 自动播真实视频) → 视口抓帧存 PNG。

const VIDEO := "D:/Work/github/avox/assets/video/avox_electron.mp4"

var _dir: String
var _tasks: Array = []
var _wait := 0
var _after: Callable = Callable()

func _initialize() -> void:
	_dir = ProjectSettings.globalize_path("res://").path_join("screenshots/")
	DirAccess.make_dir_recursive_absolute(_dir)
	_tasks = [
		{"scene": "res://scenes/hub.tscn", "frames": 50, "shot": "hub.png", "action": Callable()},
		{"scene": "res://scenes/local_player.tscn", "frames": 50, "shot": "local_player.png", "action": _play_video},
		{"scene": "res://scenes/live_stream.tscn", "frames": 50, "shot": "live_stream.png", "action": _fill_url},
		{"scene": "res://scenes/mic_subtitle.tscn", "frames": 40, "shot": "mic_subtitle.png", "action": Callable()},
	]
	process_frame.connect(_tick)
	_next()

func _next() -> void:
	if _tasks.is_empty():
		print("SCREENSHOT_GEN_DONE")
		quit()
		return
	var t: Dictionary = _tasks.pop_front()
	change_scene_to_file(String(t["scene"]))
	_after = Callable(t["action"])
	_wait = int(t["frames"])
	_shot_name = String(t["shot"])

var _shot_name := ""

func _tick() -> void:
	if _wait > 0:
		_wait -= 1
		if _wait == 0:
			if _after.is_valid():
				var action := _after
				_after = Callable()          # 只执行一次, 否则 action 重设 _wait 造成死循环
				action.call()
				_wait = _post_action_frames
				_post_action_frames = 0
				if _wait <= 0:
					_capture()
			else:
				_capture()

var _post_action_frames := 0

func _play_video() -> void:
	_post_action_frames = 150          # 播 2.5s 视频再截, 画面里有真实帧
	for m in current_scene.find_children("*", "MediaPlayer", true, false):
		m.play(VIDEO)
		break

func _fill_url() -> void:
	_post_action_frames = 10
	for e in current_scene.find_children("*", "LineEdit", true, false):
		e.text = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"
		break

func _capture() -> void:
	var img := root.get_texture().get_image()
	var path := _dir + _shot_name
	var err := img.save_png(path)
	print(("SAVED " if err == OK else "SAVE_FAILED(" + str(err) + ") ") + path + " " + str(img.get_width()) + "x" + str(img.get_height()))
	_next()
