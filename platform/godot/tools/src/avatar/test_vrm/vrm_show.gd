extends Control
## VRM 目检场景 (临时): 加载 Seed-san, 1s 推近到脸; idle 幅度 x10 供目检躯干摆动。
## 运行: godot --path tools res://src/avatar/test_vrm/vrm_show.tscn

var view

func _ready() -> void:
	view = preload("res://src/avatar/avatar_view.gd").new()
	add_child(view)
	view.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	view.size = size   # 兜底: 直接铺满当前窗口
	print("[vrm_show] loaded=", view.load_model("res://src/avatar/test_vrm/SeedSan.vrm"),
		" info=", view.vrm_info())
	get_tree().create_timer(1.0).timeout.connect(func():
		view._orbit_target += Vector3(0, 0.35, 0)   # 上移到胸 (临时场景直读内部态)
		view.set_orbit_dist(1.6)
		var idle = view.idle()
		if idle:
			idle.amplitude = 10.0   # 目检放大 (实际运行 1.0)
		print("[vrm_show] idle=", idle != null, " active=", idle.active if idle else false))
