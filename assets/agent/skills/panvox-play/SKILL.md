---
name: panvox-play
description: panvox 播放问题端到端排查:用户报"某平台+某片源+某现象"(打不开/卡顿/花屏/无声/字幕/崩溃)时加载。自动定位目标设备(本机/ssh mac/ssh pc/adb),从 panvox 数据缓存定位片源并推导可播 URL,带 -Log 复现,按日志行定位根因(源端/网络/app/引擎)。仅 panvox 应用问题用本 skill;手头只有一条裸 URL 要验播放走 avox-cli。
whenToUse: 用户描述 panvox 应用内问题(某源打不开/播放卡/字幕异常/投屏/崩溃等)且需实际复现取日志时。用户已给日志路径只要分析→analyze-log;只要截图找字点击→avox-cli。
---

# panvox 播放问题排查

## 0. 前置事实
- **双仓同级**(panvox 宿主, avox 引擎): Windows `D:\Work\github\{panvox,avox}`; Mac 构建盘 `/Volumes/PSSD/work/github/`(另有部署克隆 `~/development/panvox`); Linux/WSL `~/github/`。全平台编译/部署配方在 panvox 仓 `docs/avox-build-and-deploy.md`(启动闸/部署脚本/新鲜度闸门都在里面, 需要重编先读它 §2/§3)。
- **通道**: 本机通常是 Windows 开发机; `ssh mac` / `ssh pc` 双向免密; Android 真机经 Windows `adb`; iOS 模拟器经 Mac `xcrun simctl`; **Linux 环境 = Windows 本机里的 WSL(Ubuntu), 不是独立目标机**, 经 `wsl bash -c '...'` 进场(无 ssh), 仓库在 WSL 内 `~/github/{panvox,avox}`。动手先确认自己落在哪台(uname), 目标≠本机就过 SSH/adb, **Windows 目标过 ssh 起 GUI app 会落在不可见会话**——app 级复现让用户手起, 引擎级复现走免窗的 engine_play_test。
- **数据目录**: Windows `%APPDATA%\panvox\`; Mac `~/Library/Application Support/com.panvox.panvox/`。关键文件: `sources.json`(源配置: id/kind/origin/user/pass/root/token —— **明文凭据**)、`history.json`(键=源id+路径, 值=title/position/duration/updated)、`local_library.json`(文件清单)、`unplayable.json`(打不开下墙记录)、`freeze/`(冻结名片)。

## 1. 流程(五步)
1. **三要素**: 平台**不指明=当前机器**(本机直查, 不走 SSH/adb; 用户点名别的平台才切通道)。片源(哪个源哪部片, 文件名)与现象(打不开/卡顿/花屏/无声/音画不同步/字幕/崩溃 + 大概时刻)缺了先问; 片源模糊可先拿 `history.json` 最近条目猜并跟用户确认, 别空手反问。
2. **定片源**: 读目标机 `history.json` 最近条目 + `sources.json` 映射出 kind/origin/路径。可播 URL 推导: webdav/http = `origin+root+path`(路径段 URL 编码); smb = UNC `\\origin\共享\路径`; 本地 = 直接路径; **jellyfin/emby/云盘/IPTV 不手拼 URL**(需 token/接口), 驱动 app 内复现或向用户要直链。
3. **带日志复现**(§2, 按平台)。复现前**单实例检查**: Windows `tasklist | grep -i panvox`、Mac `pgrep -x panvox` —— 多实例共用数据目录互覆快照, 会出假象; 有实例先让用户关或 kill。
4. **按用户描述触发**: 能自动化就自动化 —— `PANVOX_AUTOPLAY=<可播URL或路径>` 环境变量让 app 启动后直接播该条(免手点; Windows cmd: `set PANVOX_AUTOPLAY=... && start panvox.exe -Log`); 需要真实 UI 操作(切轨/倍速/字幕切换)在 Windows 本机可加载 avox-cli skill 用 ops 找字点击。
5. **读日志分析**(§3) → 归属判定 → 结论 + 建议; 修复后同参数重跑复现路径才算闭案。

## 2. 各平台带日志复现
**Windows**(本机或 ssh pc):
- 首选启动闸(先哈希同步引擎 dll, 堵"宿主跑旧引擎"): `panvox.cmd -Log` → stdout/stderr 落 `%APPDATA%\panvox\logs\panvox-<ts>.log`(+.err, 自动留最近 20 份)。
- exe 直启: `panvox.exe -Log` → 进程内档 `panvox-log-<ts>.log`([dart]/[info]..[debug] 前缀)。**无 -Log 参数零日志**(9/26 门控定稿), 老构建没有此开关。
- 崩溃: Release 目录(`app\build\windows\x64\runner\Release\`) `AVOX_*.dmp`(app 自带 handler, mtime≈崩溃时刻), `python tools/analyze_dump.py <dmp>`; 疑冻结看 `%APPDATA%\panvox\freeze\`。
- 排 crash 需带符号引擎: `AVOX_BUILD_TYPE=RelWithDebInfo python build_windows.py`(avox 仓) + `tools\deploy_runtime.ps1 -EngineConfig RelWithDebInfo`(见部署文档 §5)。

**Mac**(ssh mac): shim 无 -Log 开关, 靠终端捕获+os_log。
- 终端起收 Dart stdout: `nohup ~/Applications/panvox.app/Contents/MacOS/panvox >/tmp/panvox-run.log 2>&1`。
- 引擎日志走统一日志: `log show --last 10m --predicate 'process == "panvox"' --info`, 或 `log stream` 边播边收。
- 换引擎免重打包: `bash tools/deploy_macos_shim_app.sh`(部署文档 §3.3)。

**Android**(adb): `adb install -r app/build/app/outputs/flutter-apk/app-release.apk`; `adb logcat -c && adb logcat -v time -s avox:V flutter:V`(引擎 tag=avox, Dart tag=flutter); 手机在蜂窝网"全源打不开"先 `adb shell svc wifi enable`(历史陷阱)。

**iOS**: 模拟器 `xcrun simctl launch --console-pty booted com.panvox.panvox` 收 stdout; 真机无控制台, 依赖用户复述+ repro 降级到 Mac/Windows。

**Linux/WSL**(= Windows 本机里的 WSL Ubuntu, 经 `wsl bash -c` 进场): 仓库在 WSL 内 `~/github/{panvox,avox}`; 起 app `~/github/panvox/app/build/linux/x64/release/bundle/panvox 2>&1 | tee /tmp/panvox-run.log`, **必须普通用户**起(WSLg 下 root 连不上用户 Wayland socket)。wsl.exe 实操: 复杂命令写 .sh 进去跑(引号经 Windows 层易被吃), 路径用 wslpath 转, bash 脚本忌 CRLF, **wsl.exe 的 stderr 提示常是乱码**(编码问题), 以命令正常输出为准别被它带偏。画面恒走 frame_poll CPU 车道, 与 Win/mac 硬解车道表现不可直接互推; 引擎构建车道归夜班 openclaw 会话, 动手前 `ps aux | grep build_linux` 确认没人编别抢树。

## 3. 日志分析
- 按目录 mtime 取最新文件读; **严禁全盘/递归搜索日志**(必超时)。大文件先看头尾定时间戳格式, 再按用户说的时刻 grep 时间窗。
- 引擎行([MPx] 实例前缀 / [FF] ffmpeg / [ZL] zlmediakit / io create-open result / addVideoDesc-addAudioDesc / state from X to Y / playing↔buffering / av not align)与 **analyze-log skill 同一套判定表**, 卡顿/打不开的细判直接加载它, 不在本 skill 重复。
- panvox 层特征行: `[panvox-boot]`(启动顺序), `[panvox-shim]`/`[panvox-runner]`(shim/GPU 适配器), `[dart]`(-Log 进程内档的 Dart 行), `[avox][级别]`(引擎桥行), `watchdog armed`(冻结名片机制), unplayable 记录(打不开自动下墙: 引擎败+文件头非容器才标, **网络类错误永不标**, 见 unplayable.json)。
- 归属判定顺序: ①io open 失败/超时/403/404/503 → 源或网络(签名限时源失效**严禁同参自动重播**); ②流信息缺/decode create fail → 编码不支持或硬解问题; ③opening 长停 → IO 慢/假成功; ④buffering 频繁 → 查队列与丢包行; ⑤Dart exception/zone-error → app 层逻辑; ⑥无日志直接退 → 走 dmp 流程。
- 需脱离 app 隔离引擎时: `tools/engine_play_test.exe`(须与 avox.dll 同目录, tools/build_engine_play_test.bat 出; `engine_play_test <url> [sec=8] [hard=1] [vulk=1]`, 免窗可过 ssh); 或 avox_cli play(加载 avox-cli, -io ffmpeg / -transport tcp / -log-packet / -log-decode)。
- 细化旋钮: `PANVOX_THUMB_TRACE=1`(抽帧链); FFmpeg 桥默认压在 WARNING, info 级流信息默认没有, 引擎侧需另开。

## 红线
- **凭据脱敏**: sources.json 的 user/pass/token、URL 里的 session/sign 不进结论、不复述、不回显。
- 单实例纪律; avox 构建树有并行会话在用别抢(尤其 WSL 车道)。
- 用户数据目录默认只读; 重扫/清缓存/删 unplayable 等写操作先征得用户同意。
- 结论必须附证据行(时间戳+关键行内容), 找不到证据行就明说未复现, 严禁臆测补链。
