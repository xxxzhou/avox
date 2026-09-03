extends RefCounted
## 待机微动作播放器 (M4): 采样 idle_bake.json 的躯干欧拉偏移曲线, 叠加在骨骼 rest 上。
## 只驱动 spine/mid 两根躯干骨 — 与 retarget (Spine2/Neck/Head/四肢) 天然不冲突;
## 口型/眨眼/视线是 blendshape 通道, 互不影响 (对应 AIRI MAGIC 的 skipMouthOpen 思路)。
## 数据: script/godot/idle_bake.py 离线 VAR 烘焙 (毫拉底整数), 循环末尾 crossfade 与开头混合。

const AMP_SCALE := 0.001   # JSON 存 millirad
const ROLE_BONES := {"spine": "Spine", "mid": "Spine1"}   # 默认 Mixamo 名; VRM 由 humanoid 覆盖

var _fps := 30.0
var _duration := 0.0
var _fade := 1.0
var _ch := []              # [{role, dof, data: PackedFloat32Array(rad)}]
var _bones := []           # [{idx, base: Quaternion(rest), chans: [{dof, slot}]}]
var _t := 0.0
var amplitude := 1.0       # 幅度倍率 (调试/场景调节)
var weight := 1.0          # 总权重 (0 = 完全回 rest)
var active := false

func load_curves(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var doc: Dictionary = JSON.parse_string(f.get_as_text())
	if doc == null or not doc.has("channels"):
		return false
	_fps = float(doc.get("fps", 30))
	_duration = float(doc.get("duration", 0))
	_fade = float(doc.get("crossfade", 1.0))
	_ch.clear()
	for c in doc["channels"]:
		var arr := PackedFloat32Array()
		arr.resize(c.data.size())
		for i in c.data.size():
			arr[i] = float(c.data[i]) * AMP_SCALE
		_ch.append({role = String(c.role), dof = int(c.dof), data = arr})
	active = _ch.size() > 0 and _duration > 0.0
	return active

# 绑骨架并缓存 rest。name_override = vrm_humanoid() (VRM 角色名 → 实际骨名; 非 VRM 传空)
func bind(skeleton: Skeleton3D, name_override: Dictionary = {}) -> void:
	_bones.clear()
	if skeleton == null or not active:
		return
	for role in ROLE_BONES:
		var bn: String = ROLE_BONES[role]
		if role == "spine":
			bn = name_override.get("spine", bn)
		else:
			# mid: VRM 取 upperChest→chest, 默认 Mixamo Spine1
			for cand in ["upperChest", "chest"]:
				if name_override.has(cand):
					bn = name_override[cand]
					break
		var idx := skeleton.find_bone(bn)
		if idx < 0:
			continue
		var chans := []
		for slot in _ch.size():
			if _ch[slot].role == role:
				chans.append({dof = _ch[slot].dof, slot = slot})
		if not chans.is_empty():
			_bones.append({idx = idx,
				base = skeleton.get_bone_rest(idx).basis.get_rotation_quaternion(),
				chans = chans})

# 每帧推进并应用 (骨骼姿态 = rest × 欧拉偏移; 小角度下轴序无关紧要)
func tick(delta: float, skeleton: Skeleton3D) -> void:
	if not active or _bones.is_empty() or skeleton == null:
		return
	_t = fmod(_t + delta, _duration)
	var k := weight * amplitude
	if k <= 0.0001:
		return
	for b in _bones:
		var ex := 0.0
		var ey := 0.0
		var ez := 0.0
		for c in b.chans:
			var v: float = _sample(_ch[c.slot].data) * k
			match c.dof:
				0: ex = v
				1: ey = v
				2: ez = v
		var off := Quaternion(Vector3.UP, ey) * Quaternion(Vector3.RIGHT, ex) * Quaternion(Vector3.BACK, ez)
		skeleton.set_bone_pose_rotation(b.idx, b.base * off)

# 曲线采样 (s 内插) + 循环接缝 crossfade (末尾 _fade 秒与开头混合)
func _sample(data: PackedFloat32Array) -> float:
	var n := data.size()
	var v: float = _raw(data, _t / _duration * n)
	if _t >= _duration - _fade:
		var f: float = clampf((_t - (_duration - _fade)) / _fade, 0.0, 1.0)
		var v2: float = _raw(data, f * _fade / _duration * n)
		v = lerpf(v, v2, f)
	return v

func _raw(data: PackedFloat32Array, ft: float) -> float:
	var n := data.size()
	var i := int(ft) % n
	return lerpf(data[i], data[(i + 1) % n], ft - floor(ft))
