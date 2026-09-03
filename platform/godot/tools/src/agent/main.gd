extends Control
## avox Agent 对话工具 (tools/src/agent) —— 全屏工具页
## 仿 AgentShell (src/avox_agent/AgentShell.cpp) 的 GUI 版: 多轮对话 + 流式 token + 配置切换。
## 依赖: addons/avox_godot 已部署 (AgentNode 编译进 avox_godot.dll)。
## MVP: 纯对话 (tool_call 只显示不执行), 无 skill 路由/轨迹/上下文压缩。

const ACCENT     := Color("#3B82F6")
const BG_COLOR   := Color("#0B0F16")
const PANEL_BG   := Color(0.03, 0.04, 0.055, 0.55)
const USER_BG    := Color(0.12, 0.18, 0.30, 0.85)
const ASST_BG    := Color(0.10, 0.12, 0.16, 0.70)
const TOOL_BG    := Color(0.15, 0.12, 0.05, 0.60)
const TOOL_BORDER := Color("#EAB308")
const REASON_CLR := Color(0.50, 0.55, 0.62)
const ERR_CLR    := Color("#EF4444")
const OK_CLR     := Color(0.35, 0.82, 0.55)
const WARN_CLR   := Color(0.95, 0.75, 0.25)
# 共享 UI 基建 (主题字体/flat stylebox, 与 hub/mediaplayer 一致)
const UiKit := preload("res://src/common/ui_kit.gd")
# ARKit52 canonical 名表 (与 avatar 场景/SDK 同序, 音频路插件已重排); 禁止按裸索引取 blendshape
const Arkit52 := preload("res://src/avatar/arkit52.gd")
# 3D avatar 视图 (共享组件: 轨道相机/模型加载/眨眼/视线/情绪合成链)
const AvatarView := preload("res://src/avatar/avatar_view.gd")
# 3D avatar 模型搜索路径 (与 avatar 场景一致)
const AVATAR_PATHS := ["res://src/avatar/avatar.gltf", "res://src/avatar/avatar.glb", "user://avatar.glb", "res://avatar.glb", "res://assets/avatar.glb"]

var agent: AgentNode
var tts: TtsNode
var face: FaceNode
var avatar_view              # 3D avatar 视图 (AvatarView); 加载成功接管右半区, 2D 简笔脸回退

# 虚拟人语音 (TTS + 句级播放 + RMS 口型)
var _tts_ready := false
var _tts_sample_rate := 22050          # sherpa Kokoro 原生采样率 (tts_desc 更新)
var _sentence_buf := ""                # agent_token → 句缓冲 (标点切句喂 TTS)
var _tts_pcm := PackedByteArray()      # 当前句累积 PCM (final 时拼整句入队)
var _tts_player: AudioStreamPlayer
var _play_queue := []                  # 整句播放队列 [{stream, env, dur_ms}]
var _cur_env := PackedFloat32Array()   # 当前播放句的 RMS 包络 (20ms/帧, 驱动嘴形)
var _cur_play_dur_ms := 0
var _avatar_mouth: Panel              # 嘴部 (scale.y 随 RMS)
var _avatar_emotion := "neutral"
var _avatar_emotion_intensity := 0.7   # [EMO:name:intensity] / set_emotion 强度 (0~1)
var _avatar_2d_col: VBoxContainer      # 2D 简笔脸容器 (3D 加载成功后隐藏)
var _zeros52 := PackedFloat32Array()   # 停播时 3D base 归零 (闭嘴, 眨眼/视线不受影响)
var _avatar_emotion_lbl: Label
var _avatar_preamble := ""               # 虚拟人 system prompt (assets/agent/system_prompt_avatar.md)
var _face_enabled := false                          # face_ready 收到, 模型就绪 (未就绪/未到帧 → 回退 RMS)
var _cur_blendshape := PackedFloat32Array()         # 最新一帧 ARKit52 (52 维 [0,1], face_blendshape 更新)
var _bs_jaw := -1     # jawOpen 的 canonical 名表索引 (_ready 按名解析, 见 arkit52.gd)
var _bs_lower := -1   # mouthLowerDownLeft 的名表索引 (叠加嘴部下压)

# 配置
var _configs := []          # [{name, url, apiKey, model, ...}]
var _config_names := []     # ["my-agent", ...]
var _current_config := ""   # 当前配置名
var _configured := false    # agent.configure 已调
var _config_raw := {}       # 完整原始配置 (写回时保留 agentshell 额外字段, 如 temperature/maxTokens)

# 对话状态
var _generating := false
var _request_id := 0
var _current_bubble: RichTextLabel   # 当前流式 assistant 气泡
var _current_bubble_row: Control     # 该气泡所在 hbox (推理标签要插到它前面)
var _current_tool_card: VBoxContainer  # 当前工具调用卡片
var _reasoning_label: RichTextLabel  # 当前推理标签

# UI
var _chat_scroll: ScrollContainer
var _chat_box: VBoxContainer
var _chat_empty: VBoxContainer   # 空态引导 (BOT 图标 + 建议问题胶囊)
var _input: LineEdit
var _send_btn: IconButton
var _top_bar: PanelContainer
var _config_opt: OptionButton
var _status_dot: Button
var _toast: Label
var _settings_popup: PanelContainer
var _url_edit: LineEdit
var _key_edit: LineEdit
var _model_edit: LineEdit
var _sys_edit: TextEdit
var _menu_open := false
var _left_panel: Control       # 左半: Agent 对话
var _avatar_panel: Control     # 右半: 虚拟人 (avatar/TTS/情绪, 待接入)
var _scroll_pending := false   # _scroll_to_bottom 帧内合并 (流式高频调用)

# ── 生命周期 ──

func _ready() -> void:
	# 高 DPI: 钉 1280 画布基准 + 窗口物理 ×sc (与播放器同观感; 项目基准 1920x1080 见 project.godot)
	UiKit.apply_desktop_dpi(get_window())
	theme = UiKit.build_theme()
	_build_ui()
	_load_avatar_preamble()
	_create_agent()
	_load_agent_json()
	_update_status()


func _notification(what: int) -> void:
	if what == NOTIFICATION_EXIT_TREE:
		if agent:
			agent.cancel()
			remove_child(agent)
			agent.queue_free()
			agent = null


# ── AgentNode ──

