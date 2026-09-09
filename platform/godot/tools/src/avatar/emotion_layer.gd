extends RefCounted
## 离散情绪应用层: 情绪名+强度 → ARKit52 组合, 缓入→保持→自动回落 (对齐 AIRI
## setEmotionWithResetAfter(3000ms) + easeInOutCubic, stage-ui-three expression.ts)。
## tick 返回 52 维写 AvatarDriver emotion 通道 (加法叠加, 与采集 base/眨眼/视线共存)。
## 情绪枚举对齐 AIRI llm-streaming-control payloads.ts (9 值), 另收 "thinking" 等旧写法别名。

const Arkit52 := preload("res://src/avatar/arkit52.gd")

const RISE_MS := 300.0   # 缓入时长
const HOLD_MS := 3000.0  # 保持时长 (过后自动回落)
const FALL_MS := 500.0   # 回落时长

# 情绪 → ARKit52 blendshape 组合 (权重 0..1, 乘 intensity 后进 emotion 通道)
const PRESETS := {
	"happy": {"mouthSmileLeft": 0.9, "mouthSmileRight": 0.9, "cheekSquintLeft": 0.5,
		"cheekSquintRight": 0.5, "browOuterUpLeft": 0.25, "browOuterUpRight": 0.25},
	"sad": {"mouthFrownLeft": 0.7, "mouthFrownRight": 0.7, "browInnerUp": 0.55,
		"mouthLowerDownLeft": 0.2, "mouthLowerDownRight": 0.2, "eyeSquintLeft": 0.2, "eyeSquintRight": 0.2},
	"angry": {"browDownLeft": 0.85, "browDownRight": 0.85, "eyeSquintLeft": 0.5,
		"eyeSquintRight": 0.5, "mouthPressLeft": 0.45, "mouthPressRight": 0.45,
		"noseSneerLeft": 0.3, "noseSneerRight": 0.3},
	"think": {"browDownLeft": 0.35, "browDownRight": 0.3, "eyeSquintLeft": 0.25,
		"eyeSquintRight": 0.25, "mouthPressLeft": 0.3, "mouthPressRight": 0.3, "browInnerUp": 0.2},
	"surprised": {"browInnerUp": 0.9, "eyeWideLeft": 0.9, "eyeWideRight": 0.9,
		"jawOpen": 0.35, "mouthFunnel": 0.15},
	"awkward": {"mouthPressLeft": 0.5, "mouthPressRight": 0.5, "cheekSquintLeft": 0.35,
		"cheekSquintRight": 0.35, "eyeSquintLeft": 0.3, "eyeSquintRight": 0.3, "browInnerUp": 0.25},
	"question": {"browOuterUpLeft": 0.8, "browOuterUpRight": 0.3, "eyeWideLeft": 0.35,
		"eyeWideRight": 0.35, "mouthLeft": 0.2},
	"curious": {"browInnerUp": 0.5, "eyeWideLeft": 0.45, "eyeWideRight": 0.45,
		"jawOpen": 0.15, "mouthFunnel": 0.15},
	"neutral": {},
}
# 旧写法/别名 → 表键
const ALIASES := {"thinking": "think"}

var _weights := {}     # canonical 索引 → 权重 (当前情绪 × intensity 展开)
var _name := "neutral"
var _t := 0.0
var _active := false
var _intensity := 1.0
var _last_w := 0.0     # tick 后的时间轴权重 (VRM preset 情绪通道直写用)

# 触发/切换情绪 (同名重触发 = 刷新强度与计时)
func apply(emo: String, intensity: float = 0.7) -> void:
	_name = emo.to_lower().strip_edges()
	if ALIASES.has(_name):
		_name = ALIASES[_name]
	if not PRESETS.has(_name):
		_name = "neutral"
	_intensity = clampf(intensity, 0.0, 1.0)
	var inten := clampf(intensity, 0.0, 1.0)
	_weights.clear()
	for nm in PRESETS[_name]:
		var idx := Arkit52.index_of(nm)
		if idx >= 0:
			_weights[idx] = float(PRESETS[_name][nm]) * inten
	_t = 0.0
	_active = true

func current_name() -> String:
	return _name

func current_weight() -> float:
	return _last_w

func current_intensity() -> float:
	return _intensity

# 每帧推进, 返回当前情绪的 52 维叠加值 (未激活/已回落 = 全零)
func tick(delta: float) -> PackedFloat32Array:
	var arr := PackedFloat32Array()
	arr.resize(52)
	if not _active:
		_last_w = 0.0
		return arr
	_t += delta * 1000.0
	var w := 1.0
	if _t < RISE_MS:
		w = _ease_io(_t / RISE_MS)
	elif _t < RISE_MS + HOLD_MS:
		w = 1.0
	elif _t < RISE_MS + HOLD_MS + FALL_MS:
		w = 1.0 - _ease_io((_t - RISE_MS - HOLD_MS) / FALL_MS)
	else:
		_active = false
		_last_w = 0.0
		return arr
	_last_w = w
	for idx in _weights:
		arr[idx] = _weights[idx] * w
	return arr

func _ease_io(t: float) -> float:
	return 4.0 * t * t * t if t < 0.5 else 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0
