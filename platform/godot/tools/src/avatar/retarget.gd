# retarget.gd — MediaPipe Pose 33 点 → avatar 全身骨骼 (retargeting)
#
# 输入: body_landmarks = 33×Vector3 (x,y 归一化[0,1], z=相对深度)
# 输出: 驱动 _skeleton 的骨骼 (Spine/Neck/Head/四肢)
#
# 方法:
#   - 躯干/颈/四肢: 方向对齐。以该骨骼长轴(rest, 全局)为基准, 用
#     用"向量对齐旋转"(手动实现, 兼容所有 Godot 4.x) 把长轴对齐到 landmark 骨段方向。
#     子骨 pose 旋转先扣除父骨当前世界旋转, 避免"链式叠加扭曲"。
#   - Head: yaw/roll 分解。人物正对镜头时, "肩→鼻"方向对齐会把"转头(yaw)"
#     错误表达成"歪头(roll)"(头部竖直长轴在 XY 平面倾斜), 故 Head 单独用
#     鼻子相对肩中心的横向偏移算 yaw(绕Y), 两耳连线倾角算 roll(绕Z)。
#
# 坐标系: 图像右 → +X, 图像上 → +Y (y 取负), z 为深度 (body33 当前恒 0)。
#         avatar 面向相机 (+Z), 与视频中人物正对镜头一致。
#
# 关键点:
#   1) y 镜像: 图像下(y增) → avatar -Y, 否则竖直骨段上下颠倒;
#   2) Hips 不驱动旋转 (左右髋连线在正对镜头时是水平线, 驱动会横转 90°);
#   3) Godot 4 的 pose 即相对父骨的完整局部旋转 (rest 不运行时叠加), 故
#      每骨局部 pose = 父world^-1 * (align * 本骨global_rest); 复位目标 = 本骨 rest 旋转;
#   4) 角度 clamp + 平滑, 防 landmark 抖动/预测错误时骨骼乱扭;
#   5) 置信度过滤: 每点带热图 argmax 峰值 conf, 起/终点置信度低 → 该骨段不驱动 (回默认姿态);
#      且沿骨架树向下传播: 父骨不可信 → 整条子树回默认 (子端可信也白搭, 骨段方向已不可靠);
#   6) 半身特写视频 (髋在画面底部外) 腿部 landmark 多为模型外推 → 腿整体回默认姿态 (几何迟滞判断)。
extends RefCounted

var skeleton: Skeleton3D = null
var avatar_root: Node3D = null   # avatar 根节点 (含蒙皮 MeshInstance3D), 供求骨骼解剖长轴; 可为 null → 回退骨骼原点法

# ── MediaPipe Pose 33 点索引 ──
const NOSE := 0
const LEFT_EAR := 7
const RIGHT_EAR := 8
const L_SHO := 11
const R_SHO := 12
const L_ELB := 13
const R_ELB := 14
const L_WRI := 15
const R_WRI := 16
const L_HIP := 23
const R_HIP := 24
const L_KNE := 25
const R_KNE := 26
const L_ANK := 27
const R_ANK := 28

const HEAD_BONE := "Head"

# Mixamo 风格名 → VRM humanoid 角色名 (bone_override 提供角色 → 实际骨骼名, 如 VRM 的 J_Bip_*)
const ROLE_BY_BONE := {
	"Spine2": "chest", "Neck": "neck", "Head": "head",
	"LeftArm": "leftUpperArm", "LeftForeArm": "leftLowerArm", "LeftHand": "leftHand",
	"RightArm": "rightUpperArm", "RightForeArm": "rightLowerArm", "RightHand": "rightHand",
	"LeftUpLeg": "leftUpperLeg", "LeftLeg": "leftLowerLeg", "LeftFoot": "leftFoot",
	"RightUpLeg": "rightUpperLeg", "RightLeg": "rightLowerLeg", "RightFoot": "rightFoot",
}

var _name_map := {}   # canonical 名 → 实际骨骼名 (VRM override; 空 = 名字即本名)

func _bname(n: String) -> String:
	return _name_map.get(n, n)