func _create_agent() -> void:
	agent = AgentNode.new()
	add_child(agent)
	agent.agent_token.connect(_on_token)
	agent.agent_reasoning.connect(_on_reasoning)
	agent.tool_call.connect(_on_tool_call)
	agent.tool_result.connect(_on_tool_result)
	agent.agent_result.connect(_on_result)
	# TTS (sherpa Kokoro): agent_token 切句喂入 → tts_audio 句级播放 + RMS 口型
	tts = TtsNode.new()
	add_child(tts)
	tts.tts_desc.connect(_on_tts_desc)
	tts.tts_audio.connect(_on_tts_audio)
	tts.tts_ready.connect(_on_tts_ready)
	tts.error.connect(_on_tts_error)
	tts.start()
	# Face (avox_avatar wav2arkit): TTS PCM → ARKit52 blendshape, 驱动 2D 嘴形 (优先于 RMS)
	face = FaceNode.new()
	add_child(face)
	face.face_blendshape.connect(_on_face_blendshape)
	face.face_ready.connect(_on_face_ready)
	face.error.connect(_on_face_error)
	face.start()
	_bs_jaw = Arkit52.index_of("jawOpen")
	_bs_lower = Arkit52.index_of("mouthLowerDownLeft")
	_zeros52.resize(52)


func _load_avatar_preamble() -> void:
	# 虚拟人对话规约 (独立于 CLI 共享的 system_prompt.md, 不污染 GUI 自动化场景)
	for p in ["res://assets/agent/system_prompt_avatar.md", "user://assets/agent/system_prompt_avatar.md"]:
		var f := FileAccess.open(p, FileAccess.READ)
		if f:
			_avatar_preamble = f.get_as_text()
			f.close()
			return


func _apply_system_prompt() -> void:
	# avatar preamble 为基础 system prompt; 设置弹窗里的 sys_edit 作为补充
	var extra := ""
	if _sys_edit and not _sys_edit.text.strip_edges().is_empty():
		extra = "\n\n" + _sys_edit.text.strip_edges()
	agent.set_system_prompt(_avatar_preamble + extra)


# ── 配置管理 ──

func _load_agent_json() -> void:
	_configs.clear()
	_config_names.clear()
	_config_raw = {}
	# 读取顺序: %LOCALAPPDATA%/avox (与 AgentShell 共用同一份) → res:// (打包) → user:// (开发)
	var paths := []
	var shared := _shared_config_path()
	if not shared.is_empty():
		paths.append(shared)
	paths.append("res://assets/config/agent.json")
	paths.append("user://assets/config/agent.json")
	var raw := ""
	for p in paths:
		var f := FileAccess.open(p, FileAccess.READ)
		if f:
			raw = f.get_as_text()
			f.close()
			break
	if raw.is_empty():
		_config_opt.add_item("(无配置 — 手动填写)")
		return
	var json := JSON.new()
	if json.parse(raw) != OK:
		_config_opt.add_item("(配置解析失败)")
		return
	var data: Dictionary = json.data
	if not data is Dictionary:
		_config_opt.add_item("(配置格式错误)")
		return
	_config_raw = data  # 保留完整原始结构, 写回时不丢 agentshell 额外字段
	# 解析各配置 (键=配置名, 值=配置详情)
	var now_name: String = data.get("now", "")
	for key in data.keys():
		if key == "now":
			continue
		var val = data[key]
		if not val is Dictionary:
			continue
		var cfg := {
			"name": key,
			"url": str(val.get("url", "")),
			"apiKey": str(val.get("apiKey", "")),
			"model": str(val.get("model", "")),
			"provider": str(val.get("provider", "openai")),
			"apiPath": str(val.get("apiPath", "/chat/completions")),
		}
		_configs.append(cfg)
		_config_names.append(key)
		_config_opt.add_item(key)
	# 自动选 "now" 指定的配置
	if now_name in _config_names:
		var idx := _config_names.find(now_name)
		_config_opt.select(idx)
		_apply_config(idx)


func _apply_config(idx: int, persist: bool = false) -> void:
	if idx < 0 or idx >= _configs.size():
		return
	var cfg: Dictionary = _configs[idx]
	var url: String = cfg.get("url", "")
	var key: String = cfg.get("apiKey", "")
	var model: String = cfg.get("model", "")
	if url.is_empty():
		_toast_msg("配置 '%s' 缺少 url" % cfg.get("name", ""))
		return
	var ok := agent.configure(url, key, model)
	if ok:
		_configured = true
		_current_config = cfg.get("name", "")
		_apply_system_prompt()
		if persist:
			_save_agent_now(_current_config)
		_toast_msg("已切换: %s (%s)" % [_current_config, model])
		# 同步到手动输入框
		_url_edit.text = url
		_key_edit.text = key
		_model_edit.text = model
	else:
		_toast_msg("配置失败: %s" % agent.get_last_error())
	_update_status()


func _on_config_selected(idx: int) -> void:
	if idx < _configs.size():
		_apply_config(idx, true)


func _on_manual_configure() -> void:
	var url := _url_edit.text.strip_edges()
	var key := _key_edit.text.strip_edges()
	var model := _model_edit.text.strip_edges()
	if url.is_empty():
		_toast_msg("URL 不能为空")
		return
	var ok := agent.configure(url, key, model)
	if ok:
		_configured = true
		_current_config = "(手动)"
		_toast_msg("手动配置成功 (%s)" % model)
	else:
		_toast_msg("配置失败: %s" % agent.get_last_error())
	_update_status()


func _shared_config_path() -> String:
	# 与 AgentShell (AssetLoader::getSystemConfigPath) 同一位置: %LOCALAPPDATA%/avox
	var la := OS.get_environment("LOCALAPPDATA")
	if la.is_empty():
		return ""
	return la + "/avox/config/agent.json"


func _save_agent_now(name: String) -> void:
	# 只改 "now" 字段, 完整回写, 保留 agentshell 的额外字段 (temperature/maxTokens/...)
	var p := _shared_config_path()
	if p.is_empty() or _config_raw.is_empty():
		return
	_config_raw["now"] = name
	DirAccess.make_dir_recursive_absolute(p.get_base_dir())
	var f := FileAccess.open(p, FileAccess.WRITE)
	if not f:
		_toast_msg("配置写入失败: %s" % p)
		return
	f.store_string(JSON.stringify(_config_raw, "\t"))
	f.close()


# ── 对话 ──

func _send() -> void:
	if _generating:
		# 生成中 → 取消
		agent.cancel()
		return
	var text := _input.text.strip_edges()
	if text.is_empty():
		return
	if not _configured:
		_toast_msg("请先配置 (选配置或手动填写)")
		return
	_input.text = ""
	_input.editable = false
	_hide_chat_empty()
	# 用户气泡
	_add_user_bubble(text)
	# 准备 assistant 气泡 (流式追加)
	_current_bubble = _add_asst_bubble()
	_current_tool_card = null
	_reasoning_label = null
	# 发送
	_generating = true
	_request_id = agent.chat(text)
	_update_send_btn()
	_update_status()
	if _request_id == 0:
		_generating = false
		_current_bubble.text = "[color=#%s]发送失败: %s[/color]" % [ERR_CLR.to_html(), agent.get_last_error()]
		_input.editable = true
		_update_send_btn()
		_update_status()


