# tools/tests — headless 回归用例

工具箱项目内的无头测试脚本，全部走 `SceneTree` + `-s`，不开窗口、不依赖 GUI，
**退出码 0=通过 / 1=失败或超时**，可直接串进 CI 或本地波次回归。

判定行约定与汇总见 [script/testenv/README.md](../../../../script/testenv/README.md)
（`python script/testenv/collect_verdicts.py -`）。

## 前提

- `addons/avox_godot` 已部署（`platform/godot/deploy_godot.ps1`），`bin/` 有对应平台产物。
- 涉及动态插件的用例还需 `bin/plugins/` 里有对应 so/dll（`avox_webrtc`、`avox_torrent`）。
- 本机 godot 可执行路径按实际替换，下文用 `godot` 代指。

## 用例

| 脚本 | 覆盖 | 判定行 | 额外依赖 |
|------|------|--------|----------|
| `test_url.gd` | 任意 URL → `MediaPlayer` 起播 → PLAYING 且进度推进 | `case=url-<url清洗>` | 流源（`push_streams.py`）|
| `test_rtc.gd` | HTTP 信令 → `RtcPlayer` → `first_video_frame` | `case=rtc-<url清洗>` | `avox_webrtc` 插件 + ZLM |
| `test_probe.gd` | 磁力探测 → 文件列表 → 选片 → 起播 → 进度推进 | 仅 stdout `PASS`/`FAIL` 文本 | `avox_torrent` 插件 + 外网 tracker |
| `test_retarget_check.gd` | `retarget.gd` 骨骼重定向回归（"双脚向上" bug） | 仅 stdout 逐项 `OK`/`FAIL` | `src/avatar/avatar.gltf` |

> `test_probe.gd` / `test_retarget_check.gd` 尚未输出 `[AVOX][TEST]` 统一判定行，
> `collect_verdicts.py` 汇总不到，只能看退出码。

## 运行

```bash
# URL 起播（协议/容器矩阵主力）
godot --headless --path platform/godot/tools -s res://tests/test_url.gd -- \
  rtsp://127.0.0.1:554/live/avox264 --timeout-ms=25000

# WebRTC 拉流（--case 可覆盖自动派生的 case id）
godot --headless --path platform/godot/tools -s res://tests/test_rtc.gd -- \
  "http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play" --case=rtc-h264

# torrent 全链路（磁力硬编码在脚本内，依赖外网）
godot --headless --path platform/godot/tools -s res://tests/test_probe.gd

# 骨骼重定向回归（纯合成 landmark，不需视频）
godot --headless --path platform/godot/tools --script res://tests/test_retarget_check.gd
```

`test_url.gd` / `test_rtc.gd` 的通用参数：`<url>` 位置参数，`--timeout-ms=<ms>`（默认 30000）、
`--case=<id>`（默认从 URL 清洗派生）。

## 约定

- 新用例放本目录，命名 `test_<主题>.gd`，首行注释写清运行命令与退出码。
- 输出统一判定行 `[AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]`，case id 稳定可复现。
- 只写一次的排障脚本不入库；要留证据请把结论记到 [doc/test/功能测试矩阵.md](../../../../doc/test/功能测试矩阵.md)。
- 本目录已在 `export_presets.cfg` 的 `exclude_filter` 中排除，不会打进 APK / exe。