# ── 骨骼映射 (方向对齐类): bone -> {s: 起点lm, e: 终点lm, w: 权重, ang: 最大转角(rad), leg: 是否腿} ──
# 骨段方向 = _lm3d[e] - _lm3d[s], 语义 = 骨骼长轴 (父端→子端, 与 rest 长轴同义)。
var _targets: Array[Dictionary] = [
    # minc: 该骨驱动所需的最小 landmark 置信度 (spatial softmax 峰值)。
    #   躯干/颈: 低 (特写/半身视频里仍可靠); 四肢: 高 (近景/半身时肘腕踝多为模型外推,
    #   置信度 0.00~0.01 的垃圾 landmark 会把手臂驱动成平展、把腿驱动成折叠 → 强制保持 rest)。
    # 躯干: 髋→肩 (身体主轴, 向上); hip=true → 近景/半身时髋多为模型预测点, 自动压权
    { "bone": "Spine2",  "s": L_HIP, "e": L_SHO, "w": 0.55, "ang": 0.35, "leg": false, "hip": true, "minc": 0.002, "ok": 0.015 },
    # 颈: 肩→鼻, 只做轻微跟随 (头部主旋转由 Head 的 yaw/roll 承担)
    { "bone": "Neck",    "s": L_SHO, "e": NOSE,  "w": 0.18, "ang": 0.20, "leg": false, "minc": 0.002, "ok": 0.015 },
    # 左臂: 肩→肘(上臂), 肘→腕(前臂)。LeftShoulder 长轴过短(肩→肱骨头)不驱动, 肩关节旋转由 LeftArm 承担
    # ang: 绝对转角上限 (rad)。手脚抖动主要来自腕/踝 landmark 预测漂移, 收紧上限 + 帧间突变过滤 + 末梢降速三重抑制
    # arm=true: 无可靠 landmark 时手臂自然下垂 (非 A-pose 平展)
    { "bone": "LeftArm",       "s": L_SHO, "e": L_ELB, "w": 0.75, "ang": 1.15, "leg": false, "arm": true, "minc": 0.05, "ok": 0.15 },
    { "bone": "LeftForeArm",   "s": L_ELB, "e": L_WRI, "w": 0.70, "ang": 1.05, "leg": false, "end": true, "arm": true, "minc": 0.05, "ok": 0.15 },
    # 右臂
    { "bone": "RightArm",      "s": R_SHO, "e": R_ELB, "w": 0.75, "ang": 1.15, "leg": false, "arm": true, "minc": 0.05, "ok": 0.15 },
    { "bone": "RightForeArm",  "s": R_ELB, "e": R_WRI, "w": 0.70, "ang": 1.05, "leg": false, "end": true, "arm": true, "minc": 0.05, "ok": 0.15 },
    # 左腿 (腿 landmark 在近景/半身视频里极易外推, 提高 minc + 几何保护兜底, 防折叠)
    { "bone": "LeftUpLeg",  "s": L_HIP, "e": L_KNE, "w": 0.70, "ang": 0.90, "leg": true, "minc": 0.15, "ok": 0.3 },
    { "bone": "LeftLeg",    "s": L_KNE, "e": L_ANK, "w": 0.70, "ang": 0.80, "leg": true, "end": true, "minc": 0.15, "ok": 0.3 },
    # 右腿
    { "bone": "RightUpLeg", "s": R_HIP, "e": R_KNE, "w": 0.70, "ang": 0.90, "leg": true, "minc": 0.15, "ok": 0.3 },
    { "bone": "RightLeg",   "s": R_KNE, "e": R_ANK, "w": 0.70, "ang": 0.80, "leg": true, "end": true, "minc": 0.15, "ok": 0.3 },
]

# 每骨"长轴子骨"名 (用于取 rest 长轴方向 = 子骨 global_rest - 本骨 global_rest)
const _AXIS_CHILD := {
    "Spine2": "Neck", "Neck": "Head",
    "LeftArm": "LeftForeArm", "LeftForeArm": "LeftHand",
    "RightArm": "RightForeArm", "RightForeArm": "RightHand",
    "LeftUpLeg": "LeftLeg", "LeftLeg": "LeftFoot",
    "RightUpLeg": "RightLeg", "RightLeg": "RightFoot",
}

