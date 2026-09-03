extends RefCounted
## VRM 表情绑定解析 (.vrm = glTF + VRM 扩展): 把 VRM 表情组按索引绑定归一到 canonical
## ARKit52 驱动 (比 morph 名字扫描可靠 — VRM morph 名因导出器而异)。
## kind: "arkit" = VRM 1.0 custom 带 ARKit 名 (52 全量直驱); "preset" = 官方 preset 近似
## (jawOpen→aa, mouthFunnel→oh, mouthPucker→ou, eyeBlink→blinkLeft/Right, 眼动→look* 组合);
## "" = 非 VRM (走 morph 名扫描)。emotion_binds 提供情绪保底 (happy/angry/sad/surprised/
## relaxed→preset), humanoid 提供角色名→骨骼名 (retarget 骨骼匹配用)。
## 注: Godot 的 GLTFState.get_scene_node 不映射 mesh 节点 → mesh 解析走「节点名→MeshInstance3D」
## (Godot 导入后 MeshInstance3D 名 = glTF 节点名; VRM 1.0 规范要求节点名唯一)。

const Arkit52 := preload("res://src/avatar/arkit52.gd")

var kind := ""
var binds52: Array = []        # 52 项, 每项 Array[{mesh: MeshInstance3D, bi: int, weight: float}]
var emotion_binds := {}        # 情绪名 → {binds: Array, w: float} (按 emotion_layer 时间轴权重直写)
var extra := {}                # 52 空间外组合: {lookLeft: {binds, src: [canonical 索引]}} 取 max
var humanoid := {}             # VRM humanoid 角色 → Godot 节点名 (骨骼)
var _bind_count := 0
var _nodes: Array = []         # glTF nodes 原始数组
var _mi_by_name := {}          # 场景 MeshInstance3D 名 → 节点
var _mesh_names := {}          # glTF mesh 索引 → 挂载节点名数组 (0.x bind 无 node 字段时反查)

# VRM preset → canonical 近似 (只绑强相关: 口型开合/眨眼; 弱近似如 ee/ih 不绑)
# VRM 0.x preset 同表小写 (A/aa, Blink/blink, Joy/happy...), 查询时小写化

func parse(json: Dictionary, scene_nodes: Dictionary, root: Node) -> void:
	binds52.resize(52)
	for i in 52:
		if binds52[i] == null:
			binds52[i] = []   # resize 填的是 null, 统一为空数组供迭代
	for mi in root.find_children("*", "MeshInstance3D", true, false):
		if not _mi_by_name.has(String(mi.name)):
			_mi_by_name[String(mi.name)] = mi
	var ext: Dictionary = json.get("extensions", {})
	_nodes = json.get("nodes", [])
	for i in _nodes.size():
		if _nodes[i].has("mesh"):
			var mid := int(_nodes[i]["mesh"])
			if not _mesh_names.has(mid):
				_mesh_names[mid] = []
			_mesh_names[mid].append(String(_nodes[i].get("name", "")))
	var v1: Dictionary = ext.get("VRMC_vrm", {})
	if not v1.is_empty():
		_parse_humanoid_v1(v1, scene_nodes)
		_parse_vrm1(v1)
		return
	var v0: Dictionary = ext.get("VRM", {})
	if not v0.is_empty():
		_parse_humanoid_v0(v0, scene_nodes)
		_parse_vrm0(v0)

func is_vrm() -> bool:
	return kind != ""

func bind_count() -> int:
	return _bind_count

func info() -> Dictionary:
	return {kind = kind, binds = _bind_count, emotions = emotion_binds.size(),
		look = extra.size(), humanoid = humanoid.size()}

# ========== VRM 1.0 (VRMC_vrm) ==========

func _parse_vrm1(v1: Dictionary) -> void:
	var ex: Dictionary = v1.get("expressions", {})
	var pres: Dictionary = ex.get("preset", {})
	var cust: Dictionary = ex.get("custom", {})
	# ARKit 52 自定义表情 → 全量直驱
	var arkit := 0
	for nm in cust:
		var ai := Arkit52.index_of(String(nm))
		if ai > 0 and binds52[ai].is_empty():
			binds52[ai] = _binds(cust[nm], 1.0)
			if not binds52[ai].is_empty():
				_bind_count += 1
				arkit += 1
	if arkit >= 40:
		kind = "arkit"
		return
	kind = "preset"
	# 官方 preset 近似 (口型开合/眨眼主通道)
	_bind_canonical("jawOpen", pres.get("aa"))
	_bind_canonical("mouthFunnel", pres.get("oh"))
	_bind_canonical("mouthPucker", pres.get("ou"))
	_bind_canonical("eyeBlinkLeft", _first(pres, "blinkLeft", "blink"))
	_bind_canonical("eyeBlinkRight", _first(pres, "blinkRight", "blink"))
	# 眼动: 8 路 eyeLook → 4 组 look preset (每组取两路 canonical 的 max)
	_extra_look(pres)
	# 情绪保底
	_emotion("happy", pres.get("happy"), 1.0)
	_emotion("angry", pres.get("angry"), 1.0)
	_emotion("sad", pres.get("sad"), 1.0)
	_emotion("surprised", pres.get("surprised"), 1.0)
	_emotion("relaxed", pres.get("relaxed"), 1.0)
	_emotion("awkward", pres.get("relaxed"), 0.5)

func _parse_humanoid_v1(v1: Dictionary, scene_nodes: Dictionary) -> void:
	var bones: Dictionary = v1.get("humanoid", {}).get("humanBones", {})
	for role in bones:
		var ni := int(bones[role].get("node", -1))
		if ni >= 0 and scene_nodes.has(ni):
			humanoid[String(role)] = String(scene_nodes[ni].name)

