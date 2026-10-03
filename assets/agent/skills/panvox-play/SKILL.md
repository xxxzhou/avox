---
name: panvox-play
description: panvox 播放问题端到端排查:用户报"某平台+某片源+某现象"(打不开/卡顿/花屏/无声/字幕/崩溃)时加载。自动定位目标设备(本机/ssh mac/ssh pc/adb),从 panvox 数据缓存定位片源并先验真身,带 -Log 复现(对齐用户实际续播路径),按日志行先对已知病族索引再定根因(源端/网络/app/引擎)。**§4 只是一行式病族索引(签名→结论→修态),根因/判据/验收在 `references/病族-{open,seek,播放中}.md` 按需读**。仅 panvox 应用问题用本 skill;手头只有一条裸 URL 要验播放走 avox-cli。
whenToUse: 用户描述 panvox 应用内问题(某源打不开/播放卡/字幕异常/投屏/崩溃等)且需实际复现取日志时。用户已给日志路径只要分析→analyze-log;只要截图找字点击→avox-cli。
---

# panvox 播放问题排查

## 0. 前置事实
- **双仓同级**(panvox 宿主 / avox 引擎): Win `D:\Work\github\{panvox,avox}`; Mac `/Volumes/PSSD/work/github/`(另有部署克隆 `~/development/panvox`); Linux/WSL `~/github/`。编译/部署配方见 panvox 仓 `docs/avox-build-and-deploy.md`(启动闸/部署脚本/新鲜度闸门, 要重编先读 §2/§3)。
- **通道**: 本机通常是 Win 开发机; `ssh mac`/`ssh pc` 双向免密; Android 真机经 Win `adb`; iOS 模拟器经 Mac `xcrun simctl`; **Linux = Win 本机里的 WSL(Ubuntu), 不是独立目标机**, 经 `wsl bash -c '...'` 进场, 仓库在 WSL 内 `~/github/{panvox,avox}`。先 `uname` 确认落在哪台, 目标≠本机才过 SSH/adb。**Win 目标过 ssh 起 GUI app 会落在不可见会话** —— app 级复现让用户手起, 引擎级复现走免窗的 engine_play_test。
- **数据目录**: Win `%APPDATA%\panvox\`; **Mac 真位两看(102 定谳: 沙盒态随构建变过)**: 当前安装位构建**无沙盒 entitlement**(`codesign -d --entitlements - ~/Applications/panvox.app | grep -c sandbox` = 0), 真位 = 裸 `~/Library/Application Support/com.panvox.panvox/`; **102 晚再漂移: 数据文件(history/media_info/sources/local_library/scan_index)现居 `~/Documents/panvox/`(与 logs/ 同根), Application Support 侧只剩 stale sources.json+models/cache 勿再按它找 history**; 9/24–9/26 旧沙盒构建才写容器 `~/Library/Containers/com.panvox.panvox/Data/…`。拿不准就 `codesign` 数 sandbox + 各候选路径比 mtime, 新者为真位(102 曾按过期的「容器真位」口径扑空)。关键文件: `sources.json`(源配置 id/kind/origin/user/pass/root/token —— **明文凭据**)、`history.json`(键=源id+路径 → title/position/duration/updated)、`media_info.json`(播放档案: at=ms epoch / via=thumb|playback / 轨道表, ready/playing 才落档 —— **重建用户操作时间线最可靠, 某片有档=引擎当时 open 成功过, 卡点就在其后**)、`local_library.json` / `scan_index.json`(文件清单)、`unplayable.json`(打不开下墙记录)、`freeze/`(冻结名片); 模型缓存 = 真位 `models/`(Application Support 侧, app 内 ModelFetcher 下载落位)。

## 1. 流程
0. **先对表版本**(防"改了没编/没部署, 查的全是已修掉的"): 日志首行 banner(`avox version:... commit_hash:X build_time:Y`)对比 `git -C <avox仓> log -1`; 落后 HEAD 或启动闸打「install 落后 HEAD」→ 先重编引擎+部署(部署文档 §2/§3)再排查; 其余平台闸门见文档 §4。**banner 的 commit_hash 是 configure 时烤的会失真**, 精确判"修复是否编入"用 dll 考古(§3)。
1. **三要素**: 平台(**不指明=当前机器**, 本机直查不走 SSH/adb)、片源(哪个源哪部片/文件名)、现象(打不开/卡顿/花屏/无声/音画不同步/字幕/崩溃 + 大概时刻)。缺了先问; 片源模糊可拿 `history.json` 最近条目猜并跟用户确认, 别空手反问。
2. **定片源 + 体检**: 读 `history.json` 最近条目 + `sources.json` 映射出 kind/origin/路径。URL 推导: webdav/http = `origin+root+path`(路径段 URL 编码); smb = UNC `\\origin\共享\路径`; 本地 = 直接路径; **jellyfin/emby/云盘/IPTV 不手拼**(需 token/接口), 驱动 app 内复现或向用户要直链。**到手先 curl 验真身**: `curl -r 0-63` 看魔数(mkv=`1A 45 DF A3` / mp4=`..ftyp` / flv=`FLV\x01` / 几十 KB = 假片) —— NAS content-type 按扩展名给**纯误导**(实测 BT 目录 .torrent 顶 .mkv 名、真片在同名子文件夹; 迅雷改后缀 FLV→.mkv 是一整个家族), 引擎报 `EBML header parsing failed` 就是这类, 别往引擎查。转述的中文路径可能被转码损坏(汉字会变成另一个字), 404 先 unquote 逐字节核对目录名再 PROPFIND 全盘搜关键词。`curl -r <中部偏移> -o /dev/null -w '%{speed_download}'` 测吞吐(健康 11~30MB/s = 服务器无恙的铁证)。
3. **带日志复现**(§2 按平台)。复现前**单实例检查**(Win `tasklist | grep -i panvox` / Mac `pgrep -x panvox`) —— 多实例共用数据目录互覆快照会出假象; 并**先把 stdout 重定向摆好** —— 卡住的实例被杀后无日志就无法尸检。
4. **对齐用户实际路径**: 能自动化就自动化 —— `PANVOX_AUTOPLAY=<可播URL或路径>` 让 app 启动后直接播该条(免手点; Win cmd: `set PANVOX_AUTOPLAY=... && start panvox.exe -Log`); 「seek 到某位置才出现」的病加 `PANVOX_AUTOPLAY_SEEK_TO=<秒>`(支持小数; `PANVOX_AUTOPLAY_SEEK_PAUSE=1` 到位后暂停钉帧, 截图 HUD 核对落点; 也可 `python tools/probe_shot.py <out.png> <media> --seek-to <秒> --seek-pause`)。**用户点开 = 带续播 seek**(position 从 history.json 读), 全从零起播会漏掉 seek 路径的病 —— 续播场景补 seektest(§5)。真实 UI 操作(切轨/倍速/字幕切换)在 Win 本机可加载 avox-cli skill 用 ops 找字点击。
5. **读日志**(§3) → **先对 §4 病族索引**(签名吻合直接给结论 + 核修态; 需根因/判据/验收数据再读 `references/病族-*.md` 对应分册, 对不上再立新案) → 归属判定 → 结论 + 建议。**闭案 = 同参数重跑复现路径 + ctest + 离线矩阵零回归**(`cd ../avox-test && python script/testenv/play_regress.py --offline`, 基线 49P+4dav 既有挂) + 常规直链复跑确认零影响。

## 2. 各平台带日志复现
**Windows**(本机或 ssh pc)
- 首选启动闸(先哈希同步引擎 dll, 堵"宿主跑旧引擎"): `panvox.cmd -Log` → stdout/stderr 落 `%APPDATA%\panvox\logs\panvox-<ts>.log`(+.err, 留最近 20 份)。
- exe 直启: `panvox.exe -Log` → 进程内档 `panvox-log-<ts>.log`([dart]/[info]..[debug] 前缀)。**无 -Log 参数零日志**(9/26 门控定稿), 老构建无此开关。
- **一键真实链路复现**(杀实例后): runner/Release 目录 `PANVOX_AUTOPLAY=<url> ./panvox.exe > 日志 2>&1 &` —— shim stderr + Dart print + 引擎 log 全落一个文件, 免手点走 app 真实开片链路(硬解/GPU 合成/窗口直渲全真)。
- 崩溃: Release 目录(`app\build\windows\x64\runner\Release\`) `AVOX_*.dmp`(app 自带 handler, mtime≈崩溃时刻) → `python tools/analyze_dump.py <dmp>`; 疑冻结看 `%APPDATA%\panvox\freeze\`。排 crash 需带符号引擎: `AVOX_BUILD_TYPE=RelWithDebInfo python build_windows.py`(avox 仓) + `tools\deploy_runtime.ps1 -EngineConfig RelWithDebInfo`(部署文档 §5)。

**Mac**(ssh mac): **-Log 已补(9/29, panvox 2f11d8e 三端一份)**: `open ~/Applications/panvox.app --args -Log` → `~/Documents/panvox/logs/panvox-log-<ts>.log`([dart]/[info]..[debug] 前缀同 Win; 旧构建无此开关)。
- **用户实际启动位是 `~/Applications/panvox.app`**(非 build products), 引擎静态链进 libpanvox_native.dylib(无独立 avox dylib); 换引擎优先 `bash tools/deploy_macos_runtime.sh Release`(部署文档 §3.3, 它才编 `keyring.cpp`), 换完 `nm -gU <dylib> | grep pvx_keyring_get` 须有 1 个; **误用 `deploy_macos_shim_app.sh` 会漏 `_pvx_keyring_get` ⇒ TVDB 凭据变空**(单机只换引擎可用它但须事后校验 keyring 符号)。核对启动位 dylib 已刷新用 `strings <dylib> | grep <修复特征串>` 最实。
- **Finder/launchd 启动引擎 stdout 全丢**(os_log 也常无条目、无自有日志文件): 必须终端带重定向重启 `nohup ~/Applications/panvox.app/Contents/MacOS/panvox >/tmp/panvox-run.log 2>&1`。
- **0927 实测此法可能走不通**: 直启沙盒 app 死在 `_libsecinit_appsandbox`(SIGTRAP, 非 app 崩溃), `log show --predicate 'process == "panvox"'` 也无条目 → 绕法: 经 LaunchServices 启动(`open -a`, 传 env 用 `open --env`), 或**降级到引擎级复现**(§5 avox_cli/vsynctest, 对「解码/渲染时序」类病等价且更可控)。
- **xcodebuild 在本机沙箱下会被拦**(`CreateBuildDescription failed` / `Unable to write manifest.json` / `Operation not permitted`, 卡在写 `~/Library/Developer/Xcode/DerivedData/.../info.plist` 与 SWBBuildService): **解法 = 前台执行 + 关沙箱**(0927 实证: `xcodebuild -project avox.xcodeproj -configuration Release -target ALL_BUILD build`); **放后台跑时即使带关沙箱旗标也不生效**。改 `TMPDIR`、`-derivedDataPath` 均**无效**。兜底 = 手写 clang++ 链接命令直接产可执行(清单见 avox-macos-playmatrix-runner skill 附录), 或 `cmake -S . -B build/macos/avox -DAVOX_ENABLE_CLI=ON` 重配走 ninja(`avox_cli` 默认关, 见 src/CMakeLists.txt)。
- 引擎日志备选: `log show --last 10m --predicate 'process == "panvox"' --info`, 或 `log stream` 边播边收。
- 「卡住/停止」先查 `~/Library/Logs/DiagnosticReports/` 有无 .ips 区分**崩溃 vs 冻住**(无 .ips = 冻死非崩); 解码自愈停顿 ≤1GOP(数百帧≈9s)易被用户当「停止」, 别误判。

**Android**(adb): `adb install -r app/build/app/outputs/flutter-apk/app-release.apk`; `adb logcat -c && adb logcat -v time -s avox:V flutter:V`(引擎 tag=avox, Dart tag=flutter); 蜂窝网"全源打不开"先 `adb shell svc wifi enable`(历史陷阱)。

**iOS**(模拟器 `xcrun simctl launch --console-pty booted com.panvox.panvox`; **真机 10/3 已打通, 不必再靠复述**):
- 抓**引擎**stdout: `xcrun devicectl device process launch --device <udid> --console --terminate-existing --activate com.panvox.panvox`, 用 `script -q <file> <cmd>` 落地 + 定时 `pkill -INT -f 'device process launch'` 收尾(INT 转发给 app)。原生行齐全: `state from`/`io open result`/`io error,code:`/`[metrics] io open_input_ms`/`[FF]`; 真机 udid 用 `xcrun devicectl list devices`。
- **Dart 的 print 不在 iOS 进程 stdout**(上面只见原生行), 且 **`-Log` 不可达**: Flutter iOS 不给 Dart 传 argv/env(`Platform.environment`/`main(args)` 恒空, 真机实证), 故 `-Log` 开关与 `AppLog` 进程内档都开不起来 —— 要 Dart 侧留痕只能改代码加容器文件开关(同 iOS 插件 `pwt.txt` 先例, 需重编+重装)。`log stream --device-name` / `log collect` 被本机沙箱挡(`log: Cannot run while sandboxed`), 别试。
- **无头复刻"点卡"链**: 容器 `Documents/panvox/autoplay.txt` 第 1 行喂 **history.json 的身份键(哈希)**, 第 3 行 `player-re` → AppShell 走 `_openPickedFile(hash, resume:true)` = 与墙卡/启动续播同链(`_history.refFor` → `_resolveRef`)。⚠️ 该通道**不带 `ref`** ⇒ 落 `FileRef.local`, 壳层直链重试链不触发: 只能看到「首开失败」, 看不到「失败后又播起来」——要验后者必须手点 UI。
- 数据档直读: `devicectl device info files --domain-type appDataContainer --domain-identifier com.panvox.panvox --username mobile --subdirectory Documents/panvox`; 拉档 `device copy from ... --user mobile`(**info 是 `--username`、copy 是 `--user`**, 混用报 Unknown option)。

**Linux/WSL**(= Win 本机里的 WSL Ubuntu, `wsl bash -c` 进场): 仓库在 WSL 内 `~/github/{panvox,avox}`; 起 app `~/github/panvox/app/build/linux/x64/release/bundle/panvox 2>&1 | tee /tmp/panvox-run.log`, **必须普通用户**(WSLg 下 root 连不上用户 Wayland socket)。wsl.exe 实操: 复杂命令写 .sh 进去跑(引号经 Win 层易被吃), 路径用 wslpath 转, 脚本忌 CRLF, **stderr 提示常是乱码**, 以正常输出为准。画面恒走 frame_poll CPU 车道, 与 Win/mac 硬解车道不可直接互推; 引擎构建车道归夜班 openclaw 会话, 动手前 `ps aux | grep build_linux` 确认没人编别抢树。

## 3. 日志分析
- 按目录 mtime 取最新文件读; **严禁全盘/递归搜索日志**(必超时)。大文件先看头尾定时间戳格式, 再按用户说的时刻 grep 时间窗。
- 引擎行([MPx] 实例前缀 / [FF] ffmpeg / [ZL] zlmediakit / io create-open result / addVideoDesc-addAudioDesc / state from X to Y / playing↔buffering / av not align)与 **analyze-log skill 同一套判定表**, 卡顿/打不开的细判直接加载它, 不在本 skill 重复。
- panvox 层特征行: `[panvox-boot]`(启动顺序)、`[panvox-shim]`/`[panvox-runner]`(shim/GPU 适配器)、`[dart]`(-Log 进程内档)、`[avox][级别]`(引擎桥行)、`watchdog armed`(冻结名片机制)、unplayable 记录(自动下墙: 引擎败 + 文件头非容器才标, **网络类错误永不标**)。
- 归属判定序: ①io open 失败/超时/403/404/503 → 源或网络(签名限时源失效**严禁同参自动重播**); ②流信息缺/decode create fail → 编码不支持或硬解问题; ③opening 长停 → IO 慢/假成功; ④buffering 频繁 → 查队列与丢包行; ⑤Dart exception/zone-error → app 层逻辑; ⑥无日志直接退 → 走 dmp。**最有力的归属证据 = 本地同文件零卡 + http 卡**(病在 http IO 路径)。
- **open 成功却不到 playing 先分两族**: 有 `avcodec_open2 failed`/`videodecode error … open failed` = **编码族**(归属②, extradata/编码支持面, 见 `references/病族-open.md` wmv3 条); `io open result: success` 后整段静默(无 open_input_ms/流信息/错误行) = **长停族**(归属③, demuxer 卡容器头或服务端读行为)。两族判定序落同一档, **归因别并类**(0927 夜巡 7 片两族各半)。
- **时间线重建**: Release 目录的 panvox_stdout.log 常是旧文件; stdout 未重定向的实例死了就无日志(只能收敛怀疑面让用户带日志复点)。用 media_info.json 的 at/via 逐条排(实测: [FF] 刷屏真凶是几分钟前另开的另一片)。
- **dll 考古**(判"修复是否真编入部署位"): 内嵌 commit_hash/build_time 不可信; 看 `build/windows/avox/src/<模块>.dir/Release/<改过的文件>.obj` mtime vs 源文件 mtime vs 提交时间; Win 启动闸 Copy-Item **保留源 mtime**(runner 目录 dll 的 mtime = 源构建时间 ≠ 同步时间)。
- **seektest 判读**: post0 near-black 且不同目标位置统计全同 = seek 前在飞残帧伪影, **勿当 bug 追**; 验收看 post1+alive+耗时(测试判定行可能因此恒 FAIL, 属口径非缺陷)。
- cli 默认级别看不到 [FF] 桥日志, 要 `-loglevel verbose`(但 open_input_ms/first_frame 等指标与级别无关)。FFmpeg 日志行出处用本机源码树 `D:/Work/github/ffmpeg/` grep 字符串定位最快, 别猜(引擎 = FFmpeg 9.0.1, avformat-63)。
- **本机是 BSD grep**: `\|` **不是**「或」, 多选一律 `grep -E "a|b"` —— GNU 写法**静默零命中**, 极易误判「日志里没有这条」(0927: `poc restamp active` 明明在, 被 `\|` 吃掉差点翻案)。
- **取码流头字段(判显示序)用 `ffmpeg -bsf:v trace_headers`**: `-i <file> -c:v copy -bsf:v trace_headers -frames:v N -f null -`, 从 stderr 抽 `pic_order_cnt_lsb`/`slice_type`/`nal_unit_type`(本机无 ffprobe, 只有 `~/.local/bin/ffmpeg`)。这是「重打戳对不对」的 ground truth。moov 在头时可只下前缀(`curl -r 0-N`)建可解析的局部文件。
- 需脱离 app 隔离引擎时用 §5 探针; 旋钮: 引擎级加 `-log-packet`(包时间/PTS)/`-log-decode`/`-log-render`(app 内无此开关); `PANVOX_THUMB_TRACE=1`(抽帧链); FFmpeg 桥默认压 WARNING, info 级需引擎侧另开。

## 4. 已知病族速查(索引)

签名吻合 → **先按本行结论定案**(是否已修 / 修在哪个 commit, 修态核对用 §3 dll 考古); 需要根因·判据·验收数据时再读对应分册。**对不上任何一行再立新案。**

**open/起播** → [`references/病族-open.md`](references/病族-open.md)
- 容器头解析失败(EBML…) → 拿到非容器(假 mkv/改后缀 FLV) → 源端假片, §1.2 体检定真身
- **点卡/启动续播「先闪无法打开该文件, 随后自己正常播」→ 首开喂给引擎的是非 URL(库内裸路径/身份键), 已定位(1003, 见 open 分册)**: webdav/http 源卡面传的是 `sourcePath`/`item.id`(裸库内路径), 而 `_openPickedFile` 只给 SMB(`smbEnginePlayUrl`)与云盘(CloudLinks)换真直链, **webdav 缺这一跳** → `engine.open('/sata1-…/x.avi')`; 引擎 `avformat_open_input failed error[-2]: No such file or directory` + `io error,code:100` → shim 哨兵 → Dart 即刻 failed(浮层) → 壳层 `_onEngineForRetry` 800ms 后 `_retryWithFreshUrl`→`_resolveRef` 拼真 URL 重开 → 正常播。**判据 = 日志 `io open result: success msg:` 打的是路径/哈希而非 `http://`**; SMB 源不中此族(有 `smbEnginePlayUrl`)。
- **Mac VT 起播全帧 `-12909` 风暴(resync 循环无效, 黑屏只有声) → 多 slice 流被逐 slice 包直喂 VT; 已修 AU 重组+让道软解(1002 定谳, 见 open 分册)**
- **wmv3 拒播/有声无画 → 已修 ee8bf7b(0927)**
- open 后卡死(`partial file` 风暴 + misland EOF + `clock-leak guard seek(0)` 死循环) → **0926 定谳未修**
- http 直链 open 卡 10 分钟+ = 迅雷逐 GOP 落盘 mp4 → 已修 39aa4aa
- **open 慢≠败 / AVI 慢开两族(申报大小≠实际、ODML indx) → 已修 ce86db8+271db52+393f954(0927)**
- 开片即崩(avsubtitle_free 栈, PGS 字幕) → 已修 c08adb4
- 网络源硬解首帧慢被 5s 看门狗误杀 → 已修 2c2444b