const _SPEED := 14.0   # 平滑速度: factor = w * delta * _SPEED (截断到 1)
const _END_SPEED_MUL := 0.40  # 末梢骨骼(手/脚直接父骨: 前臂/小腿)额外平滑降速系数, 防末端抖动被放大
const _DIR_JITTER_MAX := 0.50  # 帧间骨段方向突变角上限(rad, ≈29°): 单帧跳变超过即判定 landmark 预测漂移 → 权重压到几乎不动
const _LEG_HIDE := 0.85  # 髋 y 超过此值(画面底部外) → 腿不可见: 不应用 landmark 外推值, 回到默认姿态
const _LEG_SHOW := 0.76  # 髋 y 低于此值 → 腿恢复可见 (与 _LEG_HIDE 构成迟滞, 防髋在边界附近浮动时来回切换)

# ── 置信度过滤 (每点 conf = spatial softmax 后热图峰值 0~1; 阈值按实际模型响应可调) ──
# 实测: 检测到的关键点(头/肩/脊柱) conf≈0.01, 完全外推(画面外的腿) conf≈0.00 (≈1/4096)。
# 早期 _CONF_MIN=0.35 严重偏高 → 所有点(含检测到的)都被过滤, 导致 avatar 完全不动。
# 故按实际分布下调: 放行 conf≥0.002 的检测点, 过滤 conf≈0 的外推点。
const _CONF_MIN := 0.002  # 骨段置信度低于此值 → 不应用 (回默认姿态); 父骨不可信 → 子树继承此状态
const _CONF_OK := 0.015   # 置信度高于此值 → 全权重应用; 之间线性过渡, 防阈值附近抖动
# 父骨 → 子骨 传播链: 父骨不可信 → 子骨即使自身端点可信也不用 (骨段方向依赖父端 landmark)
const _CONF_PARENT := {
	"Neck": "Spine2",
	"LeftForeArm": "LeftArm",
	"RightForeArm": "RightArm",
	"LeftLeg": "LeftUpLeg",
	"RightLeg": "RightUpLeg",
}

# Head yaw/roll 参数
const _HEAD_YAW_REF := 0.35   # 参考长度 (肩→鼻 归一化距离), 用于 yaw 归一
const _HEAD_YAW_GAIN := 1.2   # yaw 增益 (转头幅度放大)
const _HEAD_YAW_MAX := 1.1    # 最大 yaw (rad)
const _HEAD_ROLL_MAX := 0.8   # 最大 roll (rad)

# 缓存: 有效目标列表 / 骨骼索引 / rest 全局变换 / rest 长轴方向
var _valid: Array[Dictionary] = []
var _bone_idx := {}
var _rest_global := {}   # bone name -> Transform3D (global rest)
var _axis_dir := {}      # bone name -> Vector3 (rest 长轴, 全局方向)
var _relax := {}         # arm=true 骨: 无可靠 landmark 时的自然下垂目标 (局部 Quaternion); 腿/躯干复位到 rest 旋转
var _relax_world := {}   # arm=true 骨: 松弛后的世界旋转 (供子臂骨组合松弛目标用)
var _diag_count := 0     # [诊断] 腿折叠定位用, 验证后移除

# 当前帧 landmark 的 3D 点 (avatar 本地坐标)
var _lm3d: Array[Vector3] = []
# 当前帧各 landmark 的置信度 (33×float, spatial softmax 后热图峰值)
var _lm_conf: PackedFloat32Array = PackedFloat32Array()
# 当前帧每骨段置信度 (bone name -> float, 已含父骨传播: 子骨 ≤ 父骨)
var _bone_conf := {}
# 当前帧已驱动骨骼的世界旋转 (bone name -> Basis), 供子骨扣父骨旋转
var _world_rot := {}
var _legs_visible := true
# 上一帧各骨的 landmark 骨段方向 (bone name -> Vector3), 用于帧间突变过滤
var _last_dir := {}

