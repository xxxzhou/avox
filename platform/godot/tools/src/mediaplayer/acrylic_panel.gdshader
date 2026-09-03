shader_type canvas_item;
// Acrylic 磨砂面板 —— avox Godot 播放器 (Fluent 升级)
// 直接采样视频纹理做模糊, 不走 BackBufferCopy/屏幕采样:
// 项目锁 max_fps=30 且 Intel iGPU 上屏幕拷贝有固定开销(见 docs/GPU直通渲染方案.md §5)。
// 映射链: 面板UV -> 屏幕UV(面板归一化矩形) -> 视频内容矩形(keep-aspect居中) -> 视频纹理UV。

uniform sampler2D video_tex : source_color, filter_linear, repeat_disable;
uniform float has_tex = 0.0;                 // 0=无画面(未开播/纯音频): 退化为纯色面板
uniform vec4 panel_rect = vec4(0.0, 0.0, 1.0, 1.0);   // 面板在屏幕上的归一化矩形 x,y,w,h
uniform vec4 video_rect = vec4(0.0, 0.0, 1.0, 1.0);   // 视频内容在屏幕上的归一化矩形
uniform vec2 screen_size = vec2(1280.0, 720.0);       // 屏幕像素尺寸(模糊半径换算用)
uniform vec2 pixel_size = vec2(100.0, 40.0);          // 面板像素尺寸(圆角 SDF 用)
uniform float corner_radius = 12.0;                   // 圆角半径 px
uniform float blur_amount = 28.0;                     // 模糊半径 px
uniform vec4 tint : source_color = vec4(0.03, 0.04, 0.055, 0.55);  // 面板暗色调
uniform vec4 border_color : source_color = vec4(1.0, 1.0, 1.0, 0.08);
uniform float border_width = 1.0;

// fragment 入参 COLOR 已含 modulate(HUD 整体淡入淡出), 用 varying 接住乘回去
varying vec4 vertex_color;

void vertex() {
	vertex_color = COLOR;
}

float rounded_rect_sdf(vec2 p, vec2 half_size, float r) {
	vec2 q = abs(p) - half_size + vec2(r);
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void fragment() {
	vec2 px = (UV - 0.5) * pixel_size;
	float d = rounded_rect_sdf(px, pixel_size * 0.5, max(corner_radius, 0.5));
	float shape = 1.0 - smoothstep(-0.75, 0.75, d);
	vec3 col;
	if (has_tex > 0.5) {
		// 面板UV -> 屏幕UV -> 视频内容UV (面板越界到内容外时 clamp 取边缘)
		vec2 suv = panel_rect.xy + UV * panel_rect.zw;
		vec2 vuv = clamp((suv - video_rect.xy) / max(video_rect.zw, vec2(1e-5)), vec2(0.0), vec2(1.0));
		// 12 tap 螺旋模糊; 半径换算到视频UV且按屏幕像素保持视觉圆形
		vec2 content_px = max(video_rect.zw * screen_size, vec2(1.0));
		vec2 rpx = vec2(blur_amount / content_px.x, blur_amount / content_px.y);
		vec3 sum = texture(video_tex, vuv).rgb;
		float wsum = 1.0;
		for (int i = 0; i < 12; i++) {
			float fi = float(i);
			float a = fi * 2.39996323;                       // 黄金角, 均匀布点
			float rr = sqrt((fi + 0.5) / 12.0);
			vec2 off = vec2(cos(a), sin(a)) * rr * rpx;
			sum += texture(video_tex, clamp(vuv + off, vec2(0.0), vec2(1.0))).rgb;
			wsum += 1.0;
		}
		vec3 blurred = sum / wsum;
		col = mix(blurred, tint.rgb, tint.a);
		col = col * 1.06 + vec3(0.015);                  // 轻微提亮, 接近 Acrylic 观感
	} else {
		col = tint.rgb * 1.4;                            // 无画面: 纯深色面板
	}
	// 1px 描边(SDF 边缘带)
	float bm = (1.0 - smoothstep(border_width - 0.5, border_width + 0.5, -d)) * step(0.0, -d);
	col = mix(col, border_color.rgb, border_color.a * bm);
	COLOR = vec4(col, shape) * vertex_color;
}
