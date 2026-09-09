extends RefCounted
## 多驱动源合成链: 各源按通道提交 52 维 canonical blendshape (arkit52.gd 序), compose() 出单帧。
## 通道语义: bs_base=基础层 (独占 claim, 换源 500ms 交叉淡入防跳变), blink/gaze=max 叠加
## (程序化源只增不减, 不压采集真值), emotion=加法叠加 (0..1 封顶)。
## 架构参考 AIRI: Live2D 每帧插件链相位顺序 (motion-manager.ts) + 驱动器 ownerId 抢占
## (stores/motion-control.ts setPose/claim)。场景每帧: tick 程序化源 → set_source → compose。

const CH_BASE := "bs_base"
const CH_EMOTION := "emotion"
const CH_BLINK := "blink"
const CH_GAZE := "gaze"
const FADE_MS := 500.0  # base 源切换交叉淡入时长

var _sources := {}      # channel -> {owner: PackedFloat32Array(52)}
var _base_owner := ""   # CH_BASE 当前独占 owner ("" = 无源)
var _fade_from: PackedFloat32Array  # 换源瞬间旧 base 快照
var _fade_t := 1.0

func _init() -> void:
	_fade_from = PackedFloat32Array()
	_fade_from.resize(52)

# 基础层独占权 (后到抢占); 换源时快照旧值线性交叠
func claim_base(owner: String) -> void:
	if _base_owner == owner:
		return
	_fade_from = _current_base()
	_fade_t = 0.0
	_base_owner = owner

func set_source(owner: String, channel: String, arr: PackedFloat32Array) -> void:
	if arr.size() < 52:
		return
	if not _sources.has(channel):
		_sources[channel] = {}
	_sources[channel][owner] = arr

# 移除该 owner 在所有通道的源 (base owner 变更时自动重算 claim)
func release(owner: String) -> void:
	for channel in _sources:
		_sources[channel].erase(owner)
	if _base_owner == owner:
		_base_owner = ""

# 每帧推进 (目前只有 base 交叉淡入计时)
func tick(delta: float) -> void:
	if _fade_t < 1.0:
		_fade_t = minf(_fade_t + delta * 1000.0 / FADE_MS, 1.0)

# 合成单帧 52 维输出 (新数组, 调用方可直接写 mesh)
func compose() -> PackedFloat32Array:
	var out := PackedFloat32Array()
	out.resize(52)
	# 基础层 (独占 owner, 换源期与旧帧交叠)
	var cur := _current_base()
	for i in 52:
		out[i] = lerpf(_fade_from[i], cur[i], _fade_t)
	# blink/gaze: max 叠加
	for channel in [CH_GAZE, CH_BLINK]:
		if _sources.has(channel):
			for owner in _sources[channel]:
				var arr: PackedFloat32Array = _sources[channel][owner]
				for i in 52:
					if arr[i] > out[i]:
						out[i] = arr[i]
	# emotion: 加法封顶
	if _sources.has(CH_EMOTION):
		for owner in _sources[CH_EMOTION]:
			var arr: PackedFloat32Array = _sources[CH_EMOTION][owner]
			for i in 52:
				out[i] = clampf(out[i] + arr[i], 0.0, 1.0)
	return out

func _current_base() -> PackedFloat32Array:
	if _base_owner != "" and _sources.has(CH_BASE) and _sources[CH_BASE].has(_base_owner):
		return _sources[CH_BASE][_base_owner]
	var z := PackedFloat32Array()
	z.resize(52)
	return z
