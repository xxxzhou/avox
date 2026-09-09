extends SceneTree
## headless 回归测试: retarget.gd "双脚向上" bug (Godot 4 pose 语义)
## 用法:  godot --headless --path tools --script res://tests/test_retarget_check.gd
## 对照修复前版本:
##   git show HEAD:platform/godot/tools/src/avatar/retarget.gd > platform/godot/tools/src/avatar/retarget_old.gd
##   RETARGET_SCRIPT=res://src/avatar/retarget_old.gd godot --headless --path tools --script res://tests/test_retarget_check.gd
##
## 不需要视频/GUI: 直接合成 MediaPipe 33 点 landmark (raw 坐标系同 body.cpp: [0,1], y 向下,
## conf 同热图峰值语义), 调 retarget.drive() 收敛后断言脚骨全局 Y < 髋骨全局 Y。
## 两个用例覆盖两条曾致翻腿的路径:
##   A 全身站立 → 正常驱动路径 (旧版公式多乘 rest^-1 → pose≈IDENTITY → 翻腿)
##   B 半身(髋 y>0.85 出画) → 腿不可见复位路径 (旧版复位到 IDENTITY → 翻腿)

const TOL := 0.05  # 脚骨全局 Y 与 rest 的允许偏差 (复位用例)

var _fail := 0

func _initialize() -> void:
	# 等第一帧 (process_frame) 再跑: 保证场景节点已进树, global_transform 可用 (同 main.gd 运行环境)
	process_frame.connect(_run, CONNECT_ONE_SHOT)

func _run() -> void:
	var script_path := OS.get_environment("RETARGET_SCRIPT")
	if script_path.is_empty():
		script_path = "res://src/avatar/retarget.gd"
	print("== retarget 回归测试, 脚本: %s ==" % script_path)
	var gltf := GLTFDocument.new()
	var state := GLTFState.new()
	var err := gltf.append_from_file("res://src/avatar/avatar.gltf", state)
	if err != OK:
		print("FAIL: 加载 avatar.gltf 失败 err=%d" % err)
		quit(1)
		return
	var scene := gltf.generate_scene(state)
	root.add_child(scene)
	var skeletons := scene.find_children("*", "Skeleton3D", true, false)
	if skeletons.is_empty():
		print("FAIL: 模型无 Skeleton3D")
		quit(1)
		return
	var skel: Skeleton3D = skeletons[0]
	var rt_script: Script = load(script_path)
	if rt_script == null:
		print("FAIL: 加载脚本失败: %s" % script_path)
		quit(1)
		return
	# 用例 A: 全身站立 → 驱动路径
	var rt = rt_script.new(skel, scene)
	_diag(rt, skel, "A")
	_case_drive(rt, skel, "A 全身站立(驱动路径)", _lm_full_body(), _conf_uniform(0.6))
	_diag_pose(skel, "A 驱动后")
	# 用例 B: 半身 (髋出画 + 腿 landmark 低置信) → 复位路径
	for i in range(skel.get_bone_count()):
		skel.reset_bone_pose(i)
	rt = rt_script.new(skel, scene)
	_case_reset(rt, skel, "B 半身髋出画(复位路径)", _lm_half_body(), _conf_half_body())
	# 用例 C: 手臂 landmark 低置信 → 手臂自然下垂默认姿态 (上臂+前臂长轴都应≈垂直向下)
	for i in range(skel.get_bone_count()):
		skel.reset_bone_pose(i)
	rt = rt_script.new(skel, scene)
	_case_relax_arms(rt, skel, "C 手臂自然下垂", _lm_full_body(), _conf_arms_low())
	print("== 结果: %s ==" % ("PASS" if _fail == 0 else "FAIL (%d)" % _fail))
	quit(1 if _fail > 0 else 0)