func _on_token(req_id: int, token: String) -> void:
	if req_id != _request_id:
		return
	if not _current_bubble:
		return
	# 句级缓冲: 切句后在完整句内剥离 marker (避免跨 token 碎片显示) → 净句显示 + 喂 TTS
	_sentence_buf += token
	_flush_sentence(false)
	_scroll_to_bottom()


func _on_reasoning(req_id: int, token: String) -> void:
	if req_id != _request_id:
		return
	# 推理流: 灰色小字, 不进主气泡
	if not _reasoning_label:
		_reasoning_label = _add_reasoning_label()
	_reasoning_label.text = _reasoning_label.text + token
	_scroll_to_bottom()


func _on_tool_call(req_id: int, name: String, args_json: String) -> void:
	if req_id != _request_id:
		return
	# 虚拟人控制指令 (agent.h 预留通道): set_emotion / play_gesture
	_exec_avatar_tool(name, args_json)
	_current_tool_card = _add_tool_card(name, args_json)
	_scroll_to_bottom()


func _exec_avatar_tool(name: String, args_json: String) -> void:
	# Phase 1: set_emotion 改情绪显示; play_gesture toast 提示 (表情/骨骼留 VRM)
	var n := name.to_lower()
	var json := JSON.new()
	var args := {}
	if json.parse(args_json) == OK and json.data is Dictionary:
		args = json.data
	if n == "set_emotion":
		var inten := float(args.get("intensity", 0.7))
		_apply_emotion(str(args.get("emotion", args.get("emo", ""))), inten)
	elif n == "play_gesture":
		_toast_msg("手势: %s" % str(args.get("name", args.get("gesture", ""))))


func _on_avatar_model_loaded(ok: bool, info: String) -> void:
	if ok:
		if _avatar_2d_col:
			_avatar_2d_col.visible = false   # 3D 接管右半区
	else:
		print("[agent] avatar 3D 加载失败, 回退 2D 简笔脸: ", info)


func _on_tool_result(req_id: int, name: String, result_json: String, ok: bool) -> void:
	if req_id != _request_id:
		return
	if not _current_tool_card:
		return
	# 在卡片下追加结果
	var result_label := RichTextLabel.new()
	result_label.bbcode_enabled = true
	result_label.fit_content = true
	result_label.scroll_following = false
	result_label.custom_minimum_size = Vector2(0, 0)
	result_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var color := OK_CLR if ok else ERR_CLR
	# 截断过长结果
	var display := result_json
	if display.length() > 500:
		display = display.substr(0, 500) + "..."
	result_label.text = "[color=#%s]→ %s[/color]" % [color.to_html(), display.replace("[", "").replace("]", "")]
	_current_tool_card.add_child(result_label)
	_scroll_to_bottom()


func _on_result(req_id: int, content: String, error: String) -> void:
	if req_id != _request_id:
		return
	_generating = false
	_input.editable = true
	_input.grab_focus()
	_update_send_btn()
	_update_status()
	if not error.is_empty():
		if _current_bubble:
			_current_bubble.text = "[color=#%s]%s[/color]" % [ERR_CLR.to_html(), error]
	elif not content.is_empty() and _current_bubble:
		# 用完整 content 覆盖 (比 token 拼接更可靠, 同 AgentShell); 剥离 marker + 首尾空白
		# (content 含原始 [EMO:]/[SPEED:], 不剥离会和流式净句不一致并露出 marker)
		_current_bubble.text = _strip_markers(content).strip_edges()
	_current_bubble = null
	_current_bubble_row = null
	_current_tool_card = null
	_reasoning_label = null
	_flush_sentence(true)   # 末尾残句 (无句末标点) 强制喂 TTS
	_scroll_to_bottom()


func _clear_chat() -> void:
	for c in _chat_box.get_children():
		_chat_box.remove_child(c)
		c.queue_free()
	agent.clear_messages()
	if tts:
		tts.stop()           # 中断合成 + 清 worker 队列 (barge-in)
	_play_queue.clear()     # 丢弃待播句
	_tts_pcm.clear()
	_sentence_buf = ""
	_current_bubble = null
	_current_bubble_row = null
	_current_tool_card = null
	_reasoning_label = null
	_generating = false
	_input.editable = true
	_update_send_btn()
	_update_status()
	_show_chat_empty()

# 空态引导: BOT 图标 + 建议问题胶囊 (点击填入输入框), 首条消息后隐藏
func _show_chat_empty() -> void:
	if _chat_empty == null:
		_chat_empty = VBoxContainer.new()
		_chat_empty.alignment = BoxContainer.ALIGNMENT_CENTER
		_chat_empty.add_theme_constant_override("separation", 14)
		var icon := IconView.new()
		icon.icon_kind = IconButton.Icon.BOT
		icon.icon_color = Color(ACCENT, 0.35)
		icon.custom_minimum_size = Vector2(72, 72)
		icon.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_chat_empty.add_child(icon)
		var hint := Label.new()
		hint.text = "开始对话"
		hint.add_theme_color_override("font_color", Color(0.55, 0.6, 0.68))
		hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_chat_empty.add_child(hint)
		for s in ["介绍一下你自己", "用一句话解释 WebRTC", "帮我写一个 ffmpeg 转码命令"]:
			var pill := Button.new()
			pill.text = s
			pill.focus_mode = Control.FOCUS_NONE
			pill.add_theme_font_size_override("font_size", 13)
			pill.add_theme_color_override("font_color", Color(0.8, 0.85, 0.92))
			pill.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.05), 15, Color(1, 1, 1, 0.10), 1))
			pill.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.16), 15, Color(ACCENT, 0.6), 1))
			pill.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.24), 15))
			pill.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
			pill.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
			pill.pressed.connect(func():
				_input.text = s
				_input.grab_focus())
			_chat_empty.add_child(pill)
	_chat_empty.visible = true
	if _chat_empty.get_parent() == null:
		_chat_box.add_child(_chat_empty)

func _hide_chat_empty() -> void:
	if _chat_empty:
		_chat_empty.visible = false


# ── 虚拟人语音 (TTS 句级播放 + RMS 口型) ──

func _on_tts_desc(sample_rate: int, _channels: int) -> void:
	_tts_sample_rate = sample_rate
	# 同步告知 face 输入格式 (TTS 出 s16 mono); 后续 feed_pcm 才能正确重采样到 16kHz
	if face:
		face.set_audio_desc(sample_rate, 1)