func _init(skel: Skeleton3D, root: Node3D = null, bone_override: Dictionary = {}) -> void:
	skeleton = skel
	avatar_root = root
	# VRM 等非 Mixamo 命名骨架: 角色 → 实际骨骼名重映射 (vrm_map.humanoid 解析自 VRM 扩展)
	for mix in ROLE_BY_BONE:
		var role: String = ROLE_BY_BONE[mix]
		if bone_override.has(role):
			_name_map[mix] = String(bone_override[role])
	_build_cache()

func has_skeleton() -> bool:
	return skeleton != null and not _valid.is_empty()

func _build_cache() -> void:
	_valid.clear()
	_bone_idx.clear()
	_rest_global.clear()
	_axis_dir.clear()
	_relax.clear()
	_relax_world.clear()
	if skeleton == null:
		return
	for e in _targets:
		var bone_name: String = e["bone"]
		var idx := skeleton.find_bone(_bname(bone_name))
		if idx < 0:
			continue
		var child_name: String = _AXIS_CHILD.get(bone_name, "")
		var cidx := skeleton.find_bone(_bname(child_name)) if not child_name.is_empty() else -1
		if cidx < 0:
			continue
		var g_rest := skeleton.get_bone_global_rest(idx)
		# 解剖长轴 (父端→子端, 骨骼全局空间): 优先用蒙皮网格几何求 (对骨节点原点不在关节上的
		# 模型鲁棒, 如 Ready Player Me 的小腿/颈骨原点方向不可靠 → 会导致腿/头折叠);
		# 无网格数据时回退「子骨原点 - 本骨原点」。
		var axis: Vector3 = _mesh_axis(bone_name, child_name)
		var src := "mesh"
		if axis.length_squared() < 1e-8:
			axis = skeleton.get_bone_global_rest(cidx).origin - g_rest.origin
			src = "bone"
		if axis.length_squared() < 1e-8:
			continue
		_bone_idx[bone_name] = idx
		_rest_global[bone_name] = g_rest
		_axis_dir[bone_name] = axis
		_valid.append(e)
		if e.get("arm", false):
			# 预计算"自然下垂"目标: 手臂长轴对齐到 -Y (重力向下), 使 avatar 在无可靠 landmark 时
			# 手自然下垂 (而非 Ready Player Me A-pose 平展)。转成局部 pose quaternion 存 _relax。
			# 注意父子组合: 运行时上臂已先转到自己的下垂位, 前臂目标必须相对"上臂松弛后"的
			# 世界计算, 否则前臂继承上臂的对齐旋转 → 肘部弯折 (实测偏 37°)。
			# _targets 顺序保证上臂先于前臂处理。
			var relax_world := _safe_align(axis, Vector3(0, -1, 0), PI) * g_rest.basis.orthonormalized()
			var p_idx := skeleton.get_bone_parent(idx)
			var p_name := skeleton.get_bone_name(p_idx) if p_idx >= 0 else ""
			var pw: Basis = _relax_world.get(p_name, _parent_world(idx))
			var relax_local := pw.inverse() * relax_world
			_relax[bone_name] = relax_local.get_rotation_quaternion().normalized()
			_relax_world[bone_name] = relax_world
	# Head 单独驱动 (yaw/roll), 若骨骼存在也登记
	var hidx := skeleton.find_bone(_bname(HEAD_BONE))
	if hidx >= 0:
		_bone_idx[HEAD_BONE] = hidx
		_rest_global[HEAD_BONE] = skeleton.get_bone_global_rest(hidx)