**seek 后** → [`references/病族-seek.md`](references/病族-seek.md)
- **seek 落尾/越尾钳尾滞留 buffering(三形态) → ①②③ 已修(1bcc35a / bd09de9); 残留新形态(258)未修**
- **fMP4 分片(stub moov + moof×N)只播首片即假 EOF → 已修 c5f70ed(0928)**
- **缩图/录制 muxer init 失败后 close 跳0崩(RIP=0) → 已修 f76db0e(0928)**
- mkv seek 卡 ~3 分钟(matroska 前向扫描) → 已修 8d61674; **残宗(Android interrupt 窗口)未修**
- 改后缀 FLV seek 转 25~46s → 已修 flvEstimateSeek(0926)
- seek 进片尾 buffering 死锁 → 已修 a501667
- seek 后卡一会/pos 半速爬 + 周期 buffering(rip mega-chunk) → 已修 af17043
- Mac seek 冻画只有声 + `[vt] BadDataErr(-12909)` 风暴 → 韧性已修 88b0135; 根因待 a02
- Mac **反向** seek 没反应/进度条弹回 → **未修**(a02)
- seek 后长冻(GOP≥20s IDR 稀疏) → **未修**(a02)

**播放中** → [`references/病族-播放中.md`](references/病族-播放中.md)
- **黑屏有声 + `[FF][hevc] PPS id out of range` 风暴(手机/微信导出 HEVC 双 PPS 流) → 已修(1003, 见播放中分册)**: addConfigPacket 同类型替换丢掉另一 id 的 PPS(IDR 与非IDR切片各用 pps_id=0/1 缺一不可); 叠加水印 SEI 混入 parseConfigs 整段判失败 → SPS 544x960 vs 容器 540x960 误判 updateSize 硬解重置。判据: 参考解码器软/硬解全通 + 引擎拼的 hvcC 数组数少于文件真值 → 别往 avcodec/渲染层查
- **iOS 真机「全黑不出画」与「出画一两秒后定格」→ 已修(1003, 修在 panvox 侧, 见播放中分册)**: 两病灶分开——全黑 = `CAMetalLayer.drawableSize` 恒 0×0(挂进 Flutter 平台视图后 UIKit 的 bounds×scale 自动同步失效, MoltenVK 建了链却无 drawable 可出), 修 = `layoutSubviews` 显式钉 `bounds×contentsScale`; 出画后定格 = 治「层内容不上屏」加的 `presentsWithTransaction=YES` 在**无 runloop 的渲染线程**上等不到 Core Animation 事务(Flutter UI 一静止就没人提交), 修 = 改回关。判据: 引擎侧 `tick present`/`rw-in` 全 30fps 稳态 + `mtl cb: status 4 err:none`(排除 GPU PageFault) + `vk-in record` 只 1 条(排除图重建) ⇒ **渲染在跑、只是没人提交事务上屏**; 同机 A/B 8/8(pwt.txt 切开关, 关=抓帧差异 769860 在更新 / 开=差异 0 定格)
- **显示格时间戳截断族(VT 独有)**: A 每几秒跳 → 79abb44 / B 同类片仍每 2s 跳 → 已修 0927 / C seek 后持续抖 → 已修 0927
- 后向 seek 后画面脱离音轨 → 已修 24ff54d(全平台)
- 全片零规律散点跳画 + `Invalid NAL unit size` 风暴 → 已修 e409b32
- 播放时间来回跳 + 无限 buffering(音轨锚文件头) → 已修 a0107be
- 直链反复 buffering 无时间跳 → 非 bug(垫子 2s, 2a200e9)
- 花屏伴 rtp 丢包 → 网络; 从 P 起解不自愈 → 落点缺参考
- `[FF][mlp] Stream parameters not seen` 刷屏 + 位置 18 倍慢放 → 已修 96e205b+f58bc94
- 全片每秒周期跳帧(TrueHD 碎片轨) → 已修 c3f92b0
- **多声道 AAC(5.1+)整轨静音(音轨在、画面正常、无 buffering, 日志缺 `setDesc` 行) → 已修(fdk-aac 输出缓冲定长 10240B 不足 6ch×1024, 每帧 `8204` 风暴; 见播放中分册)**: fdk 初始化成功故不触发 openFailed 回退链而 FFmpeg 车道永不接管; 首帧恒有一条 `error:5` 是无害噪声勿与风暴混判
- **顶部细条彩带闪烁 → 片源病, 非引擎(换源才能根治)**
- **DV Profile 5 颜色与 VLC/系统播放器不一致(自己品红/紫、别家青绿) → 非引擎问题: P5 基础层=IPTPQc2 且容器无色彩标签, 不做 DV 反变换的播放器按 BT.709 直出必然偏色; 自己日志 `[dovi] dispatch valid=1` 即正确(判据/复现配方见分册)**; 同日复核实测另发现的 P5 偏暗欠饱和(DV 输出未走 tone map)**已修(1002, 四腿 `doviEnable==1` 时按 PQ 消费输出)**: 修后 `ubo transfer:0` 属正常(打印的是源标签), 验收数字见分册
- **DV 片之后播非 DV 片发红/发粉(DV 整形状态跨 open 残留; 拖窗跨 HDR/SDR 屏也会触发) → 已修(1002, 开流复位空 DoviMeta; 见播放中分册)**: 非 DV 流不派发 DV 元数据故旧状态常驻, 判据 = 非 DV 开流日志应见 `setDoviMeta valid=0`
- **无色彩标签的 HDR 片不出 HDR 徽章 + 画面发灰(Netflix Open Content「P3PQ」家族) → 非引擎问题: 文件无 primaries/transfer/matrix 也无 SEI, 引擎只能记 gamma/bt709, 徽章按源 trc 出故不出; 不做自动猜测(会误伤暗调 SDR), 需要时加手动「按 HDR 播」override(判据见分册)**
- **报「无声」先查容器有没有音轨(Netflix Open Content 测试片族整族无音轨) → 非播放器病(1002): 判据 = 引擎日志 `trackReady no audio track` + media_info 档案 `"a": []`; 反证用带 TrueHD 的片跑通全链(见分册)**
- **报「没画面」先量片头亮度+首个 GOP(Netflix 高帧率片: 开头黑场淡入 + 首 GOP 10.24s → 前 10s 内拖条回落 0:00 黑帧, `seek landed:0 target:8000`) → 非引擎问题(1002, 判据/复现见分册)**
- **点播放/拖进度条后整 UI 冻死(低 CPU+无 .ips+`sample` 全采样锁同一栈) → 锁序反转死锁, Apple 专属(macOS/iOS) → 已修 466a928**: IOSAudioRender 同把 `mtx` 护缓冲+CoreAudio 句柄, onClose/pause/setVolume 持锁调 AudioOutputUnitStop/Start/SetParameter 等 HAL 锁, 实时 `renderCallback` 持 HAL 锁抢 `mtx` → AB-BA; `renderCallback` 改 `try_lock` + CoreAudio 调用全移锁外。判据/二进制验收见本文「锁序反转死锁」条。
- **报「说话时没字幕」先分两族定归属(1002)**: ①**源轨段内缺词** —— 用 ffmpeg 抽该轨全量对表(`ffmpeg -i <url> -map 0:s:0 -f srt out.srt`, 按 60s 桶数 cue 找洞), 洞内音频能量与对白密集区同级(ebur128 I 差 <1 LU)即坐实片源缺词, 换源/AI 字幕才能治; WEB-DL 双语轨同一时间源会同洞。②**app 选轨链没跑**(重开同片族, 面板事后能列轨=轨快照已回但 _reloadSubtitles 先跑了读到 count=0 且无人补跑) → 已修 dacf754+57845bf(1002, 部署态核对 BUILD_INFO ≥ 10-02 13:07)。判据: 引擎日志有无 `cmdSetSubtitleTrack … selected:N` 行。⚠️ 复现注意: `PANVOX_AUTOPLAY` 走 playback_probe_page, **不跑字幕选轨链**(只认 `PANVOX_AUTOPLAY_SRT` 外挂路), 内封轨复现要么手点 UI 要么看正常路径日志; l2_panvox 字幕像素用例的截图法停在 A-4 纹理时代(tex 恒 0/整帧黑), FAIL 不构成引擎失效证据, 待重写采集腿
- **字幕整行空心方框(tofu, 日中全灭、英文数字正常, mac) → 双层同症状, 均已修 d5a603b+6edc3c3(1002)**: ①字体层=FontCache 兜底链在新 macOS 命中 Helvetica(无 CJK); ②解码层=`utf8TWstring` 非 Win 分支有符号 char 误判→**每 UTF-8 字节一个框**(CJK 每字 3 框, 方框数=字节数即这层, Windows/Android 无感)。判据 = -Log `Loaded default font: …Helvetica.ttc`(病)/`Loaded bundled font: …simhei.ttf`(修后); **方框=字体/解码病, 错字/U+FFFD 菱形=编码病别混**; mac 外挂 srt 验收走 panvox 真实链路, subtitletexttest 在 mac 不渲染勿用(详见分册)