func _on_tts_ready() -> void:
	_tts_ready = true
	_toast_msg("语音合成就绪")


func _on_tts_error(msg: String) -> void:
	_toast_msg("TTS: %s" % msg)


func _on_face_ready() -> void:
	_face_enabled = true
	_toast_msg("面部追踪就绪")


func _on_face_error(msg: String) -> void:
	_toast_msg("面部: %s" % msg)


func _on_face_blendshape(blendshape: PackedFloat32Array, _pts: int, _final: bool) -> void:
	# 缓存最新一帧 ARKit52 (52 维 [0,1]); _process 取嘴部索引驱动 2D 嘴形 / 推 3D base
	if blendshape.size() >= 52:
		_cur_blendshape = blendshape
		if avatar_view and avatar_view.is_loaded():
			avatar_view.set_base("audio", blendshape)


func _on_tts_audio(data: PackedByteArray, pts: int, final: bool) -> void:
	# sherpa 句级流式: data=增量 s16 mono PCM; final=本句末片
	if data.size() > 0:
		_tts_pcm.append_array(data)
		# ★ 同时喂 face (PCM→ARKit52), 与 RMS 并行; face 未就绪 (_face_enabled=false) 则不喂
		if face and _face_enabled:
			face.feed_pcm(data, pts)
	if final:
		var pcm := _tts_pcm.duplicate()
		_tts_pcm.clear()
		if pcm.size() > 0:
			_enqueue_pcm(pcm)


func _enqueue_pcm(pcm: PackedByteArray) -> void:
	# s16 mono PCM → AudioStreamWAV + 离线 RMS 包络 (20ms/帧); 入队顺序播放
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = _tts_sample_rate
	wav.stereo = false
	wav.data = pcm
	var dur_ms := int(pcm.size() * 1000.0 / (_tts_sample_rate * 2))
	_play_queue.append({"stream": wav, "env": _compute_rms_env(pcm), "dur_ms": dur_ms})
	if not _tts_player.playing:
		_play_next()


func _play_next() -> void:
	if _play_queue.is_empty():
		_cur_env = PackedFloat32Array()
		_cur_play_dur_ms = 0
		return
	var item: Dictionary = _play_queue.pop_front()
	_cur_env = item["env"]
	_cur_play_dur_ms = int(item["dur_ms"])
	_tts_player.stream = item["stream"]
	_tts_player.play()


func _compute_rms_env(pcm: PackedByteArray) -> PackedFloat32Array:
	# 每 20ms 一帧 RMS, 驱动嘴形 (离线算, 播放时按进度取值)
	var env := PackedFloat32Array()
	var frame_n := maxi(1, _tts_sample_rate * 20 / 1000)
	var total := pcm.size() / 2
	var i := 0
	while i < total:
		var n := mini(frame_n, total - i)
		var sum := 0.0
		for j in range(n):
			var f := float(pcm.decode_s16((i + j) * 2)) / 32768.0
			sum += f * f
		env.append(sqrt(sum / float(n)))
		i += n
	return env


func _process(_delta: float) -> void:
	# 播放结束 → 驱动队列下一句 (AudioStreamPlayer 播完 playing=false, _enqueue_pcm 只触发首句)
	if _tts_player and not _tts_player.playing and not _play_queue.is_empty():
		_play_next()
	# 播放中: 优先 ARKit52 blendshape (face 就绪), 回退 RMS 包络; 平滑跟随
	if not _avatar_mouth:
		return
	var speaking := _tts_player != null and _tts_player.playing
	if speaking and _face_enabled and _cur_blendshape.size() >= 52 and _bs_jaw >= 0:
		# ARKit52 嘴形: jawOpen 主控开合, 模型偏保守故放大 + 叠 mouthLowerDownLeft (索引按名表解析)
		var open := clampf(_cur_blendshape[_bs_jaw] * 3.0 + _cur_blendshape[_bs_lower] * 0.5, 0.0, 0.85)
		var target := 0.15 + open
		_avatar_mouth.scale.y = lerp(_avatar_mouth.scale.y, target, 0.4)
	elif speaking and _cur_env.size() > 0 and _cur_play_dur_ms > 0:
		# RMS 回退 (face 未就绪或本句 blendshape 尚未到达)
		var pos_ms := int(_tts_player.get_playback_position() * 1000.0)
		var idx := clampi(pos_ms / 20, 0, _cur_env.size() - 1)
		var target := 0.15 + clampf(_cur_env[idx] * 4.0, 0.0, 0.85)
		_avatar_mouth.scale.y = lerp(_avatar_mouth.scale.y, target, 0.4)
	else:
		_avatar_mouth.scale.y = lerp(_avatar_mouth.scale.y, 0.15, 0.2)
	# 3D avatar: 说话帧由 _on_face_blendshape 推 base; 停播归零闭嘴 (眨眼/视线视图内自走)
	if avatar_view and avatar_view.is_loaded():
		if not speaking:
			avatar_view.set_base("audio", _zeros52)
		elif not _face_enabled and _cur_env.size() > 0 and _cur_play_dur_ms > 0:
			# face 未就绪: RMS 包络近似 jawOpen 驱动 3D 口型
			var arr := _zeros52.duplicate()
			var pos_ms := int(_tts_player.get_playback_position() * 1000.0)
			var idx := clampi(pos_ms / 20, 0, _cur_env.size() - 1)
			if _bs_jaw >= 0:
				arr[_bs_jaw] = clampf(_cur_env[idx] * 4.0, 0.0, 1.0)
			avatar_view.set_base("audio", arr)


# ── 句级 TTS 缓冲 + marker 解析 ──

func _flush_sentence(force: bool) -> void:
	# 切句 → 净句显示 (总是, 与 TTS 就绪无关) + 喂 TTS (仅就绪时)
	while not _sentence_buf.is_empty():
		var idx := _find_sentence_end(_sentence_buf)
		if idx < 0:
			if not force:
				break
			idx = _sentence_buf.length() - 1
		var sentence := _sentence_buf.substr(0, idx + 1)
		_sentence_buf = _sentence_buf.substr(idx + 1)
		var clean := _strip_markers(sentence).strip_edges()
		if clean.is_empty():
			continue
		if _current_bubble:
			_current_bubble.text += clean
		if tts and _tts_ready:
			tts.synthesize(clean)


func _find_sentence_end(s: String) -> int:
	# 中英文句末标点 + 换行; 返回首个句末字符下标, 无则 -1
	for i in range(s.length()):
		var c := s[i]
		if c == "。" or c == "！" or c == "？" or c == "；" or c == "!" or c == "?" or c == ";" or c == "\n":
			return i
	return -1