# 用蒙皮网格顶点求 bone 的解剖长轴 (子骨蒙皮顶点质心 - 本骨蒙皮顶点质心, 骨架局部空间)。
# 网格顶点在 bind pose 的位置 = mesh 全局变换 * 顶点局部坐标; 再变换到骨架局部空间与
# get_bone_global_rest 一致。权重阈值取 0.3, 只统计对该骨影响显著的顶点 (多为绑定权重=1 的主骨)。
func _mesh_axis(bone_name: String, child_name: String) -> Vector3:
	if skeleton == null or avatar_root == null:
		return Vector3.ZERO
	var bone_idx := skeleton.find_bone(_bname(bone_name))
	var child_idx := skeleton.find_bone(_bname(child_name))
	if bone_idx < 0 or child_idx < 0:
		return Vector3.ZERO
	var to_skeleton := skeleton.global_transform.affine_inverse()
	var bone_pts: PackedVector3Array = PackedVector3Array()
	var child_pts: PackedVector3Array = PackedVector3Array()
	var meshes := avatar_root.find_children("*", "MeshInstance3D", true, false)
	for mi in meshes:
		var mii := mi as MeshInstance3D
		var mesh := mii.mesh as ArrayMesh
		if mesh == null:
			continue
		# 蒙皮骨索引 → 骨架骨索引。最可靠是经 Skin 的 bind 名反查 (不依赖索引是否 1:1);
		# 无 Skin 时才假设 ARRAY_BONES 即骨架骨索引。
		var bone_to_skel := {}
		var skin: Skin = mii.skin
		if skin != null and skin.get_bind_count() > 0:
			var all_invalid := true
			for bi in range(skin.get_bind_count()):
				var si := skeleton.find_bone(skin.get_bind_name(bi))
				bone_to_skel[bi] = si
				if si >= 0:
					all_invalid = false
			if all_invalid:
				bone_to_skel.clear()  # bind_name 反查全失败 → 放弃用 skin, 走直接索引
		var mesh_to_skeleton := to_skeleton * mii.global_transform
		for s in range(mesh.get_surface_count()):
			var arrays := mesh.surface_get_arrays(s)
			var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			if verts.is_empty():
				continue
			var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
			var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
			if bones.size() != verts.size() * 4 or weights.size() != verts.size() * 4:
				continue
			for v in range(verts.size()):
				var wpos := mesh_to_skeleton * verts[v]
				for k in range(4):
					var b := bones[v * 4 + k]
					if weights[v * 4 + k] <= 0.3:
						continue
					var skel_b: int
					if bone_to_skel.is_empty():
						skel_b = b  # 无 skin 重映射, ARRAY_BONES 即骨架骨索引
					elif bone_to_skel.has(b):
						skel_b = bone_to_skel[b]
					else:
						continue
					if skel_b == bone_idx:
						bone_pts.append(wpos)
					elif skel_b == child_idx:
						child_pts.append(wpos)
	if bone_pts.is_empty() or child_pts.is_empty():
		return Vector3.ZERO
	var c_bone := Vector3.ZERO
	for p in bone_pts:
		c_bone += p
	c_bone /= bone_pts.size()
	var c_child := Vector3.ZERO
	for p in child_pts:
		c_child += p
	c_child /= child_pts.size()
	return c_child - c_bone

# ── 主驱动入口: 每帧调用 (lm/conf 来自 body_landmarks 信号) ──
func drive(lm: PackedVector3Array, conf: PackedFloat32Array, delta: float) -> void:
	if skeleton == null or _valid.is_empty() or lm.size() < 33:
		return
	_to_lm3d(lm, conf)
	_compute_conf(conf)
	# 半身保护: 髋在画面底部外 → 腿 landmark 多为模型外推, 不可信 → 腿回到默认姿态, 不应用这些值
	# 迟滞: 进入隐藏需 y>_LEG_HIDE, 恢复可见需 y<_LEG_SHOW, 防髋在边界附近浮动时来回切换
	var hip_bottom := maxf(lm[L_HIP].y, lm[R_HIP].y)
	if _legs_visible and hip_bottom > _LEG_HIDE:
		_legs_visible = false
	elif not _legs_visible and hip_bottom < _LEG_SHOW:
		_legs_visible = true
	_world_rot.clear()
	_diag_count += 1
	for e in _valid:
		_process_bone(e, delta)
	if _bone_idx.has(HEAD_BONE):
		_drive_head(delta)

# 图像坐标 → avatar 本地: 右→+X, 下→-Y (上→+Y), z 深度不变; 同时缓存每点置信度
func _to_lm3d(lm: PackedVector3Array, conf: PackedFloat32Array) -> void:
	_lm3d.resize(33)
	_lm_conf.resize(33)
	for i in range(33):
		var p := lm[i]
		_lm3d[i] = Vector3((p.x - 0.5) * 2.0, -(p.y - 0.5) * 2.0, p.z)
		_lm_conf[i] = conf[i] if i < conf.size() else 0.0

