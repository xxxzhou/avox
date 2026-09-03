extends SubViewportContainer
## 可复用 3D avatar 视图 (avatar/agent 场景共用): SubViewport + 轨道相机 + 三灯环境 +
## 模型加载 (GLB/GLTF, ARKit 名 blendshape 扫描) + AvatarDriver 全链合成
## (采集 base + 程序化眨眼/视线 + emotion_layer 情绪), 内部 60fps compose 写 mesh。
## 用法: add_child 后 load_model(path); 采集源 set_base(owner, arr52); 情绪 apply_emotion(name, intensity);
## 身体 retargeting 由场景侧做 (get_skeleton()/get_avatar_root() 取骨骼)。

signal model_loaded(ok: bool, info: String)

const Arkit52 := preload("res://src/avatar/arkit52.gd")
const AvatarDriver := preload("res://src/avatar/avatar_driver.gd")
const AutoBlink := preload("res://src/avatar/auto_blink.gd")
const IdleGaze := preload("res://src/avatar/idle_gaze.gd")
const EmotionLayer := preload("res://src/avatar/emotion_layer.gd")
const VRMMap := preload("res://src/avatar/vrm_map.gd")
const IdleMotion := preload("res://src/avatar/idle_motion.gd")
const IDLE_CURVES := "res://src/avatar/idle_bake.json"

var _viewport: SubViewport
var _camera: Camera3D
var _avatar_root: Node3D
var _driver: RefCounted
var _blink: RefCounted
var _gaze: RefCounted
var _emotion: RefCounted
var _vrm              # VRMMap 实例 (VRM 模型索引绑定); null = 非 VRM, 走 morph 名扫描
var _idle             # IdleMotion 待机微动 (M4); load_model 后绑骨架
var _skel: Skeleton3D
# 驱动表: 每个 mesh 一项 {mesh, pairs:[{ai, bi}]}; ai=canonical 索引, bi=该 mesh blendshape 索引
var _drv: Array = []
var _matched := 0
var _loaded := false
var _orbit_target := Vector3.ZERO
var _orbit_yaw := 0.0
var _orbit_pitch := 0.0
var _orbit_dist := 1.5
var _dragging := false

func _ready() -> void:
	stretch = true
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	_driver = AvatarDriver.new()
	_blink = AutoBlink.new()
	_gaze = IdleGaze.new()
	_emotion = EmotionLayer.new()
	_idle = IdleMotion.new()
	_idle.load_curves(IDLE_CURVES)
	_viewport = SubViewport.new()
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.own_world_3d = true   # 与宿主场景世界隔离 (嵌入对话场景时不互相干扰)
	add_child(_viewport)
	_camera = Camera3D.new()
	_camera.current = true
	_camera.fov = 35.0
	_viewport.add_child(_camera)
	# 主光 + rim 逆光 (蓝白, 从模型背后勾轮廓) + 补光: 头发/肩线与深底分离
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35, 40, 0)
	light.light_energy = 1.1
	_viewport.add_child(light)
	var rim := DirectionalLight3D.new()
	rim.rotation_degrees = Vector3(25, 200, 0)
	rim.light_energy = 0.9
	rim.light_color = Color(0.72, 0.82, 1.0)
	_viewport.add_child(rim)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-10, -60, 0)
	fill.light_energy = 0.35
	_viewport.add_child(fill)
	# 品牌深底 (Environment BG; transparent 会透出默认灰)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("#0B0F16")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.52, 0.6)
	env.ambient_light_energy = 0.6
	var we := WorldEnvironment.new()
	we.environment = env
	_viewport.add_child(we)

func _process(delta: float) -> void:
	if not _loaded:
		return
	# 合成链: 程序化眨眼/视线 + 情绪 + 采集 base → 单帧 52 维 → 写 mesh
	_driver.set_source("blink", _driver.CH_BLINK, _blink.to_blendshape(_blink.tick(delta)))
	_driver.set_source("gaze", _driver.CH_GAZE, _gaze.tick(delta))
	_driver.set_source("emotion", _driver.CH_EMOTION, _emotion.tick(delta))
	_driver.tick(delta)
	var final: PackedFloat32Array = _driver.compose()
	if _vrm != null:
		_apply_vrm(final)
	else:
		for d in _drv:
			var mi: MeshInstance3D = d.mesh
			if not is_instance_valid(mi):
				continue
			for p in d.pairs:
				mi.set_blend_shape_value(p.bi, final[p.ai])
	# 待机微动 (躯干骨骼; 与 retarget 按骨段分工, 与 blendshape 通道无关)
	if _idle != null and _idle.active and _skel != null:
		_idle.tick(delta, _skel)