func _strip_markers(s: String) -> String:
	# 剥离内联表演 marker, 副作用应用情绪/语速; 返回净文本
	# [EMO:happy] / [EMO:happy:0.8] (强度可选 0~1) / [SPEED:1.2] — 大小写不敏感 (LLM 按 prompt 规约输出)
	var re := RegEx.new()
	re.compile("(?i)\\[(EMO|SPEED):([^\\]:]+)(?::([0-9.]+))?\\]")
	for m in re.search_all(s):
		var key := m.get_string(1).to_lower()
		var val := m.get_string(2)
		if key == "emo":
			var inten := m.get_string(3).to_float() if m.get_string(3) != "" else 0.7
			_apply_emotion(val, inten)
		elif key == "speed":
			var f := val.to_float()
			if f > 0.0 and tts:
				tts.set_speed(f)
	return re.sub(s, "", true)


func _apply_emotion(emo: String, intensity: float = 0.7) -> void:
	var e := emo.to_lower().strip_edges()
	if e.is_empty():
		return
	_avatar_emotion = e
	_avatar_emotion_intensity = clampf(intensity, 0.0, 1.0)
	if _avatar_emotion_lbl:
		_avatar_emotion_lbl.text = "情绪: %s %.2f" % [e, _avatar_emotion_intensity]
	# 表情驱动: avatar_view 内 emotion_layer → ARKit52 叠加 (3D 模式)
	if avatar_view and avatar_view.is_loaded():
		avatar_view.apply_emotion(e, _avatar_emotion_intensity)


# ── 气泡 ──

func _add_user_bubble(text: String) -> void:
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 0)
	# 右侧弹簧 (用户消息右对齐)
	var spacer_l := Control.new()
	spacer_l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spacer_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(spacer_l)
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _flat(USER_BG, 12, Color(1, 1, 1, 0.08), 1))
	panel.custom_minimum_size = Vector2(0, 0)
	panel.size_flags_horizontal = Control.SIZE_SHRINK_END
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	panel.add_child(margin)
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 14)
	label.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	margin.add_child(label)
	hbox.add_child(panel)
	_chat_box.add_child(hbox)


func _add_asst_bubble() -> RichTextLabel:
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 0)
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _flat(ASST_BG, 12, Color(1, 1, 1, 0.06), 1))
	panel.custom_minimum_size = Vector2(0, 0)
	panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	panel.add_child(margin)
	var rt := RichTextLabel.new()
	rt.bbcode_enabled = true
	rt.fit_content = true
	rt.scroll_following = false
	rt.add_theme_font_size_override("normal_font_size", 14)
	rt.add_theme_color_override("default_color", Color(0.92, 0.95, 1.0))
	rt.custom_minimum_size = Vector2(400, 0)
	rt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.add_child(rt)
	hbox.add_child(panel)
	# 右侧弹簧
	var spacer_r := Control.new()
	spacer_r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spacer_r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(spacer_r)
	_chat_box.add_child(hbox)
	_current_bubble_row = hbox
	return rt


func _add_reasoning_label() -> RichTextLabel:
	var rt := RichTextLabel.new()
	rt.bbcode_enabled = true
	rt.fit_content = true
	rt.scroll_following = false
	rt.add_theme_font_size_override("normal_font_size", 12)
	rt.add_theme_color_override("default_color", REASON_CLR)
	rt.custom_minimum_size = Vector2(0, 0)
	rt.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	rt.text = ""
	# 推理在回复之前: 插到 assistant 气泡行上方, 否则超长推理把回复顶到上面, 滚到底看不到回复
	var bidx := -1
	if _current_bubble_row and _current_bubble_row.is_inside_tree():
		bidx = _current_bubble_row.get_index()
	_chat_box.add_child(rt)
	if bidx >= 0:
		_chat_box.move_child(rt, bidx)
	return rt


func _add_tool_card(name: String, args_json: String) -> VBoxContainer:
	var card := VBoxContainer.new()
	card.add_theme_constant_override("separation", 4)
	# 外框: 黄色边框
	var panel := PanelContainer.new()
	panel.add_theme_stylebox_override("panel", _flat(TOOL_BG, 8, TOOL_BORDER, 1))
	panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 10)
	margin.add_theme_constant_override("margin_right", 10)
	margin.add_theme_constant_override("margin_top", 6)
	margin.add_theme_constant_override("margin_bottom", 6)
	panel.add_child(margin)
	var inner := VBoxContainer.new()
	inner.add_theme_constant_override("separation", 2)
	margin.add_child(inner)
	# 工具名
	var name_label := Label.new()
	name_label.text = "[%s]" % name
	name_label.add_theme_font_size_override("font_size", 13)
	name_label.add_theme_color_override("font_color", TOOL_BORDER)
	inner.add_child(name_label)
	# 参数 (截断)
	var args_label := Label.new()
	var display := args_json
	if display.length() > 300:
		display = display.substr(0, 300) + "..."
	args_label.text = display
	args_label.add_theme_font_size_override("font_size", 12)
	args_label.add_theme_color_override("font_color", Color(0.7, 0.74, 0.80))
	args_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	inner.add_child(args_label)
	card.add_child(panel)
	_chat_box.add_child(card)
	return inner


func _scroll_to_bottom() -> void:
	# 帧内合并: 流式高频调用只留一个 pending, 一帧后钉到滚动条绝对底部 (跟随最新内容)
	# 不用 ensure_control_visible: 气泡比视口高时它会停在气泡顶部, 新字被推到视口外看不见
	if _scroll_pending:
		return
	_scroll_pending = true
	await get_tree().process_frame
	_scroll_pending = false
	if is_instance_valid(_chat_scroll):
		_chat_scroll.scroll_vertical = _chat_scroll.get_v_scroll_bar().max_value


# ── 状态 ──

func _update_send_btn() -> void:
	if _generating:
		_send_btn.icon_kind = IconButton.Icon.STOP
		_send_btn.tooltip_text = "停止生成"
		_send_btn.add_theme_stylebox_override("normal", _flat(ERR_CLR, 6))
		_send_btn.add_theme_stylebox_override("hover", _flat(ERR_CLR.lightened(0.15), 6))
	else:
		_send_btn.icon_kind = IconButton.Icon.SEND
		_send_btn.tooltip_text = "发送 (Enter)"
		_send_btn.add_theme_stylebox_override("normal", _flat(ACCENT, 6))
		_send_btn.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 6))