# 松弛后骨骼长轴 (全局) 与竖直向下 -Y 的夹角 (度)
func _axis_world_deg(rt, skel: Skeleton3D, bone: String) -> float:
	var idx: int = skel.find_bone(bone)
	var axis_w: Vector3 = rt._axis_dir.get(bone, Vector3.ZERO)
	if axis_w == Vector3.ZERO:
		return NAN
	var rest_b: Basis = skel.get_bone_global_rest(idx).basis.orthonormalized()
	var local: Vector3 = rest_b.inverse() * axis_w
	var now: Vector3 = (skel.get_bone_global_pose(idx).basis.orthonormalized() * local).normalized()
	return rad_to_deg(now.angle_to(Vector3(0, -1, 0)))

func _case_relax_arms(rt, skel: Skeleton3D, title: String, lm: PackedVector3Array, conf: PackedFloat32Array) -> void:
	for i in range(180):
		rt.drive(lm, conf, 0.033)
	for b in ["LeftArm", "LeftForeArm", "RightArm", "RightForeArm"]:
		var deg := _axis_world_deg(rt, skel, b)
		_check(title, "%s 下垂(≈竖直)" % b, deg < 10.0, "长轴偏离 -Y %.1f°" % deg)

func _case_drive(rt, skel: Skeleton3D, title: String, lm: PackedVector3Array, conf: PackedFloat32Array) -> void:
	var rest := _bone_y(skel)
	for i in range(180):
		rt.drive(lm, conf, 0.033)
	var y := _bone_y(skel)
	# 脚/膝必须在髋下方 (旧版翻腿时 footY > hipY)
	_check(title, "左脚在髋下", y.foot_l < y.hip_l, "footY=%.3f hipY=%.3f (rest footY=%.3f)" % [y.foot_l, y.hip_l, rest.foot_l])
	_check(title, "右脚在髋下", y.foot_r < y.hip_r, "footY=%.3f hipY=%.3f (rest footY=%.3f)" % [y.foot_r, y.hip_r, rest.foot_r])
	_check(title, "左膝在髋下", y.knee_l < y.hip_l, "kneeY=%.3f hipY=%.3f" % [y.knee_l, y.hip_l])
	_check(title, "右膝在髋下", y.knee_r < y.hip_r, "kneeY=%.3f hipY=%.3f" % [y.knee_r, y.hip_r])

func _case_reset(rt, skel: Skeleton3D, title: String, lm: PackedVector3Array, conf: PackedFloat32Array) -> void:
	var rest := _bone_y(skel)
	for i in range(180):
		rt.drive(lm, conf, 0.033)
	var y := _bone_y(skel)
	# 复位路径: 脚应回到 rest 附近且在髋下 (旧版复位 IDENTITY → 脚翻到髋上方)
	_check(title, "左脚回 rest 且在髋下", y.foot_l < y.hip_l and absf(y.foot_l - rest.foot_l) < TOL,
		"footY=%.3f hipY=%.3f restFootY=%.3f" % [y.foot_l, y.hip_l, rest.foot_l])
	_check(title, "右脚回 rest 且在髋下", y.foot_r < y.hip_r and absf(y.foot_r - rest.foot_r) < TOL,
		"footY=%.3f hipY=%.3f restFootY=%.3f" % [y.foot_r, y.hip_r, rest.foot_r])

func _bone_y(skel: Skeleton3D) -> Dictionary:
	var gp := func(n: String) -> float:
		var i := skel.find_bone(n)
		return skel.get_bone_global_pose(i).origin.y if i >= 0 else NAN
	return {
		"hip_l": gp.call("LeftUpLeg"), "hip_r": gp.call("RightUpLeg"),
		"knee_l": gp.call("LeftLeg"),  "knee_r": gp.call("RightLeg"),
		"foot_l": gp.call("LeftFoot"), "foot_r": gp.call("RightFoot"),
	}

func _check(title: String, what: String, ok: bool, detail: String) -> void:
	print("  [%s] %s: %s | %s" % [title, what, "OK" if ok else "FAIL", detail])
	if not ok:
		_fail += 1