# VRM 路径: canonical 52 → 索引绑定直写; 附加 look 组合 (两路取 max) 与情绪 preset 保底
func _apply_vrm(final: PackedFloat32Array) -> void:
	for ai in 52:
		var v := final[ai]
		for b in _vrm.binds52[ai]:
			if is_instance_valid(b.mesh):
				b.mesh.set_blend_shape_value(b.bi, clampf(v * b.weight, 0.0, 1.0))
	for key in _vrm.extra:
		var e: Dictionary = _vrm.extra[key]
		var v := 0.0
		for si in e.src:
			if si >= 0:
				v = maxf(v, final[si])
		for b in e.binds:
			if is_instance_valid(b.mesh):
				b.mesh.set_blend_shape_value(b.bi, clampf(v * b.weight, 0.0, 1.0))
	if _vrm.emotion_binds.is_empty():
		return
	var en: String = _emotion.current_name()
	var ew: float = _emotion.current_weight() * _emotion.current_intensity()
	for key in _vrm.emotion_binds:
		var tgt: float = ew if key == en else 0.0
		var eb: Dictionary = _vrm.emotion_binds[key]
		for b in eb.binds:
			if is_instance_valid(b.mesh):
				b.mesh.set_blend_shape_value(b.bi, clampf(tgt * eb.w * b.weight, 0.0, 1.0))

# ========== 数据源 ==========

# 采集帧进基础层 (独占 owner; 换 owner 自动 500ms 交叉淡入)
func set_base(owner: String, arr: PackedFloat32Array) -> void:
	_driver.claim_base(owner)
	_driver.set_source(owner, _driver.CH_BASE, arr)

# 离散情绪 (emotion_layer: 缓入→3s 保持→自动回落)
func apply_emotion(emo: String, intensity: float = 0.7) -> void:
	_emotion.apply(emo, intensity)

# 轨道相机距离 (小 = 拉近)
func set_orbit_dist(d: float) -> void:
	_orbit_dist = maxf(d, 0.05)
	_update_camera_transform()

# ========== 模型 ==========

func load_model(path: String) -> bool:
	if _avatar_root:
		_avatar_root.queue_free()
		_avatar_root = null
	_drv.clear()
	_matched = 0
	_loaded = false
	_vrm = null
	var gltf := GLTFDocument.new()
	var state := GLTFState.new()
	var err := gltf.append_from_file(path, state)
	if err == OK:
		var scene := gltf.generate_scene(state)
		if scene is Node3D:
			_avatar_root = scene
			_viewport.add_child(scene)
			_loaded = true
			# VRM 扩展 → 按索引绑定驱动 (morph 名因导出器而异, 名字扫描不可靠); 其余走名字扫描
			var vrm := VRMMap.new()
			var js: Dictionary = state.get_json()
			var snodes := {}
			if not js.is_empty():
				var gnodes: Array = js.get("nodes", [])
				for i in gnodes.size():
					var n: Node = state.get_scene_node(i)
					if n != null:
						snodes[i] = n
			vrm.parse(js, snodes, scene)
			if vrm.is_vrm():
				_vrm = vrm
				_matched = vrm.bind_count()
			else:
				_scan_blendshapes()
			_frame_camera()
			_bind_idle()
	var info: String
	if _loaded:
		info = "已加载: %d 个 mesh, 匹配 %d/52 blendshape" % [_drv.size() if _vrm == null else 0, _matched]
	else:
		info = "加载模型失败 (%d): %s" % [err, path]
	model_loaded.emit(_loaded, info)
	return _loaded

func is_loaded() -> bool:
	return _loaded

func mesh_count() -> int:
	return _drv.size()

func matched_count() -> int:
	return _matched

func get_avatar_root() -> Node3D:
	return _avatar_root

# 第一个 Skeleton3D (身体 retargeting 用)
func get_skeleton() -> Skeleton3D:
	if _avatar_root == null:
		return null
	var skeletons := _avatar_root.find_children("*", "Skeleton3D", true, false)
	if skeletons.is_empty():
		return null
	return skeletons[0] as Skeleton3D

# VRM 解析结果 (kind/binds/emotions/look/humanoid; 非 VRM kind="")
func vrm_info() -> Dictionary:
	return _vrm.info() if _vrm != null else {kind = "", binds = 0, emotions = 0, look = 0, humanoid = 0}