func _update_status() -> void:
	if not _status_dot:
		return
	if _generating:
		_status_dot.text = "● 生成中"
		_status_dot.add_theme_color_override("font_color", WARN_CLR)
	elif _configured:
		_status_dot.text = "● 就绪 (%s)" % _current_config
		_status_dot.add_theme_color_override("font_color", OK_CLR)
	else:
		_status_dot.text = "● 未配置"
		_status_dot.add_theme_color_override("font_color", Color(0.5, 0.54, 0.60))


# ── 键盘 ──

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_ESCAPE and _settings_popup.visible:
			_settings_popup.visible = false
			_menu_open = false
			get_viewport().set_input_as_handled()


# ── 菜单 ──

func _on_file_menu(id: int) -> void:
	match id:
		0: _clear_chat()
		1: _back_home()
		2: get_tree().quit()


func _on_tool_menu(id: int) -> void:
	match id:
		0: _open_settings()


func _back_home() -> void:
	get_tree().change_scene_to_file("res://src/hub/main.tscn")


func _open_settings() -> void:
	_settings_popup.visible = true
	_menu_open = true


func _close_settings() -> void:
	_settings_popup.visible = false
	_menu_open = false


# ── UI 构建 ──

func _flat(bg: Color, radius: int = 0, border: Color = Color.TRANSPARENT, bw: int = 0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.set_corner_radius_all(radius)
	sb.border_color = border
	sb.set_border_width_all(bw)
	return sb


func _style_menu(mb: MenuButton) -> void:
	mb.focus_mode = Control.FOCUS_NONE
	mb.add_theme_font_size_override("font_size", 13)
	mb.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	mb.add_theme_color_override("font_hover_color", Color.WHITE)
	mb.add_theme_color_override("font_pressed_color", Color.WHITE)
	mb.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.12), 5))
	mb.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.10), 5))
	mb.add_theme_stylebox_override("focus", StyleBoxEmpty.new())


func _track_popup(pop: PopupMenu) -> void:
	pop.about_to_popup.connect(func(): _menu_open = true)
	pop.popup_hide.connect(func(): _menu_open = false)


