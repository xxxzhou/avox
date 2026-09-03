extends RefCounted
## 自动眨眼程序化源: 间隔 3-8s 随机, 闭眼 75ms, 睁眼 150-300ms (参数对齐 AIRI AutoEyeBlink)。
## tick(delta) 返回眨眼量 0..1 (1=全闭); to_blendshape 展开成 52 维
## (仅 eyeBlinkLeft/Right), 场景写入 AvatarDriver blink 通道 (max 叠加)。

const Arkit52 := preload("res://src/avatar/arkit52.gd")

var _idx_l := -1
var _idx_r := -1
var _wait := 0.0        # 距下次眨眼
var _phase := 0         # 0=待机 1=闭眼中 2=睁开中
var _phase_t := 0.0
var _open_ms := 0.2     # 本次睁眼时长 (每次随机 0.15~0.30)

func _init() -> void:
	_idx_l = Arkit52.index_of("eyeBlinkLeft")
	_idx_r = Arkit52.index_of("eyeBlinkRight")
	_arm()

func _arm() -> void:
	_wait = randf_range(3.0, 8.0)

func tick(delta: float) -> float:
	if _phase == 0:
		_wait -= delta
		if _wait <= 0.0:
			_phase = 1
			_phase_t = 0.0
			_open_ms = randf_range(0.15, 0.30)
		return 0.0
	_phase_t += delta
	if _phase == 1:
		if _phase_t >= 0.075:
			_phase = 2
			_phase_t = 0.0
			return 1.0
		return _phase_t / 0.075
	var v := 1.0 - _phase_t / _open_ms
	if v <= 0.0:
		_phase = 0
		_arm()
		return 0.0
	return v

# 眨眼量 → 52 维 blendshape (只填 eyeBlink 两路)
func to_blendshape(v: float) -> PackedFloat32Array:
	var arr := PackedFloat32Array()
	arr.resize(52)
	if v > 0.0 and _idx_l >= 0 and _idx_r >= 0:
		arr[_idx_l] = v
		arr[_idx_r] = v
	return arr