# 每骨段置信度 = min(起点, 终点) landmark 置信度; 再沿骨架树传播: 父骨不可信 → 子骨继承
func _compute_conf(conf: PackedFloat32Array) -> void:
	_bone_conf.clear()
	if conf.size() < 33:
		# 置信度缺失(异常帧/裁剪) → 视为不可信 (0): 全部回默认姿态。
		# 反向下场: 若置 1.0 (全信), 会用垃圾 landmark 驱动四肢 → 腿折叠/手乱摆。
		for e in _valid:
			_bone_conf[e["bone"]] = 0.0
		return
	for e in _valid:
		var c := minf(conf[e["s"]], conf[e["e"]])
		_bone_conf[e["bone"]] = c
	for child in _CONF_PARENT:
		var parent: String = _CONF_PARENT[child]
		if _bone_conf.has(parent):
			_bone_conf[child] = minf(_bone_conf[child], _bone_conf[parent])

func _process_bone(e: Dictionary, delta: float) -> void:
	var bone_name: String = e["bone"]
	var idx: int = _bone_idx[bone_name]
	# 不应用 landmark 值的情况 (骨骼平滑回默认姿态):
	#   1) 腿整体不可见 (髋在画面底部外, 腿 landmark 多为模型外推);
	#   2) 该骨段置信度不足 (起/终点 landmark 低可信, 或父骨不可信已传播到子树)。
	var bc: float = _bone_conf.get(bone_name, 1.0)
	var minc: float = e.get("minc", _CONF_MIN)  # 该骨驱动所需最小置信度 (躯干低, 四肢高)
	var ok: float = e.get("ok", _CONF_OK)       # 该骨置信度饱和值 (≥此值全权重)
	# 四肢几何有效性: 骨段应大致指向外/下 (父端→子端)。近景/半身视频里肘腕踝多为外推,
	# 常见错误是"反向"(腕在肘上、踝在膝上、膝在髋上) → 判定 landmark 无效, 复位默认姿态,
	# 避免手臂被驱动成平展、双腿被驱动成向上折叠。
	# 只对四肢生效: Spine2(髋→肩)/Neck(肩→鼻) 直立时骨段本来就朝上, 不能按此规则判无效。
	var geo_invalid := false
	if (e.get("leg", false) or e.get("arm", false)) and _lm3d.size() > maxi(e["s"], e["e"]):
		var dir_y := _lm3d[e["e"]].y - _lm3d[e["s"]].y
		if dir_y > 0.12:  # 骨段明显朝上 = 外推/折叠 (腿反向, 或手臂上抬异常)
			geo_invalid = true
	if (not _legs_visible and e.get("leg", false)) or geo_invalid or bc < minc:
		# 手臂 → 自然下垂 (_relax); 腿/躯干 → rest。末梢(前臂/小腿)额外降速平滑。
		_reset_bone_pose(idx, delta, e.get("end", false), _relax.get(bone_name, Quaternion()))
		# 复位是 slerp 渐变, 取复位后的实际 pose 旋转供子骨扣父
		_world_rot[bone_name] = _parent_world(idx) * Basis(skeleton.get_bone_pose_rotation(idx))
		if e.get("leg", false) and _diag_count % 30 == 0:
			# [诊断] 打印腿骨骼的"世界"pose: 本骨与脚骨 global 原点 Y (含所有祖先旋转)。
			# 站立时 脚Y < 髋Y (脚在下方, 差值<0); 折叠时 脚Y > 髋Y (差值>0)。
			var leg_gi := skeleton.find_bone(_bname(bone_name))
			var foot_gi := skeleton.find_bone(_bname("LeftFoot")) if bone_name.begins_with("Left") else skeleton.find_bone(_bname("RightFoot"))
			var hip_y := skeleton.get_bone_global_pose(leg_gi).origin.y
			var foot_y := -INF if foot_gi < 0 else skeleton.get_bone_global_pose(foot_gi).origin.y
			print("[leg-reset] %s bc=%.3f minc=%.3f geo=%s legvis=%s pose=%s footY-hipY=%.3f (hipY=%.3f footY=%s)"
			      % [bone_name, bc, minc, geo_invalid, _legs_visible, skeleton.get_bone_pose_rotation(idx),
			         foot_y - hip_y, hip_y, foot_y])
		return
	var dir: Vector3 = _lm3d[e["e"]] - _lm3d[e["s"]]
	if dir.length_squared() < 1e-8:
		return
	# rest 长轴 → landmark 骨段方向 的对齐旋转 (全局), 角度受 e["ang"] 限制
	var align := _safe_align(_axis_dir[bone_name], dir, e.get("ang", PI))
	var g_rest_basis: Basis = _rest_global[bone_name].basis.orthonormalized()
	# 父骨当前世界旋转 (未驱动 → rest 全局)
	var p_world := _parent_world(idx)
	# 期望本骨全局旋转 = align * 全局 rest; Godot 4 的局部 pose 即相对父骨的完整局部旋转
	# (rest 不运行时叠加), 故 局部pose = 父world^-1 * 期望全局
	var target_world := align * g_rest_basis
	var local := p_world.inverse() * target_world
	var lq := local.get_rotation_quaternion().normalized()
	# 权重 + 平滑。置信度过渡: minc~_CONF_OK 线性渐强 (低于 minc 已在开头 return), 防边界抖动
	# (腿已在函数开头处理: 不可见 → 复位默认姿态, 不走到这里)
	var w: float = e["w"] * clampf((bc - minc) / maxf(ok - minc, 1e-6), 0.0, 1.0)
	if not _legs_visible and e.get("hip", false):
		w *= 0.15
	# 帧间方向突变过滤: landmark 单帧跳变(遮挡/预测漂移, 手脚乱动的主要来源) → 权重压到几乎不动
	var last_dir: Vector3 = _last_dir.get(bone_name, dir)
	var ddot := clampf(last_dir.normalized().dot(dir.normalized()), -1.0, 1.0)
	if acos(ddot) > _DIR_JITTER_MAX:
		w *= 0.05
	_last_dir[bone_name] = dir
	# 末梢骨骼(手/脚直接父骨)额外降速: 前臂/小腿的抖动会被手腕/脚踝放大
	var speed := _SPEED
	if e.get("end", false):
		speed *= _END_SPEED_MUL
	var cur := skeleton.get_bone_pose_rotation(idx)
	var factor := clampf(w * delta * speed, 0.0, 1.0)
	var fq := cur.slerp(lq, factor)
	skeleton.set_bone_pose_rotation(idx, fq)
	# 更新本骨世界旋转 (供子骨扣父): 全局 pose = 父world * 本骨局部 pose
	_world_rot[bone_name] = p_world * Basis(fq)

