class_name KeyCapture
extends Button
## 热键捕获控件 (tools/src/voiceinput)
## 常态显示当前热键; 点击进入捕获态 → 按下组合键 → 发 captured(name)。
## 捕获期间调用 hotkey.set_capture_mode(true), 让 GlobalHotkey 钩子不响应
## (否则"设置 F9 时"会被钩子当成一次录音热键触发)。
## 编排器置 button_pressed 驱动 hold 按住高亮 (需 toggle_mode = true)。

signal captured(hotkey_name: String)

## 由编排器注入的 GlobalHotkey 节点 (捕获期屏蔽钩子用)
var hotkey: GlobalHotkey

## 当前热键名 (插件 parseHotkey 语法: ctrl+shift+f9 / f9 / right_alt / space ...)。
## 赋值自动刷新显示 (编排器切模式后设 current_name 即可)。
var current_name: String = "f9":
	set(v):
		current_name = v
		_update_text()

## 显示前缀 (默认空; 单热键布局不用, 保留给多热键场景)
var caption := ""

var _capturing := false


func _ready() -> void:
	pressed.connect(_on_pressed)
	_update_text()


func _on_pressed() -> void:
	button_pressed = false        # 清掉点击带来的 toggle 视觉, 不干扰 hold 按住高亮
	_capturing = true
	text = "按组合键... (Esc 取消)"
	if hotkey:
		hotkey.set_capture_mode(true)
	grab_focus()


func _cancel_capture() -> void:
	_capturing = false
	if hotkey:
		hotkey.set_capture_mode(false)
	button_pressed = false
	_update_text()


## 供编排器在外部改 current_name / caption 后手动刷新显示
func refresh() -> void:
	_update_text()


func _gui_input(event: InputEvent) -> void:
	if not _capturing:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_ESCAPE:
			_cancel_capture()
		elif event.keycode in [KEY_CTRL, KEY_SHIFT, KEY_ALT, KEY_META]:
			pass  # 修饰键本身不构成组合, 等主键
		else:
			var name := _combo_name(event)
			if name != "":
				current_name = name
				_capturing = false
				if hotkey:
					hotkey.set_capture_mode(false)
				button_pressed = false
				_update_text()
				captured.emit(name)
		accept_event()  # 吞掉, 避免 F9 等传下去


## 组合键名 → 插件语法 (physical key + 修饰键; 用 physical 不受布局影响)
func _combo_name(event: InputEventKey) -> String:
	var parts: Array[String] = []
	var mods := event.get_modifiers_mask()
	if mods & KEY_MASK_CTRL:
		parts.append("ctrl")
	if mods & KEY_MASK_ALT:
		parts.append("alt")
	if mods & KEY_MASK_SHIFT:
		parts.append("shift")
	var k := event.physical_keycode
	var kname := ""
	if k >= KEY_A and k <= KEY_Z:
		kname = char(k).to_lower()
	elif k >= KEY_F1 and k <= KEY_F24:
		kname = "f%d" % (k - KEY_F1 + 1)
	elif k == KEY_SPACE:
		kname = "space"
	elif k == KEY_TAB:
		kname = "tab"
	else:
		return ""
	parts.append(kname)
	return "+".join(parts)


## 热键名 → 人读显示 (ctrl+shift+f9 → Ctrl+Shift+F9)
func display_name(name: String) -> String:
	var parts := name.split("+")
	var out: Array[String] = []
	for p in parts:
		match p:
			"ctrl": out.append("Ctrl")
			"alt": out.append("Alt")
			"shift": out.append("Shift")
			"space": out.append("Space")
			"tab": out.append("Tab")
			_: out.append(p.to_upper())
	return "+".join(out)


func _update_text() -> void:
	text = caption + display_name(current_name)
