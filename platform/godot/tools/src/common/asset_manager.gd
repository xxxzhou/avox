extends Node
## AssetManager —— avox 运行时资源检查 / 下载 (autoload)
## 唯一权威 = 部署根 assets/script/assets_manifest.json; 检查与下载都经 fetch_assets.py。
## 选 autoload 而非场景子节点: change_scene_to_file 会释放场景, 下载中的 Thread 不能随之销毁。
## 部署根 (含 avox.dll/assets/plugins/script/):
##   编辑器期 = addons/avox_godot/bin (junction → avox 安装树);
##   导出发布版 = exe 同目录 (avox.dll 等扁平铺在 exe 旁, res://addons/avox_godot/bin 在 pck 内不可用)。

signal check_failed(msg: String)
signal download_started(item_id: String)
signal download_progress(item_id: String, pct: float, name: String)
signal download_finished(item_id: String, ok: bool, msg: String)

## 语音输入依赖的 STT 流式模型 (manifest id)
const VOICE_MODEL_ID := "sherpa_zh_en"
const MANIFEST_REL := "assets/script/assets_manifest.json"
const FETCH_REL := "assets/script/fetch_assets.py"

var _root := ""        # 部署根 (含 avox.dll 的目录)
var _fetch := ""       # <root>/assets/script/fetch_assets.py
var _py := ""          # "python" 或 "py"
var _checked := {}     # item_id -> {id,name,plugin,type,method,dest,ready,missing_files,files}
var _downloading := false
var _thread: Thread
var _thread_item := ""
var _progress_file := ""   # fetch.py 写进度的 JSONL (globalize 自 user://)
var _last_pct := 0.0
var _last_name := ""
var _last_poll_ms := 0

func _ready() -> void:
	# 编辑器: bin junction → avox 安装树; 导出发布版: exe 同目录 (res:// 在 pck 内不可达)
	if OS.has_feature("editor"):
		_root = ProjectSettings.globalize_path("res://addons/avox_godot/bin")
	else:
		_root = OS.get_executable_path().get_base_dir()
	_fetch = _root.path_join(FETCH_REL)
	if not FileAccess.file_exists(_fetch):
		push_warning("AssetManager: 找不到 %s (插件未部署? 先运行 plugin/deploy_godot.ps1), 资源检查不可用" % _fetch)
	_find_python()

func _find_python() -> void:
	for cand in ["python", "py"]:
		var out := []
		if OS.execute(cand, PackedStringArray(["-c", "import sys"]), out, false, false) == 0:
			_py = cand
			return
	push_warning("AssetManager: 未找到 python/py, 资源下载不可用")

func _ready_to_run() -> bool:
	return _py != "" and FileAccess.file_exists(_fetch)

# ── 检查 (同步, ~200ms; 仅在 _ready / 下载完成后调) ──
func check_all() -> Dictionary:
	## 跑 fetch_assets.py check --json, 返回 {platform, installed_plugins, items}, 并缓存 items
	if not _ready_to_run():
		return {}
	var out := []
	var code := OS.execute(_py, PackedStringArray([
		_fetch, "check", "--json", "--no-color", "--root", _root]), out, false, false)
	if code != 0:
		emit_signal("check_failed", "exit=%d" % code)
		return {}
	# fetch 的进度/状态全写 stderr, stdout 是纯 JSON (多行 indent=2)
	var parsed: Variant = JSON.parse_string("\n".join(out))
	if parsed is Dictionary:
		_cache_items(parsed.get("items", []))
		return parsed
	return {}

func _cache_items(items: Array) -> void:
	for it in items:
		if it is Dictionary and it.has("id"):
			_checked[it["id"]] = it

func check_item(item_id: String, refresh := false) -> Dictionary:
	if refresh or not _checked.has(item_id):
		check_all()
	return _checked.get(item_id, {})

func items() -> Array:
	## 全部已缓存 item (按 manifest 顺序)
	return _checked.values()

func is_downloading(item_id: String) -> bool:
	return _downloading and _thread_item == item_id

# ── 下载 (后台线程, 不卡 UI) ──
func download_item(item_id: String) -> bool:
	if _downloading or not _ready_to_run():
		return false
	_downloading = true
	_thread_item = item_id
	# 进度文件: fetch.py 边下边写 JSONL, _process 轮询取最新 prog 行发 download_progress
	_progress_file = ProjectSettings.globalize_path("user://avox_dl_progress.jsonl")
	DirAccess.remove_absolute(_progress_file)
	_last_pct = 0.0
	_last_name = ""
	emit_signal("download_started", item_id)
	_thread = Thread.new()
	_thread.start(_worker.bind(item_id))
	return true

func _worker(item_id: String) -> Dictionary:
	# 后台线程: 只调 OS.execute + 本地数组, 不碰场景/引擎 API (wait_to_finish 取回)
	var args := PackedStringArray([
		_fetch, "download", "--select", item_id, "--root", _root, "--json", "--no-color"])
	if _progress_file != "":
		args.append("--progress-file")
		args.append(_progress_file)
	var out := []
	var code := OS.execute(_py, args, out, false, false)
	return {"id": item_id, "code": code, "out": out}

func _process(_delta: float) -> void:
	# 下载中: 轮询 fetch.py 写的进度文件, 发 download_progress
	if _downloading and _progress_file != "":
		_poll_progress()
	if _thread == null or _thread.is_alive():
		return
	var res: Dictionary = _thread.wait_to_finish()
	_thread = null
	var id := String(res.get("id", ""))
	_downloading = false
	_thread_item = ""
	_checked.erase(id)   # 强制下次重新检查
	# 清进度文件 + 复位
	if _progress_file != "":
		DirAccess.remove_absolute(_progress_file)
		_progress_file = ""
	_last_pct = 0.0
	_last_name = ""
	var code: int = res.get("code", -1)
	if code == 0:
		var parsed: Variant = JSON.parse_string("\n".join(res.get("out", [])))
		if parsed is Dictionary and parsed.get("results", []) is Array:
			var results: Array = parsed["results"]
			if results.size() > 0 and results[0] is Dictionary:
				var r: Dictionary = results[0]
				emit_signal("download_finished", id, bool(r.get("ok", false)), String(r.get("msg", "")))
				return
		emit_signal("download_finished", id, true, "完成")
	else:
		emit_signal("download_finished", id, false, "下载失败 (exit=%d)" % code)

func _poll_progress() -> void:
	# 每 100ms 读一次进度文件, 取最后一个含 pct 的事件发信号 (fetch.py 0.25s 写一条)
	var now := Time.get_ticks_msec()
	if now - _last_poll_ms < 100:
		return
	_last_poll_ms = now
	if not FileAccess.file_exists(_progress_file):
		return
	var f := FileAccess.open(_progress_file, FileAccess.READ)
	if f == null:
		return
	var lines := f.get_as_text().split("\n", false)
	f.close()
	for i in range(lines.size() - 1, -1, -1):
		var line := lines[i].strip_edges()
		if line.is_empty():
			continue
		var p: Variant = JSON.parse_string(line)
		if p is Dictionary and p.has("pct"):
			var pct := float(p["pct"])
			var name_ := String(p.get("name", ""))
			if pct != _last_pct or name_ != _last_name:
				_last_pct = pct
				_last_name = name_
				emit_signal("download_progress", _thread_item, pct, name_)
			return


func _exit_tree() -> void:
	# 退出时若下载线程仍在跑, join 等它结束 (168MB 可能阻塞, v1 接受)
	if _thread != null and _thread.is_started():
		_thread.wait_to_finish()