# 骨骼平滑回到基准姿态 (local pose 旋转 → target), 用于不可见部位: 不应用 landmark 外推值。
# target 默认为本骨 rest 旋转 (Godot 4 的 pose 即相对父骨的完整局部旋转, rest 不会运行时叠加,
# 复位到 IDENTITY 会丢掉 rest 旋转 → Ready Player Me 腿骨 rest 自带 ~180° 翻转, 会整条腿朝上);
# 手臂传 _relax(自然下垂)。
func _reset_bone_pose(idx: int, delta: float, is_end: bool, target: Quaternion = Quaternion()) -> void:
	if target == Quaternion():
		target = skeleton.get_bone_rest(idx).basis.orthonormalized().get_rotation_quaternion().normalized()
	var cur := skeleton.get_bone_pose_rotation(idx)
	if cur.is_equal_approx(target):
		return
	var speed := _SPEED
	if is_end:
		speed *= _END_SPEED_MUL
	var factor := clampf(delta * speed, 0.0, 1.0)
	skeleton.set_bone_pose_rotation(idx, cur.slerp(target, factor))

# Head: yaw(绕Y, 转头) / roll(绕Z, 歪头) 分解驱动
func _drive_head(delta: float) -> void:
	var idx: int = _bone_idx[HEAD_BONE]
	# 头置信度 = min(鼻/双肩/双耳) landmark 置信度, 并传播 Neck/Spine2: 父骨不可信 → 头也不驱动
	var hc := 1.0
	if _lm_conf.size() >= 33:
		hc = minf(minf(_lm_conf[NOSE], minf(_lm_conf[L_SHO], _lm_conf[R_SHO])),
		          minf(_lm_conf[LEFT_EAR], _lm_conf[RIGHT_EAR]))
	hc = minf(hc, _bone_conf.get("Neck", 1.0))
	if hc < _CONF_MIN:
		_reset_bone_pose(idx, delta, true)
		_world_rot[HEAD_BONE] = _parent_world(idx) * Basis(skeleton.get_bone_pose_rotation(idx))
		return
	var nose: Vector3 = _lm3d[NOSE]
	var ms: Vector3 = (_lm3d[L_SHO] + _lm3d[R_SHO]) * 0.5
	var el: Vector3 = _lm3d[LEFT_EAR]
	var er: Vector3 = _lm3d[RIGHT_EAR]
	# yaw: 鼻相对肩中心线的横向偏移 (人物转头时鼻在画面内左右移动)
	var yaw := clampf(atan2(nose.x - ms.x, _HEAD_YAW_REF) * _HEAD_YAW_GAIN, -_HEAD_YAW_MAX, _HEAD_YAW_MAX)
	# roll: 两耳连线相对水平面的倾角 (歪头)
	var roll := clampf(atan2(el.y - er.y, er.x - el.x), -_HEAD_ROLL_MAX, _HEAD_ROLL_MAX)
	# 期望头骨全局旋转: 先绕Z歪头, 再绕Y转头 (叠加在 rest 全局旋转上)
	var g_rest_basis: Basis = _rest_global[HEAD_BONE].basis.orthonormalized()
	var target_world: Basis = Basis(Vector3.UP, yaw) * Basis(Vector3.BACK, roll) * g_rest_basis
	var p_world := _parent_world(idx)
	var local := p_world.inverse() * target_world
	var lq := local.get_rotation_quaternion().normalized()
	var cur := skeleton.get_bone_pose_rotation(idx)
	# 置信度过渡 (低可信渐弱, 同 _process_bone)
	var hw := 0.8 * clampf((hc - _CONF_MIN) / maxf(_CONF_OK - _CONF_MIN, 1e-6), 0.0, 1.0)
	var factor := clampf(hw * delta * _SPEED, 0.0, 1.0)
	var fq := cur.slerp(lq, factor)
	skeleton.set_bone_pose_rotation(idx, fq)
	_world_rot[HEAD_BONE] = p_world * Basis(fq)