# ========== VRM 0.x (VRM) ==========

func _parse_vrm0(v0: Dictionary) -> void:
	var groups: Array = v0.get("blendShapeMaster", {}).get("blendShapeGroups", [])
	var by_lc := {}   # presetName/name 小写 → 组
	var by_raw := {}  # 原始名 → 组 (自定义组名可能是 ARKit 名)
	for g in groups:
		var raw := String(g.get("presetName", ""))
		if raw == "unknown" or raw.is_empty():
			raw = String(g.get("name", ""))
		if raw.is_empty():
			continue
		by_lc[raw.to_lower()] = g
		by_raw[raw] = g
	# ARKit 自定义组 → 全量直驱 (0.x weight 是 0~100)
	var arkit := 0
	for raw in by_raw:
		var ai := Arkit52.index_of(raw)
		if ai > 0 and binds52[ai].is_empty():
			binds52[ai] = _binds(by_raw[raw], 0.01)
			if not binds52[ai].is_empty():
				_bind_count += 1
				arkit += 1
	if arkit >= 40:
		kind = "arkit"
		return
	kind = "preset"
	_bind_canonical("jawOpen", by_lc.get("a"), 0.01)
	_bind_canonical("mouthFunnel", by_lc.get("o"), 0.01)
	_bind_canonical("mouthPucker", by_lc.get("u"), 0.01)
	_bind_canonical("eyeBlinkLeft", _first(by_lc, "blink_l", "blink"), 0.01)
	_bind_canonical("eyeBlinkRight", _first(by_lc, "blink_r", "blink"), 0.01)
	_extra_look(by_lc, 0.01)
	_emotion("happy", by_lc.get("joy"), 1.0, 0.01)
	_emotion("angry", by_lc.get("angry"), 1.0, 0.01)
	_emotion("sad", by_lc.get("sorrow"), 1.0, 0.01)
	_emotion("relaxed", by_lc.get("fun"), 1.0, 0.01)
	_emotion("awkward", by_lc.get("fun"), 0.5, 0.01)

func _parse_humanoid_v0(v0: Dictionary, scene_nodes: Dictionary) -> void:
	var bones: Array = v0.get("humanoid", {}).get("humanBones", [])
	for b in bones:
		var ni := int(b.get("node", -1))
		if ni >= 0 and scene_nodes.has(ni):
			humanoid[String(b.get("bone", ""))] = String(scene_nodes[ni].name)

# ========== 绑定解析 ==========

# expression/组条目 → [{mesh: MeshInstance3D, bi, weight}]; wscale: 1.0(1.0) / 0.01(0.x 百分制)
func _binds(entry: Variant, wscale: float) -> Array:
	var out := []
	if entry == null or not (entry is Dictionary):
		return out
	var mb: Array = entry.get("morphTargetBinds", entry.get("binds", []))
	for b in mb:
		for mi in _resolve_mesh(b):
			out.append({mesh = mi, bi = int(b.get("index", 0)),
				weight = clampf(float(b.get("weight", 1.0)) * wscale, 0.0, 1.0)})
	return out

# bind 的 mesh 节点定位: 优先 bind.node 的节点名, 回退 glTF mesh 索引反查挂载节点名
func _resolve_mesh(b: Dictionary) -> Array:
	var out := []
	var ni := int(b.get("node", -1))
	if ni >= 0 and ni < _nodes.size():
		var nmi = _mi_by_name.get(String(_nodes[ni].get("name", "")))
		if nmi != null:
			out.append(nmi)
			return out
	for nm in _mesh_names.get(int(b.get("mesh", -1)), []):
		var nmi2 = _mi_by_name.get(nm)
		if nmi2 != null:
			out.append(nmi2)
	return out

func _bind_canonical(canonical: String, entry: Variant, wscale: float = 1.0) -> void:
	var ai := Arkit52.index_of(canonical)
	if ai <= 0:
		return
	var b := _binds(entry, wscale)
	if not b.is_empty():
		binds52[ai] = b
		_bind_count += 1

# 眼动: 双眼各两路 canonical 取 max 合成 → lookLeft/lookRight/lookUp/lookDown preset
func _extra_look(pres: Dictionary, wscale: float = 1.0) -> void:
	var pairs := {
		"lookLeft": ["eyeLookOutLeft", "eyeLookInRight"],
		"lookRight": ["eyeLookInLeft", "eyeLookOutRight"],
		"lookUp": ["eyeLookUpLeft", "eyeLookUpRight"],
		"lookDown": ["eyeLookDownLeft", "eyeLookDownRight"],
	}
	for key in pairs:
		var entry: Variant = pres.get(key)
		if entry == null or not (entry is Dictionary):
			continue
		var b := _binds(entry, wscale)
		if b.is_empty():
			continue
		var src := []
		for nm in pairs[key]:
			src.append(Arkit52.index_of(nm))
		extra[key] = {binds = b, src = src}

# 情绪保底: 情绪名 → preset 组 (view 按 emotion_layer 时间轴权重直写)
func _emotion(emo: String, entry: Variant, w: float, wscale: float = 1.0) -> void:
	if entry == null or not (entry is Dictionary):
		return
	var b := _binds(entry, wscale)
	if not b.is_empty():
		emotion_binds[emo] = {binds = b, w = w}

func _first(dict: Dictionary, k1: String, k2: String) -> Variant:
	if dict.has(k1):
		return dict[k1]
	return dict.get(k2)