**App 内模型下载「满进度重来」(AI 字幕/画质模型的下载卡)**: 进度反复跑满→清零重下 = 清单 sha256 与发布 asset 失配 → 下载器每源**完整下载后**才 hashMismatch 换源重下(无续传、当时零日志)。102 定谳: stt-sense-voice.zip 4/24 重传后字节变(实测 11152a86)≠三份清单烤的 01cd4398, 主源组三条源全废靠 HF 备用组落地; 修=三份清单同哈希(avox a287502 + panvox 173a719)+ 失败路径落 `model-fetch:` 日志行(-Log 可见)。诊断铁证 = 真机 `curl -sL` 拉 zip 实算 sha 对清单; Windows 不暴露此族因模型由部署配方预铺, 不走 app 内下载器。

**mac 插件腿 dlopen 失败(「STT plugin unavailable」/ 字幕降级 no-op 等)**: mac 引擎静态链入 shim, **只拉被引用的归档成员** → 插件 dlopen(RTLD_NOW 全量解析)要的引擎符号若 shim 自身无引用就不在 dylib 里(10/2 定谳: AudioTts::setSpeaker)。修=deploy_macos_runtime.sh 链接行按部署插件 `nm -u | grep -o __ZN4avox*` 逐 `-u` 钉链(f7a1ee0); 诊断三板斧: ①裸 ctypes dlopen 插件看首个缺符号 ②引擎 RTLD_GLOBAL 后再 dlopen(模拟 app) ③nm -gU shim 对缺符号。

