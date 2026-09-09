# testenv/ — 功能测试本地流源套件

复用本机已运行的 ZLMediaKit MediaServer（`MediaServer.exe`，http/80 rtsp/554 rtmp/1935 rtc/8000），
用 ffmpeg 循环推流产生多协议测试源，并为所有平台的自动化回归提供**统一判定行约定**。

## 快速开始

```bash
python script/testenv/push_streams.py            # 推 live/avox(H265) + live/avox264(H264) 两路
python script/testenv/push_streams.py --status   # 查看在线媒体
python script/testenv/push_streams.py --stop     # 停流
python script/testenv/push_streams.py --all      # assets/video 全部样本 (含 sherpa.mp4)
```

ZLM secret 查找顺序: `--secret` > 环境变量 `ZLM_SECRET` > 常见 MediaServer `config.ini` 路径嗅探。

推好后可用的拉流地址（已实测）：

| 协议 | URL (本机) |
|------|------------|
| RTSP | `rtsp://127.0.0.1:554/live/avox` · `.../live/avox264` |
| RTMP | `rtmp://127.0.0.1:1935/live/avox` |
| HLS | `http://127.0.0.1/live/avox/hls.m3u8` |
| HTTP-TS | `http://127.0.0.1/live/avox.live.ts` |
| ZLM WebRTC 信令 | `http://127.0.0.1/index/api/webrtc?app=live&stream=avox&type=play` |

**手机等局域网设备**拉流时把 `127.0.0.1` 换成本机局域网 IP，推流时加 `--lan-ip=<IP>` 让判定行直接输出可拉地址。

## 判定行约定 (基建②)

所有自动化出口打印一行：

```
[AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]
```

- case id 稳定可复现（推荐从 URL/用例名派生），同一 case 重跑取最新结果。
- 桌面: stdout / godot log；Android: `logcat`（Godot print）；iOS: Console；Unity/UE: Editor.log。

汇总：

```bash
adb logcat -d | python script/testenv/collect_verdicts.py -         # Android
python script/testenv/collect_verdicts.py logs/*.log --out m.md     # 文件
```

## 各平台驱动方式

### Windows / 桌面 Godot（headless，已验证）

```bash
D:/Work/godot/godot.exe --headless --path platform/godot/tools -s res://test_url.gd -- \
  rtsp://127.0.0.1:554/live/avox264 --timeout-ms=25000 | python script/testenv/collect_verdicts.py -
```

`test_url.gd`: 任意 URL 起播 → PLAYING 且进度推进 → PASS，退出码 0/1。
工具箱 UI 亦支持 CLI 直开: `godot --path tools -- <url|avox://url|magnet:...>`，`-- --ui=live` 截图走查。

### Android 真机（本机 adb）

APK 需含 avox:// 深链（`plugin/patch_apk.py` 已支持注入，重出包生效）：

```bash
adb install -r platform/godot/tools/avox_tools_debug.apk
adb shell am start -a android.intent.action.VIEW -d "avox://rtsp://<局域网IP>:554/live/avox264"
adb logcat -d | python script/testenv/collect_verdicts.py -      # 工具箱 main.gd 自带判定行
```

`avox://` 壳内可包 rtsp/rtmp/http/文件路径（直接播）或 `avox://magnet:?...`（走解析流程）。

### torrent

headless 全链路已有 `platform/godot/tools/test_probe.gd`（探测→选文件→起播）；
分步基准 `script/torrent/torrent_bench.py`。

## 判定行发出点（现状）

| 发出点 | case | 触发 |
|--------|------|------|
| `push_streams.py` | `push-<流>-<协议>` / `push-stop` | 推流就绪/停流 |
| `test_url.gd` | `url-<url清洗>` | headless 起播 |
| 工具箱 `main.gd` | `url-play` | 任意播放首次 PLAYING / IO·解码错误（logcat 可抓） |
