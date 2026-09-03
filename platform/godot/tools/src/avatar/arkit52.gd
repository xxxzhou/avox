## ARKit52 blendshape canonical 名表 — 消费端唯一名表来源 (音频/视频两路统一顺序)。
## 与 SDK src/avox/AvoxAvatar.h getArkit52NamesCsv() 严格一致: index0=_neutral 占位 (不驱动 mesh);
## wav2arkit 原生序 (browDownLeft@0..noseSneerRight@50, tongueOut@51) 已由 avox_avatar 插件
## 整体 +1 重排对齐本表 (jawOpen: 24→25)。场景禁止按裸索引取 blendshape, 一律 index_of(名)。
const CSV := "_neutral,browDownLeft,browDownRight,browInnerUp,browOuterUpLeft,browOuterUpRight,cheekPuff,cheekSquintLeft,cheekSquintRight,eyeBlinkLeft,eyeBlinkRight,eyeLookDownLeft,eyeLookDownRight,eyeLookInLeft,eyeLookInRight,eyeLookOutLeft,eyeLookOutRight,eyeLookUpLeft,eyeLookUpRight,eyeSquintLeft,eyeSquintRight,eyeWideLeft,eyeWideRight,jawForward,jawLeft,jawOpen,jawRight,mouthClose,mouthDimpleLeft,mouthDimpleRight,mouthFrownLeft,mouthFrownRight,mouthFunnel,mouthLeft,mouthLowerDownLeft,mouthLowerDownRight,mouthPressLeft,mouthPressRight,mouthPucker,mouthRight,mouthRollLower,mouthRollUpper,mouthShrugLower,mouthShrugUpper,mouthSmileLeft,mouthSmileRight,mouthStretchLeft,mouthStretchRight,mouthUpperUpLeft,mouthUpperUpRight,noseSneerLeft,noseSneerRight"

static func names() -> PackedStringArray:
	return CSV.split(",")

static func index_of(bs_name: String) -> int:
	return CSV.split(",").find(bs_name)