**mac 插件自包含纪律(第二插件案, 10/2)**: mac 插件是 `-undefined dynamic_lookup` 构建, 其 UND 在 RTLD_LOCAL dlopen 的平面查找下**看不到自己 LC_LOAD 的依赖 dylib** → 插件依赖的第三方符号必须静态吞入插件内(libsmb2/SSL 系 = WebRTC 归档的 BoringSSL + 系统 Security.framework; 注意 darwin 预编译件按 BoringSSL 编, 这些名字在 BoringSSL 是真函数, 别拿 OpenSSL 3 静态库去接——3.x 里它们是宏, 符号不存在)。引擎 rebuild 会连带重链插件, 依赖面可能静默变化, 插件加载失败先 `otool -L` + `nm -u` 重验。

**网络环境(本机代理/TUN, 非引擎病)** —— 修法在引擎外, 故全文留本文常驻
- IPTV/m3u 列表与国内流普遍慢、超时、周期 buffering, 而 NAS/局域网源全正常 → 先查本机 TUN 接管: `route print` 见 Meta Tunnel/Wintun + `tasklist` 见 verge-mihomo(Clash Verge)= 全机流量过代理。**对照法: `curl` 默认路由 vs `curl --interface <物理网卡IP>` 直连**(0923 实锤 CCTV1 列表 TUN 19s→直连 1s; 0926 复测首响 3.2s vs 1.2s); 修法 = Clash 给国内直播域名加 DIRECT 规则或关 TUN, 不动引擎。**绑定源地址法在部分环境只是绕路成功, 直连腿 000 时先核对绑定语义再下结论**。
- 免费聚合清单(live.zbds 类)两大常态别当 app 病: ①大量「频道」= 点播循环(HTTP-FLV 服务端把整剧/整片循环推流, 0926 抽样 542 频道 107 个循环体, metshop 一台 66 个 —— 循环是内容本身, 永不完播); ②死链/整台服务器超时常态(145 台流服务器抽样过半 8s 无响应)。

