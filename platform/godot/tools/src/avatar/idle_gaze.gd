extends RefCounted
## 视线游移程序化源: 1.5-4s 随机小幅目标, ~120ms 扫视线性趋近 (AIRI idle saccade 简化版)。
## tick(delta) 返回 52 维 (仅 8 路 eyeLook* 非零), 写 AvatarDriver gaze 通道 (max 叠加)。

const Arkit52 := preload("res://src/avatar/arkit52.gd")

# 8 路 eyeLook 名 (目标向量 x,y → 每眼 in/out/up/down 四路)
const LOOK_NAMES := ["eyeLookInLeft", "eyeLookOutLeft", "eyeLookInRight", "eyeLookOutRight",
	"eyeLookUpLeft", "eyeLookUpRight", "eyeLookDownLeft", "eyeLookDownRight"]

var _idx := {}           # 名 → canonical 索引
var _target := Vector2.ZERO
var _cur := Vector2.ZERO
var _wait := 0.0

func _init() -> void:
	for nm in LOOK_NAMES:
		_idx[nm] = Arkit52.index_of(nm)
	_arm()

func _arm() -> void:
	_wait = randf_range(1.5, 4.0)
	_target = Vector2(randf_range(-1.0, 1.0), randf_range(-0.6, 0.6)) * randf_range(0.15, 0.45)

func tick(delta: float) -> PackedFloat32Array:
	_wait -= delta
	if _wait <= 0.0:
		_arm()
	_cur = _cur.lerp(_target, clampf(delta / 0.12, 0.0, 1.0))
	var arr := PackedFloat32Array()
	arr.resize(52)
	var dl: float = maxf(-_cur.x, 0.0)   # x<0: 双眼向左 (左眼 out, 右眼 in)
	var dr: float = maxf(_cur.x, 0.0)    # x>0: 双眼向右 (左眼 in, 右眼 out)
	var du: float = maxf(_cur.y, 0.0)
	var dd: float = maxf(-_cur.y, 0.0)
	arr[_idx["eyeLookOutLeft"]] = dl
	arr[_idx["eyeLookInLeft"]] = dr
	arr[_idx["eyeLookInRight"]] = dl
	arr[_idx["eyeLookOutRight"]] = dr
	arr[_idx["eyeLookUpLeft"]] = du
	arr[_idx["eyeLookUpRight"]] = du
	arr[_idx["eyeLookDownLeft"]] = dd
	arr[_idx["eyeLookDownRight"]] = dd
	return arr