func _build_ui() -> void:
	# 背景
	var bg := ColorRect.new()
	bg.color = BG_COLOR
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	# 左半: Agent 对话 (顶栏 + 对话区 + 输入栏); 右半: 虚拟人
	_left_panel = Control.new()
	_left_panel.anchor_left = 0.0
	_left_panel.anchor_top = 0.0
	_left_panel.anchor_right = 0.5
	_left_panel.anchor_bottom = 1.0
	add_child(_left_panel)
	_build_avatar_panel()

	# ── 顶部菜单栏 ──
	_top_bar = PanelContainer.new()
	_top_bar.set_anchors_preset(Control.PRESET_TOP_WIDE)
	_top_bar.offset_bottom = 36
	_top_bar.add_theme_stylebox_override("panel", _flat(Color(0.04, 0.05, 0.07, 0.90), 0, Color(1, 1, 1, 0.06), 1))
	_left_panel.add_child(_top_bar)
	var top_inner := MarginContainer.new()
	top_inner.add_theme_constant_override("margin_left", 8)
	top_inner.add_theme_constant_override("margin_right", 8)
	top_inner.add_theme_constant_override("margin_top", 4)
	top_inner.add_theme_constant_override("margin_bottom", 4)
	_top_bar.add_child(top_inner)
	var menu_bar := HBoxContainer.new()
	menu_bar.add_theme_constant_override("separation", 2)
	top_inner.add_child(menu_bar)
	# 文件
	var m_file := MenuButton.new()
	m_file.text = "文件"
	_style_menu(m_file)
	var pf := m_file.get_popup()
	pf.add_item("清空对话", 0)
	pf.add_item("返回主页", 1)
	pf.add_separator()
	pf.add_item("退出", 2)
	pf.id_pressed.connect(_on_file_menu)
	_track_popup(pf)
	menu_bar.add_child(m_file)
	# 工具
	var m_tool := MenuButton.new()
	m_tool.text = "工具"
	_style_menu(m_tool)
	var pt := m_tool.get_popup()
	pt.add_item("配置…", 0)
	pt.id_pressed.connect(_on_tool_menu)
	_track_popup(pt)
	menu_bar.add_child(m_tool)
	# 状态 (可点击直达设置; 颜色语义: 灰=未配置 绿=就绪 黄=生成中)
	_status_dot = Button.new()
	_status_dot.flat = true
	_status_dot.focus_mode = Control.FOCUS_NONE
	_status_dot.add_theme_font_size_override("font_size", 12)
	_status_dot.add_theme_color_override("font_color", Color(0.5, 0.54, 0.60))
	_status_dot.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.06), 5))
	_status_dot.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.04), 5))
	_status_dot.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_status_dot.tooltip_text = "点击打开配置"
	_status_dot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_status_dot.mouse_filter = Control.MOUSE_FILTER_STOP
	_status_dot.pressed.connect(_open_settings)
	menu_bar.add_child(_status_dot)
	# 配置选择
	_config_opt = OptionButton.new()
	_config_opt.custom_minimum_size = Vector2(140, 26)
	_config_opt.focus_mode = Control.FOCUS_NONE
	_config_opt.add_theme_font_size_override("font_size", 12)
	_config_opt.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	_config_opt.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	_config_opt.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.14), 6))
	_config_opt.add_theme_stylebox_override("pressed", _flat(Color(1, 1, 1, 0.10), 6))
	_config_opt.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_config_opt.item_selected.connect(_on_config_selected)
	menu_bar.add_child(_config_opt)

	# ── 对话区 ──
	_chat_scroll = ScrollContainer.new()
	_chat_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
	_chat_scroll.offset_top = 36
	_chat_scroll.offset_bottom = -52
	_chat_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_chat_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	_left_panel.add_child(_chat_scroll)
	_chat_box = VBoxContainer.new()
	_chat_box.add_theme_constant_override("separation", 8)
	_chat_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_chat_box.custom_minimum_size = Vector2(0, 0)
	# 对话区内边距
	var chat_margin := MarginContainer.new()
	chat_margin.add_theme_constant_override("margin_left", 24)
	chat_margin.add_theme_constant_override("margin_right", 24)
	chat_margin.add_theme_constant_override("margin_top", 12)
	chat_margin.add_theme_constant_override("margin_bottom", 12)
	chat_margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	chat_margin.add_child(_chat_box)
	_chat_scroll.add_child(chat_margin)

	# ── 底部输入栏 ──
	var input_bar := PanelContainer.new()
	input_bar.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	input_bar.offset_top = -52
	input_bar.add_theme_stylebox_override("panel", _flat(Color(0.04, 0.05, 0.07, 0.90), 0, Color(1, 1, 1, 0.06), 1))
	_left_panel.add_child(input_bar)
	var input_inner := MarginContainer.new()
	input_inner.add_theme_constant_override("margin_left", 16)
	input_inner.add_theme_constant_override("margin_right", 16)
	input_inner.add_theme_constant_override("margin_top", 10)
	input_inner.add_theme_constant_override("margin_bottom", 10)
	input_bar.add_child(input_inner)
	var input_hbox := HBoxContainer.new()
	input_hbox.add_theme_constant_override("separation", 10)
	input_inner.add_child(input_hbox)
	_input = LineEdit.new()
	_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_input.placeholder_text = "输入消息… (Enter 发送)"
	_input.add_theme_font_size_override("font_size", 14)
	_input.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	_input.add_theme_color_override("font_placeholder_color", Color(0.4, 0.44, 0.50))
	_input.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 8))
	_input.add_theme_stylebox_override("focus", _flat(Color(1, 1, 1, 0.10), 8, ACCENT, 1))
	_input.text_submitted.connect(func(_t): _send())
	input_hbox.add_child(_input)
	_send_btn = IconButton.new()
	_send_btn.icon_kind = IconButton.Icon.SEND
	_send_btn.icon_ratio = 0.42
	_send_btn.custom_minimum_size = Vector2(48, 32)
	_send_btn.focus_mode = Control.FOCUS_NONE
	_send_btn.add_theme_color_override("font_color", Color.WHITE)
	_send_btn.add_theme_color_override("font_hover_color", Color.WHITE)
	_send_btn.add_theme_color_override("font_pressed_color", Color.WHITE)
	_send_btn.add_theme_stylebox_override("normal", _flat(ACCENT, 6))
	_send_btn.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 6))
	_send_btn.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 6))
	_send_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_send_btn.pressed.connect(_send)
	input_hbox.add_child(_send_btn)

	# ── 设置弹窗 ──
	_settings_popup = PanelContainer.new()
	_settings_popup.visible = false
	_settings_popup.set_anchors_preset(Control.PRESET_CENTER)
	_settings_popup.custom_minimum_size = Vector2(480, 0)
	_settings_popup.add_theme_stylebox_override("panel", _flat(Color(0.05, 0.06, 0.08, 0.96), 12, Color(1, 1, 1, 0.12), 1))
	add_child(_settings_popup)
	var sp_inner := MarginContainer.new()
	sp_inner.add_theme_constant_override("margin_left", 20)
	sp_inner.add_theme_constant_override("margin_right", 20)
	sp_inner.add_theme_constant_override("margin_top", 16)
	sp_inner.add_theme_constant_override("margin_bottom", 16)
	_settings_popup.add_child(sp_inner)
	var sp_box := VBoxContainer.new()
	sp_box.add_theme_constant_override("separation", 10)
	sp_inner.add_child(sp_box)
	# 标题行
	var sp_head := HBoxContainer.new()
	sp_box.add_child(sp_head)
	var sp_title := Label.new()
	sp_title.text = "Agent 配置"
	sp_title.add_theme_font_size_override("font_size", 15)
	sp_title.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	sp_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sp_head.add_child(sp_title)
	var sp_close := Button.new()
	sp_close.text = "✕"
	sp_close.custom_minimum_size = Vector2(30, 26)
	sp_close.focus_mode = Control.FOCUS_NONE
	sp_close.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	sp_close.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.16), 6))
	sp_close.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	sp_close.add_theme_color_override("font_color", Color(0.85, 0.9, 0.95))
	sp_close.pressed.connect(_close_settings)
	sp_head.add_child(sp_close)
	# 手动配置区
	var section := Label.new()
	section.text = "手动配置 (覆盖配置文件)"
	section.add_theme_font_size_override("font_size", 12)
	section.add_theme_color_override("font_color", Color(0.5, 0.54, 0.60))
	sp_box.add_child(section)
	# URL
	_url_edit = _add_field(sp_box, "URL", "https://api.openai.com/v1")
	# API Key
	_key_edit = _add_field(sp_box, "API Key", "sk-...", true)
	# Model
	_model_edit = _add_field(sp_box, "模型", "gpt-4o")
	# 系统提示词
	var sys_row := VBoxContainer.new()
	sys_row.add_theme_constant_override("separation", 4)
	sp_box.add_child(sys_row)
	var sys_lbl := Label.new()
	sys_lbl.text = "系统提示词"
	sys_lbl.add_theme_font_size_override("font_size", 12)
	sys_lbl.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	sys_row.add_child(sys_lbl)
	_sys_edit = TextEdit.new()
	_sys_edit.custom_minimum_size = Vector2(0, 80)
	_sys_edit.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_sys_edit.add_theme_font_size_override("font_size", 13)
	_sys_edit.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	_sys_edit.add_theme_color_override("background_color", Color(1, 1, 1, 0.04))
	_sys_edit.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	_sys_edit.add_theme_stylebox_override("focus", _flat(Color(1, 1, 1, 0.10), 6, ACCENT, 1))
	sys_row.add_child(_sys_edit)
	# 应用按钮
	var btn_row := HBoxContainer.new()
	btn_row.alignment = BoxContainer.ALIGNMENT_END
	btn_row.add_theme_constant_override("separation", 8)
	sp_box.add_child(btn_row)
	var apply_btn := Button.new()
	apply_btn.text = "应用"
	apply_btn.custom_minimum_size = Vector2(72, 30)
	apply_btn.add_theme_stylebox_override("normal", _flat(ACCENT, 6))
	apply_btn.add_theme_stylebox_override("hover", _flat(Color(ACCENT, 0.85), 6))
	apply_btn.add_theme_stylebox_override("pressed", _flat(Color(ACCENT, 0.7), 6))
	apply_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	apply_btn.add_theme_color_override("font_color", Color.WHITE)
	apply_btn.pressed.connect(func():
		_on_manual_configure()
		_apply_system_prompt()
		_close_settings())
	btn_row.add_child(apply_btn)
	var cancel_btn := Button.new()
	cancel_btn.text = "取消"
	cancel_btn.custom_minimum_size = Vector2(72, 30)
	cancel_btn.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.08), 6))
	cancel_btn.add_theme_stylebox_override("hover", _flat(Color(1, 1, 1, 0.16), 6))
	cancel_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	cancel_btn.add_theme_color_override("font_color", Color(0.9, 0.93, 0.97))
	cancel_btn.pressed.connect(_close_settings)
	btn_row.add_child(cancel_btn)

	# ── Toast ──
	_toast = Label.new()
	_toast.add_theme_font_size_override("font_size", 13)
	_toast.add_theme_color_override("font_color", Color(1.0, 0.85, 0.55))
	_toast.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.7))
	_toast.add_theme_constant_override("shadow_offset_y", 1)
	_toast.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_toast.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	_toast.offset_top = -90
	_toast.offset_bottom = -62
	_toast.modulate.a = 0.0
	_toast.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_toast)
	_show_chat_empty()   # 初始空态引导