## 5. 引擎级复现与探针
- `tools/engine_play_test.exe`(须与 avox.dll 同目录, `tools/build_engine_play_test.bat` 出; `engine_play_test <url> [sec=8] [hard=1] [vulk=1]`, 免窗可过 ssh); 或 avox_cli play(加载 avox-cli skill: `-io ffmpeg`/`-transport tcp`/`-log-packet`/`-log-decode`/`-loglevel verbose`)。
- **seektest**(samples/functest): `seektest <url> <秒> <前缀> [-hard]`, 判定行 `[AVOX][TEST] result=PASS`; 验续播/seek 修复必备(判读见 §3)。
- **seekstorm**(samples/functest): `seekstorm <url> [count=24] [intervalMs=120]`, 密集连发 seek 复刻拖进度条; 判定 = 风暴后回 playing + 末 5s 帧计数推进。单次 seektest 绿但用户拖动冻 → 用它。
- **stutterprobe**(samples/functest): 帧到达间隔 + buffering 段 + pos 推进, 验供给类修复; 判定要帧数下限防空判。
- **Mac 探针族 /tmp/probe0926**: 链接级覆盖 —— 改 IOSVDecoder.mm 等单编 .o 放 libavox.a 前链接即换实现, 不碰共享树(编译参数从 flags 反推); Win 侧对应 `build/probe_seek`、`build/probe_tailseek`(python minidump + ctypes dbghelp 挂 pdb 符号化卡死线程栈, 线程全在 ntdll 等待时找自家读/解码线程的分支)。
- **容器病态结构 python 手解**: stco/stsz/stsc 分布暴露锚位与碎片化(track 块数/块率量化); EBML walker 验轨道表; mp4 fullbox 的 entry_count 在 version+flags 之后(+4); 结构对账别心算对齐用 python。

## 红线
- **凭据脱敏**: sources.json 的 user/pass/token、URL 里的 session/sign 不进结论、不复述、不回显。
- 单实例纪律; avox 构建树有并行会话在用别抢(尤其 WSL 车道)。**并行会话同文件施工**: 提交挑自己 hunk(python 挑选 + `git apply --cached -C1` 分摊)别捎带别人改动; dll 被对方 playtest 锁住(LNK1104)等别杀; 动 a02 施工区(AVSource.cpp/VideoTrack.cpp 等)先确认归属。
- 用户数据目录默认只读; 重扫/清缓存/删 unplayable 等写操作先征得用户同意。
- 结论必须附证据行(时间戳 + 关键行内容), 找不到证据行就明说未复现, 严禁臆测补链; 全绿未定谳就明说全绿 + 收敛怀疑面, 别硬安根因。