# 父骨当前世界旋转 (未驱动 → 其 rest 全局旋转)
func _parent_world(idx: int) -> Basis:
	var p_idx := skeleton.get_bone_parent(idx)
	if p_idx < 0:
		return Basis.IDENTITY
	var p_name := skeleton.get_bone_name(p_idx)
	return _world_rot.get(p_name, skeleton.get_bone_global_rest(p_idx).basis.orthonormalized())

# rest 方向 a → 目标方向 b 的旋转 (角度限制到 max_ang, 防 landmark 抖动/预测错误时骨骼乱扭)
# 平行 → IDENTITY (回正); 反平行 → 转 180° (方向对齐, 不瞬间回弹 rest 姿态)
func _safe_align(a: Vector3, b: Vector3, max_ang: float = PI) -> Basis:
	var an := a.normalized()
	var bn := b.normalized()
	var d := an.dot(bn)
	if d > 0.9999:
		return Basis.IDENTITY
	if d < -0.9999:
		# 反平行: 取任意垂直于 a 的轴, 旋转 180°
		var orth := an.cross(Vector3.RIGHT)
		if orth.length_squared() < 1e-8:
			orth = an.cross(Vector3.UP)
		return Basis(orth.normalized(), PI)
	var axis := an.cross(bn).normalized()
	var angle := an.angle_to(bn)
	if angle > max_ang:
		angle = max_ang
	return Basis(axis, angle)