func _diag(rt, skel: Skeleton3D, tag: String) -> void:
	var n_valid: int = rt._valid.size()
	var axis: Vector3 = rt._axis_dir.get("LeftUpLeg", Vector3(NAN, NAN, NAN))
	var rest_q: Quaternion = skel.get_bone_rest(skel.find_bone("LeftUpLeg")).basis.orthonormalized().get_rotation_quaternion()
	print("  [%s] diag: has_skel=%s valid=%d axisUpLeg=%v restQ(upleg)=%s (%.1f°)" % [
		tag, rt.has_skeleton(), n_valid, axis, rest_q,
		rest_q.get_angle() * 180.0 / PI])

func _diag_pose(skel: Skeleton3D, tag: String) -> void:
	var i := skel.find_bone("LeftUpLeg")
	var pq := skel.get_bone_pose_rotation(i)
	var rq := skel.get_bone_rest(i).basis.orthonormalized().get_rotation_quaternion()
	print("  [%s] diag: poseQ=%s restQ=%s pose≈rest? %s" % [tag, pq, rq, pq.is_equal_approx(rq)])

# ── 合成 landmark (raw: [0,1], y 向下, 同 body.cpp 输出) ──
func _lm_full_body() -> PackedVector3Array:
	var pts := {
		0: Vector2(0.50, 0.06),   # 鼻
		7: Vector2(0.44, 0.07), 8: Vector2(0.56, 0.07),  # 耳
		11: Vector2(0.40, 0.22), 12: Vector2(0.60, 0.22),  # 肩
		13: Vector2(0.36, 0.42), 14: Vector2(0.64, 0.42),  # 肘
		15: Vector2(0.34, 0.60), 16: Vector2(0.66, 0.60),  # 腕 (手臂下垂)
		23: Vector2(0.44, 0.58), 24: Vector2(0.56, 0.58),  # 髋 (画面内, <0.76)
		25: Vector2(0.44, 0.76), 26: Vector2(0.56, 0.76),  # 膝
		27: Vector2(0.44, 0.92), 28: Vector2(0.56, 0.92),  # 踝
	}
	return _pack(pts)

func _lm_half_body() -> PackedVector3Array:
	var pts := {
		0: Vector2(0.50, 0.08),
		7: Vector2(0.44, 0.09), 8: Vector2(0.56, 0.09),
		11: Vector2(0.38, 0.28), 12: Vector2(0.62, 0.28),
		13: Vector2(0.34, 0.50), 14: Vector2(0.66, 0.50),
		15: Vector2(0.32, 0.70), 16: Vector2(0.68, 0.70),
		23: Vector2(0.44, 0.92), 24: Vector2(0.56, 0.92),  # 髋出画 (>0.85)
		25: Vector2(0.44, 0.10), 26: Vector2(0.56, 0.10),  # 膝踝: 垃圾外推值
		27: Vector2(0.44, 0.05), 28: Vector2(0.56, 0.05),
	}
	return _pack(pts)

func _pack(pts: Dictionary) -> PackedVector3Array:
	var lm := PackedVector3Array()
	lm.resize(33)
	for k in pts:
		lm[k] = Vector3(pts[k].x, pts[k].y, 0.0)
	return lm

func _conf_uniform(v: float) -> PackedFloat32Array:
	var c := PackedFloat32Array()
	c.resize(33)
	c.fill(v)
	return c

func _conf_half_body() -> PackedFloat32Array:
	var c := _conf_uniform(0.6)
	for i in [23, 24, 25, 26, 27, 28]:  # 髋/膝/踝: 外推垃圾点
		c[i] = 0.001
	return c

func _conf_arms_low() -> PackedFloat32Array:
	# 肩/肘/腕低置信 (近景视频常见) → 手臂走自然下垂默认姿态
	var c := _conf_uniform(0.6)
	for i in [11, 12, 13, 14, 15, 16]:
		c[i] = 0.001
	return c
