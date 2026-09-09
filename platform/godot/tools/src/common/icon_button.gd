class_name IconButton
extends Button
## 单色矢量图标按钮 —— 替代 emoji 字符图标 (Windows 彩色 emoji / Android 缺字, 与扁平风格冲突)。
## 图标在 _draw 里矢量绘制, 叠加在 Button 样式之上 (text 保持空), 颜色跟随 font_color/hover。
## 绘制逻辑收敛在 draw_icon 静态函数, IconView (非交互图标) 复用同一份。

enum Icon { PLAY, PAUSE, REW, FFWD, VOL, MUTE, FULLSCREEN, MORE, ELLIPSIS, CLOSE, MIC, BOT, FACE, SEND, STOP, LINK }

## 图标外接直径 / 按钮短边
var icon_ratio := 0.5:
	set(v):
		icon_ratio = v
		queue_redraw()

var icon_kind: int = Icon.PLAY:
	set(v):
		icon_kind = v
		queue_redraw()

func _draw() -> void:
	var col := get_theme_color("font_hover_color") if is_hovered() else get_theme_color("font_color")
	draw_icon(self, icon_kind, Vector2(size.x, size.y) * 0.5, minf(size.x, size.y) * icon_ratio * 0.5, col)

## 矢量绘制单个图标: c=中心, r=图标外接半径 (坐标均为 -1..1 单位盒缩放到 r)
static func draw_icon(item: CanvasItem, kind: int, c: Vector2, r: float, col: Color) -> void:
	var lw := maxf(r * 0.16, 1.5)
	match kind:
		Icon.PLAY:
			# 播放三角视觉重心略右移
			var p := PackedVector2Array([
				c + Vector2(-0.7, -0.95) * r, c + Vector2(-0.7, 0.95) * r, c + Vector2(1.0, 0.0) * r])
			item.draw_colored_polygon(p, col)
		Icon.PAUSE:
			var w := r * 0.62
			item.draw_rect(Rect2(c + Vector2(-0.95 * r, -0.95 * r), Vector2(w, 1.9 * r)), col)
			item.draw_rect(Rect2(c + Vector2(0.95 * r - w, -0.95 * r), Vector2(w, 1.9 * r)), col)
		Icon.REW:
			item.draw_colored_polygon(PackedVector2Array([
				c + Vector2(0.9, -0.75) * r, c + Vector2(0.9, 0.75) * r, c + Vector2(0.0, 0.0) * r]), col)
			item.draw_colored_polygon(PackedVector2Array([
				c + Vector2(0.0, -0.75) * r, c + Vector2(0.0, 0.75) * r, c + Vector2(-0.9, 0.0) * r]), col)
		Icon.FFWD:
			item.draw_colored_polygon(PackedVector2Array([
				c + Vector2(-0.9, -0.75) * r, c + Vector2(-0.9, 0.75) * r, c + Vector2(0.0, 0.0) * r]), col)
			item.draw_colored_polygon(PackedVector2Array([
				c + Vector2(0.0, -0.75) * r, c + Vector2(0.0, 0.75) * r, c + Vector2(0.9, 0.0) * r]), col)
		Icon.VOL, Icon.MUTE:
			_draw_speaker(item, c, r, col)
			if kind == Icon.MUTE:
				item.draw_line(c + Vector2(0.35, -0.5) * r, c + Vector2(1.0, 0.5) * r, col, lw)
				item.draw_line(c + Vector2(1.0, -0.5) * r, c + Vector2(0.35, 0.5) * r, col, lw)
			else:
				item.draw_arc(c + Vector2(0.05, 0.0) * r, r * 0.45, -0.95, 0.95, 24, col, lw, true)
				item.draw_arc(c + Vector2(0.05, 0.0) * r, r * 0.8, -0.9, 0.9, 24, col, lw, true)
		Icon.FULLSCREEN:
			for sx in [-1.0, 1.0]:
				for sy in [-1.0, 1.0]:
					var corner := c + Vector2(sx, sy) * r
					item.draw_polyline(PackedVector2Array([
						corner + Vector2(0.0, -sy) * r * 0.55,
						corner,
						corner + Vector2(-sx, 0.0) * r * 0.55]), col, lw, true)
		Icon.MORE, Icon.ELLIPSIS:
			for x in [-0.72, 0.0, 0.72]:
				item.draw_circle(c + Vector2(x * r, 0.0), r * 0.18, col)
		Icon.CLOSE:
			item.draw_line(c + Vector2(-0.7, -0.7) * r, c + Vector2(0.7, 0.7) * r, col, lw)
			item.draw_line(c + Vector2(0.7, -0.7) * r, c + Vector2(-0.7, 0.7) * r, col, lw)
		Icon.SEND:
			# 纸飞机 (右指镖形, 区别于播放三角)
			item.draw_colored_polygon(PackedVector2Array([
				c + Vector2(1.0, 0.0) * r, c + Vector2(-0.9, -0.85) * r,
				c + Vector2(-0.45, 0.0) * r, c + Vector2(-0.9, 0.85) * r]), col)
		Icon.STOP:
			item.draw_rect(Rect2(c - Vector2(0.75, 0.75) * r, Vector2(1.5, 1.5) * r), col)
		Icon.MIC:
			# 话筒胶囊 + U 型支架 + 杆与底座
			item.draw_circle(c + Vector2(0.0, -0.55) * r, r * 0.3, col)
			item.draw_rect(Rect2(c + Vector2(-0.3, -0.55) * r, Vector2(0.6 * r, 0.9 * r)), col)
			item.draw_circle(c + Vector2(0.0, 0.35) * r, r * 0.3, col)
			item.draw_arc(c + Vector2(0.0, 0.05) * r, r * 0.65, 0.0, PI, 24, col, lw, true)
			item.draw_line(c + Vector2(0.0, 0.7) * r, c + Vector2(0.0, 1.0) * r, col, lw)
			item.draw_line(c + Vector2(-0.3, 1.0) * r, c + Vector2(0.3, 1.0) * r, col, lw)
		Icon.BOT:
			# 机器人: 天线 + 圆角头 (描边) + 双眼
			item.draw_line(c + Vector2(0.0, -0.95) * r, c + Vector2(0.0, -0.55) * r, col, lw)
			item.draw_circle(c + Vector2(0.0, -1.0) * r, r * 0.14, col)
			item.draw_rect(Rect2(c + Vector2(-0.8, -0.55) * r, Vector2(1.6 * r, 1.35 * r)), col, false, lw)
			item.draw_circle(c + Vector2(-0.32, 0.12) * r, r * 0.16, col)
			item.draw_circle(c + Vector2(0.32, 0.12) * r, r * 0.16, col)
		Icon.FACE:
			# Avatar/面具: 圆脸描边 + 双眼 + 微笑
			item.draw_arc(c, r * 0.95, 0.0, TAU, 40, col, lw, true)
			item.draw_circle(c + Vector2(-0.3, -0.2) * r, r * 0.12, col)
			item.draw_circle(c + Vector2(0.3, -0.2) * r, r * 0.12, col)
			item.draw_arc(c + Vector2(0.0, 0.1) * r, r * 0.45, 0.35, PI - 0.35, 24, col, lw, true)
		Icon.LINK:
			# 链路/实时信号: 中心圆点 + 左右对称双弧
			item.draw_circle(c, r * 0.26, col)
			item.draw_arc(c, r * 0.58, -0.62, 0.62, 20, col, lw, true)
			item.draw_arc(c, r * 0.58, PI - 0.62, PI + 0.62, 20, col, lw, true)
			item.draw_arc(c, r * 0.95, -0.72, 0.72, 20, col, lw, true)
			item.draw_arc(c, r * 0.95, PI - 0.72, PI + 0.72, 20, col, lw, true)

static func _draw_speaker(item: CanvasItem, c: Vector2, r: float, col: Color) -> void:
	var p := PackedVector2Array([
		c + Vector2(-1.0, -0.35) * r, c + Vector2(-0.4, -0.35) * r, c + Vector2(0.05, -0.8) * r,
		c + Vector2(0.05, 0.8) * r, c + Vector2(-0.4, 0.35) * r, c + Vector2(-1.0, 0.35) * r])
	item.draw_colored_polygon(p, col)
