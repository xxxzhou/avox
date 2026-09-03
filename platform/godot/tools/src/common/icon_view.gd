class_name IconView
extends Control
## 矢量图标视图 (非交互) —— 卡片/标题等处替代 emoji 字符 Label, 与 IconButton 共用绘制。

var icon_kind: int = IconButton.Icon.PLAY:
	set(v):
		icon_kind = v
		queue_redraw()

## 图标外接直径 / 控件短边
var icon_ratio := 0.5:
	set(v):
		icon_ratio = v
		queue_redraw()

var icon_color := Color.WHITE:
	set(v):
		icon_color = v
		queue_redraw()

func _draw() -> void:
	IconButton.draw_icon(self, icon_kind, Vector2(size.x, size.y) * 0.5,
		minf(size.x, size.y) * icon_ratio * 0.5, icon_color)