func _build_avatar_panel() -> void:
	# 右半: 虚拟人 (Phase 1 占位简笔角色 + RMS 口型 + TTS 播放; VRM 留后续)
	_avatar_panel = Control.new()
	_avatar_panel.anchor_left = 0.5
	_avatar_panel.anchor_top = 0.0
	_avatar_panel.anchor_right = 1.0
	_avatar_panel.anchor_bottom = 1.0
	add_child(_avatar_panel)
	var av_bg := ColorRect.new()
	av_bg.color = Color(0.06, 0.075, 0.105, 1.0)
	av_bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	av_bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_avatar_panel.add_child(av_bg)
	var av_div := ColorRect.new()
	av_div.color = Color(1, 1, 1, 0.06)
	av_div.set_anchors_preset(Control.PRESET_LEFT_WIDE)
	av_div.offset_right = 1
	av_div.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_avatar_panel.add_child(av_div)
	# 3D avatar 视图 (最底层; 加载成功 → 隐藏 2D 简笔脸, 语音/情绪驱动走 AvatarDriver)
	avatar_view = AvatarView.new()
	avatar_view.set_anchors_preset(Control.PRESET_FULL_RECT)
	avatar_view.model_loaded.connect(_on_avatar_model_loaded)
	_avatar_panel.add_child(avatar_view)
	for p in AVATAR_PATHS:
		if avatar_view.load_model(p):
			break
	# 音频播放器: 整句 AudioStreamWAV 顺序播放, finished → 下一句
	_tts_player = AudioStreamPlayer.new()
	_tts_player.finished.connect(_play_next)
	_avatar_panel.add_child(_tts_player)
	# 角色居中
	var cc := CenterContainer.new()
	cc.set_anchors_preset(Control.PRESET_FULL_RECT)
	cc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_avatar_panel.add_child(cc)
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 18)
	col.alignment = BoxContainer.ALIGNMENT_CENTER
	cc.add_child(col)
	_avatar_2d_col = col
	# 脸部 (扁平矢量风: 深底描边圆 + 圆眼 + ACCENT 圆角口型; 嘴 scale.y 随 RMS 开合)
	var face := PanelContainer.new()
	face.custom_minimum_size = Vector2(240, 240)
	face.add_theme_stylebox_override("panel", _flat(Color(0.055, 0.07, 0.10), 120, Color(ACCENT, 0.55), 2))
	col.add_child(face)
	var mug := Control.new()
	mug.set_anchors_preset(Control.PRESET_FULL_RECT)
	mug.mouse_filter = Control.MOUSE_FILTER_IGNORE
	face.add_child(mug)
	var eye_sb := _flat(Color(0.92, 0.95, 1.0, 0.92), 9)
	var eye_l := Panel.new()
	eye_l.add_theme_stylebox_override("panel", eye_sb)
	eye_l.position = Vector2(66, 84)
	eye_l.size = Vector2(24, 30)
	eye_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	mug.add_child(eye_l)
	var eye_r := Panel.new()
	eye_r.add_theme_stylebox_override("panel", eye_sb)
	eye_r.position = Vector2(150, 84)
	eye_r.size = Vector2(24, 30)
	eye_r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	mug.add_child(eye_r)
	_avatar_mouth = Panel.new()
	var mouth_sb := _flat(ACCENT, 10)
	_avatar_mouth.add_theme_stylebox_override("panel", mouth_sb)
	_avatar_mouth.position = Vector2(80, 150)
	_avatar_mouth.size = Vector2(80, 44)
	_avatar_mouth.pivot_offset = Vector2(40, 22)   # 中心, scale.y 以此缩放
	_avatar_mouth.scale = Vector2(1.0, 0.15)       # 静止: 闭合细条
	_avatar_mouth.mouse_filter = Control.MOUSE_FILTER_IGNORE
	mug.add_child(_avatar_mouth)
	# 情绪标签 (set_emotion / [EMO:..] 更新; 钉面板左上, 3D 模式下依然可见)
	_avatar_emotion_lbl = Label.new()
	_avatar_emotion_lbl.text = "情绪: neutral"
	_avatar_emotion_lbl.add_theme_font_size_override("font_size", 14)
	_avatar_emotion_lbl.add_theme_color_override("font_color", Color(0.7, 0.74, 0.80))
	_avatar_emotion_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	_avatar_emotion_lbl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_avatar_emotion_lbl.position = Vector2(14, 42)
	_avatar_panel.add_child(_avatar_emotion_lbl)
	var note := Label.new()
	note.text = "语音驱动 · 口型同步"
	note.add_theme_font_size_override("font_size", 12)
	note.add_theme_color_override("font_color", Color(0.5, 0.54, 0.60))
	note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	note.mouse_filter = Control.MOUSE_FILTER_IGNORE
	col.add_child(note)
	# model_loaded 信号在建 col 之前已发过 (load_model 在本函数前段), 这里补一次隐藏判断
	if avatar_view and avatar_view.is_loaded() and _avatar_2d_col:
		_avatar_2d_col.visible = false


func _add_field(parent: VBoxContainer, label: String, placeholder: String, secret: bool = false) -> LineEdit:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	parent.add_child(row)
	var lbl := Label.new()
	lbl.text = label
	lbl.custom_minimum_size = Vector2(72, 0)
	lbl.add_theme_font_size_override("font_size", 12)
	lbl.add_theme_color_override("font_color", Color(0.6, 0.64, 0.69))
	row.add_child(lbl)
	var edit := LineEdit.new()
	edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	edit.placeholder_text = placeholder
	edit.secret = secret
	edit.add_theme_font_size_override("font_size", 13)
	edit.add_theme_color_override("font_color", Color(0.92, 0.95, 1.0))
	edit.add_theme_color_override("font_placeholder_color", Color(0.4, 0.44, 0.50))
	edit.add_theme_stylebox_override("normal", _flat(Color(1, 1, 1, 0.06), 6))
	edit.add_theme_stylebox_override("focus", _flat(Color(1, 1, 1, 0.10), 6, ACCENT, 1))
	row.add_child(edit)
	return edit


func _toast_msg(msg: String) -> void:
	_toast.text = msg
	_toast.modulate.a = 1.0
	var tw := create_tween()
	tw.tween_interval(2.5)
	tw.tween_property(_toast, "modulate:a", 0.0, 0.4)