# VRM humanoid 角色 → 骨骼名 (非 VRM / 无 humanoid = 空); 供 retarget.gd 名字重映射
func vrm_humanoid() -> Dictionary:
	return _vrm.humanoid if _vrm != null else {}

# ========== 待机微动 (M4) ==========

# load_model 后绑骨架 (缓存 Skeleton3D, 供 _process 每帧 tick)
func _bind_idle() -> void:
	_skel = get_skeleton()
	if _idle != null and _idle.active:
		_idle.bind(_skel, vrm_humanoid())

# 待机微动实例 (场景可调 weight/amplitude; 如视频驱动 retarget 激活时压低)
func idle() -> RefCounted:
	return _idle

# 全部 blendshape 归零 (菜单「重置表情」)
func reset_blendshapes() -> void:
	for d in _drv:
		var mi: MeshInstance3D = d.mesh
		if is_instance_valid(mi):
			for p in d.pairs:
				mi.set_blend_shape_value(p.bi, 0.0)

# ========== 内部 ==========

# 扫所有 MeshInstance3D, 建 {canonical 名 → 该 mesh blendshape 索引} 映射表 _drv
func _scan_blendshapes() -> void:
	_drv.clear()
	_matched = 0
	if _avatar_root == null:
		return
	var names := Arkit52.names()
	var meshes := _avatar_root.find_children("*", "MeshInstance3D", true, false)
	for m in meshes:
		var mi := m as MeshInstance3D
		var mesh := mi.mesh as ArrayMesh
		if mesh == null:
			continue
		var cnt := mesh.get_blend_shape_count()
		if cnt == 0:
			continue
		# 该 mesh 的 blendshape 名 → 索引
		var name2bi := {}
		for bi in range(cnt):
			name2bi[String(mesh.get_blend_shape_name(bi))] = bi
		var pairs := []
		for ai in range(52):
			var nm := names[ai]
			if nm == "_neutral":
				continue
			if name2bi.has(nm):
				pairs.append({ai = ai, bi = name2bi[nm]})
				_matched += 1
		if not pairs.is_empty():
			_drv.append({mesh = mi, pairs = pairs})

# 框选相机: 优先框被驱动的脸部 mesh (有匹配 blendshape), 无匹配回退全模型
func _frame_camera() -> void:
	if _avatar_root == null:
		return
	var nodes: Array = []
	if not _drv.is_empty():
		for d in _drv:
			nodes.append(d.mesh)
	else:
		nodes = _avatar_root.find_children("*", "MeshInstance3D", true, false)
	var box: AABB = AABB()
	var first := true
	for mi in nodes:
		var mi3d := mi as MeshInstance3D
		if mi3d == null:
			continue
		var b: AABB = mi3d.global_transform * mi3d.get_aabb()
		if first:
			box = b
			first = false
		else:
			box = box.merge(b)
	if first or box.size == Vector3.ZERO:
		return
	# 按 size 归一相机距离 (脸部框选 1.8 留边; 头部远小于整身)
	_orbit_target = box.get_center()
	_orbit_dist = maxf(box.get_longest_axis_size() * 1.8, 0.5)
	_orbit_yaw = 0.0
	_orbit_pitch = 0.0
	_update_camera_transform()

func _update_camera_transform() -> void:
	if _camera == null:
		return
	# 球坐标求相机位置 (绕 _orbit_target), 再 look_at 朝向目标
	var cp := Vector3.ZERO
	cp.x = _orbit_target.x + _orbit_dist * sin(_orbit_yaw) * cos(_orbit_pitch)
	cp.y = _orbit_target.y + _orbit_dist * sin(_orbit_pitch)
	cp.z = _orbit_target.z + _orbit_dist * cos(_orbit_yaw) * cos(_orbit_pitch)
	_camera.global_position = cp
	_camera.look_at(_orbit_target)

# 输入: 右键拖拽旋转 + 滚轮缩放
func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		_dragging = event.pressed
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_WHEEL_UP:
		_orbit_dist = maxf(_orbit_dist * 0.9, 0.05)
		_update_camera_transform()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		_orbit_dist *= 1.1
		_update_camera_transform()
	elif event is InputEventMouseMotion and _dragging:
		_orbit_yaw -= event.relative.x * 0.01
		_orbit_pitch = clampf(_orbit_pitch + event.relative.y * 0.01, -1.2, 1.2)
		_update_camera_transform()
