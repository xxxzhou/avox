extends SceneTree
## VRM 加载链路无头测试: avatar_view.load_model → vrm_map 解析 + 骨骼发现 + 非 VRM 回归。
## 运行: godot --headless --path tools -s res://src/avatar/test_vrm/test_vrm.gd
## 注: -s SceneTree 脚本 _initialize 时 root 尚未创建, 挂首帧 process_frame 再跑。

func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)

func _run() -> void:
	var errors := 0
	for path in ["res://src/avatar/test_vrm/SeedSan.vrm",
			"res://src/avatar/test_vrm/VRM1_Constraint_Twist_Sample.vrm",
			"res://src/avatar/avatar.gltf"]:
		var view = preload("res://src/avatar/avatar_view.gd").new()
		root.add_child(view)
		var ok: bool = view.load_model(path)
		var info: Dictionary = view.vrm_info()
		var skel = view.get_skeleton()
		var has_bones: bool = skel != null and skel.get_bone_count() > 0
		print("TEST ", path.get_file(), " loaded=", ok, " vrm=", info.kind, " binds=", info.binds,
			" emotions=", info.emotions, " look=", info.look, " humanoid=", info.humanoid,
			" skeleton=", has_bones)
		if path.ends_with(".vrm"):
			if not ok or info.kind == "" or info.binds < 5 or info.emotions < 4 or info.humanoid < 10:
				errors += 1
				print("TEST !! VRM 判定失败")
		else:
			if not ok or info.kind != "":
				errors += 1
				print("TEST !! 非 VRM 判定失败")
		view.free()
	print("TEST === ", "PASS" if errors == 0 else "FAIL (%d)" % errors, " ===")
	quit(errors)
