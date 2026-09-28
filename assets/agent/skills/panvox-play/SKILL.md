---
name: panvox-play
description: panvox 播放问题端到端排查:用户报"某平台+某片源+某现象"(打不开/卡顿/花屏/无声/字幕/崩溃)时加载。自动定位目标设备(本机/ssh mac/ssh pc/adb),从 panvox 数据缓存定位片源并先验真身,带 -Log 复现(对齐用户实际续播路径),按日志行先对已知病族速查再定根因(源端/网络/app/引擎)。仅 panvox 应用问题用本 skill;手头只有一条裸 URL 要验播放走 avox-cli。
whenToUse: 用户描述 panvox 应用内问题(某源打不开/播放卡/字幕异常/投屏/崩溃等)且需实际复现取日志时。用户已给日志路径只要分析→analyze-log;只要截图找字点击→avox-cli。
---

# panvox 播放问题排查

## 0. 前置事实
- **双仓同级**(panvox 宿主, avox 引擎): Windows `D:\Work\github\{panvox,avox}`; Mac 构建盘 `/Volumes/PSSD/work/github/`(另有部署克隆 `~/development/panvox`); Linux/WSL `~/github/`。全平台编译/部署配方在 panvox 仓 `docs/avox-build-and-deploy.md`(启动闸/部署脚本/新鲜度闸门都在里面, 需要重编先读它 §2/§3)。
- **通道**: 本机通常是 Windows 开发机; `ssh mac` / `ssh pc` 双向免密; Android 真机经 Windows `adb`; iOS 模拟器经 Mac `xcrun simctl`; **Linux 环境 = Windows 本机里的 WSL(Ubuntu), 不是独立目标机**, 经 `wsl bash -c '...'` 进场(无 ssh), 仓库在 WSL 内 `~/github/{panvox,avox}`。动手先确认自己落在哪台(uname), 目标≠本机就过 SSH/adb, **Windows 目标过 ssh 起 GUI app 会落在不可见会话**——app 级复现让用户手起, 引擎级复现走免窗的 engine_play_test。
- **数据目录**: Windows `%APPDATA%\panvox\`; **Mac 真位=沙盒容器 `~/Library/Containers/com.panvox.panvox/Data/Documents/panvox/`**(history/media_info/sources 都在这; 裸 `~/Library/Application Support/com.panvox.panvox/` 只有残缺旧位, 0926 两案实证)。关键文件: `sources.json`(源配置: id/kind/origin/user/pass/root/token —— **明文凭据**)、`history.json`(键=源id+路径, 值=title/position/duration/updated)、`media_info.json`(播放档案: at=ms epoch/via=thumb|playback/轨道表, ready/playing 才落档 —— **重建用户操作时间线最可靠; 某片有档=引擎当时 open 成功过, 卡点就在其后**)、`local_library.json`(文件清单)、`unplayable.json`(打不开下墙记录)、`freeze/`(冻结名片)。

## 1. 流程
0. **先对表版本**(防"改了没编/没部署, 查的全是已修掉的问题"): 运行日志首行 banner(`avox version:... commit_hash:X build_time:Y`)对比 `git -C <avox仓> log -1`; banner 落后 HEAD、或启动闸打「install 落后 HEAD」WARNING → 先重编引擎+部署(部署文档 §2/§3)再排查。Windows 启动闸每次启动哈希同步 dll; 其余平台闸门见部署文档 §4, 见闸照做别绕。**banner 的 commit_hash 是 configure 时烤的会失真**, 精确判"修复是否编入"用 dll 考古(§3)。
1. **三要素**: 平台**不指明=当前机器**(本机直查, 不走 SSH/adb; 用户点名别的平台才切通道)。片源(哪个源哪部片, 文件名)与现象(打不开/卡顿/花屏/无声/音画不同步/字幕/崩溃 + 大概时刻)缺了先问; 片源模糊可先拿 `history.json` 最近条目猜并跟用户确认, 别空手反问。
2. **定片源+片源体检**: 读目标机 `history.json` 最近条目 + `sources.json` 映射出 kind/origin/路径。可播 URL 推导: webdav/http = `origin+root+path`(路径段 URL 编码); smb = UNC `\\origin\共享\路径`; 本地 = 直接路径; **jellyfin/emby/云盘/IPTV 不手拼 URL**(需 token/接口), 驱动 app 内复现或向用户要直链。**URL 到手先 curl 验真身再复现**: `curl -r 0-63` 看魔数(mkv=`1A 45 DF A3` EBML / mp4=`..ftyp` / flv=`FLV\x01` / 几十 KB 小文件=假片)——NAS content-type 按扩展名给**纯误导**(实测: BT 目录 .torrent 顶 .mkv 名、真片在同名子文件夹; 迅雷改后缀 FLV→.mkv 是一整个家族), 引擎报 `EBML header parsing failed` 就是这类, 别往引擎查。转述的中文路径可能被转码损坏(UTF-8 尾字节被改, 汉字会变成另一个字, 实测发生过), 404 先 unquote 逐字节核对目录名再 PROPFIND 全盘搜关键词。`curl -r <中部偏移> -o /dev/null -w '%{speed_download}'` 测吞吐(健康 11~30MB/s=服务器无恙的铁证)。
3. **带日志复现**(§2, 按平台)。复现前**单实例检查**: Windows `tasklist | grep -i panvox`、Mac `pgrep -x panvox` —— 多实例共用数据目录互覆快照, 会出假象; 有实例先让用户关或 kill。**复现前先把 stdout 重定向摆好**——卡住的实例被杀后无日志就无法尸检(实测教训)。
4. **按用户描述触发, 且对齐用户实际路径**: 能自动化就自动化 —— `PANVOX_AUTOPLAY=<可播URL或路径>` 环境变量让 app 启动后直接播该条(免手点; Windows cmd: `set PANVOX_AUTOPLAY=... && start panvox.exe -Log`); **打「seek 到某位置才出现」的病**加 `PANVOX_AUTOPLAY_SEEK_TO=<秒>`(开播后自动 seek 到该位置, 支持小数; `PANVOX_AUTOPLAY_SEEK_PAUSE=1` 到位后暂停钉帧, 截图 HUD 核对实际落点; 也可 `python tools/probe_shot.py <out.png> <media> --seek-to <秒> --seek-pause` 一步到位)。**用户点开=带续播 seek**(position 从 history.json 读), 全部测试从零起播会漏掉 seek 路径的病(实测教训)——续播场景补 seektest(§5)。需要真实 UI 操作(切轨/倍速/字幕切换)在 Windows 本机可加载 avox-cli skill 用 ops 找字点击。
5. **读日志分析**(§3) → **先对 §4 病族速查**(签名吻合直接给结论+核修态, 对不上再立新案) → 归属判定 → 结论 + 建议; **闭案=修复后同参数重跑复现路径 + ctest + 离线矩阵零回归**(`cd ../avox-test && python script/testenv/play_regress.py --offline`, 基线 49P+4dav 既有挂) + 常规直链复跑确认零影响。

## 2. 各平台带日志复现
**Windows**(本机或 ssh pc):
- 首选启动闸(先哈希同步引擎 dll, 堵"宿主跑旧引擎"): `panvox.cmd -Log` → stdout/stderr 落 `%APPDATA%\panvox\logs\panvox-<ts>.log`(+.err, 自动留最近 20 份)。
- exe 直启: `panvox.exe -Log` → 进程内档 `panvox-log-<ts>.log`([dart]/[info]..[debug] 前缀)。**无 -Log 参数零日志**(9/26 门控定稿), 老构建没有此开关。
- **一键真实链路复现**(杀实例后): runner/Release 目录 `PANVOX_AUTOPLAY=<url> ./panvox.exe > 日志 2>&1 &` —— shim stderr+Dart print+引擎 log 全落一个文件, 免手点走 app 真实开片链路(硬解/GPU 合成/窗口直渲全真)。
- 崩溃: Release 目录(`app\build\windows\x64\runner\Release\`) `AVOX_*.dmp`(app 自带 handler, mtime≈崩溃时刻), `python tools/analyze_dump.py <dmp>`; 疑冻结看 `%APPDATA%\panvox\freeze\`。
- 排 crash 需带符号引擎: `AVOX_BUILD_TYPE=RelWithDebInfo python build_windows.py`(avox 仓) + `tools\deploy_runtime.ps1 -EngineConfig RelWithDebInfo`(见部署文档 §5)。

**Mac**(ssh mac): shim 无 -Log 开关, 靠终端捕获+os_log。
- **用户实际启动位是 `~/Applications/panvox.app`(非 build products)**, 引擎静态链进 libpanvox_native.dylib(无独立 avox dylib); 换引擎 `bash tools/deploy_macos_shim_app.sh`(部署文档 §3.3)后核对启动位 dylib 已刷新(`strings <dylib> | grep <修复特征串>` 最实)。
- **Finder/launchd 启动引擎 stdout 全丢**(os_log 也常无条目、无自有日志文件): 复现必须终端带重定向重启 `nohup ~/Applications/panvox.app/Contents/MacOS/panvox >/tmp/panvox-run.log 2>&1`, 否则出事后无日志可读。
- **本机(0927)实测: 上面这条 nohup 法可能走不通** —— 直启沙盒 app 会死在 `_libsecinit_appsandbox`(SIGTRAP, 非 app 崩溃), `log show --predicate 'process == "panvox"'` 也无条目。绕法: 经 LaunchServices 启动(`open -a`, 需传 env 用 `open --env`), 或**直接降级到引擎级复现**(§5 avox_cli / vsynctest), 后者对「解码/渲染时序」类病等价且更可控。
- **xcodebuild 在本机沙箱下会被拦**: `CreateBuildDescription failed` / `Unable to write manifest.json` / `Operation not permitted`(写 `~/Library/Developer/Xcode/DerivedData/.../info.plist` 与 SWBBuildService)。**解法 = 前台执行 + 关沙箱**(0927 实证有效: `xcodebuild -project avox.xcodeproj -configuration Release -target ALL_BUILD build`); 注意**放后台跑时即使带了关沙箱旗标也不生效**, 仍报同样的错。改 `TMPDIR` 到工作区内、`-derivedDataPath` 均**无效**。兜底=手写 clang++ 链接命令直接产出可执行(清单见 avox-macos-playmatrix-runner skill 附录), 或 `cmake -S . -B build/macos/avox -DAVOX_ENABLE_CLI=ON` 重配后走 ninja(`avox_cli` 目标默认关, 见 src/CMakeLists.txt)。
- 引擎日志备选: `log show --last 10m --predicate 'process == "panvox"' --info`, 或 `log stream` 边播边收。
- 「卡住/停止」先查 `~/Library/Logs/DiagnosticReports/` 有无 panvox .ips 区分**崩溃 vs 冻住**(无 .ips=冻死非崩); 解码自愈停顿 ≤1GOP(dropped 数百帧≈9s)易被用户当「停止」, 别误判。

**Android**(adb): `adb install -r app/build/app/outputs/flutter-apk/app-release.apk`; `adb logcat -c && adb logcat -v time -s avox:V flutter:V`(引擎 tag=avox, Dart tag=flutter); 手机在蜂窝网"全源打不开"先 `adb shell svc wifi enable`(历史陷阱)。

**iOS**: 模拟器 `xcrun simctl launch --console-pty booted com.panvox.panvox` 收 stdout; 真机无控制台, 依赖用户复述+ repro 降级到 Mac/Windows。

**Linux/WSL**(= Windows 本机里的 WSL Ubuntu, 经 `wsl bash -c` 进场): 仓库在 WSL 内 `~/github/{panvox,avox}`; 起 app `~/github/panvox/app/build/linux/x64/release/bundle/panvox 2>&1 | tee /tmp/panvox-run.log`, **必须普通用户**起(WSLg 下 root 连不上用户 Wayland socket)。wsl.exe 实操: 复杂命令写 .sh 进去跑(引号经 Windows 层易被吃), 路径用 wslpath 转, bash 脚本忌 CRLF, **wsl.exe 的 stderr 提示常是乱码**(编码问题), 以命令正常输出为准别被它带偏。画面恒走 frame_poll CPU 车道, 与 Win/mac 硬解车道表现不可直接互推; 引擎构建车道归夜班 openclaw 会话, 动手前 `ps aux | grep build_linux` 确认没人编别抢树。

## 3. 日志分析
- 按目录 mtime 取最新文件读; **严禁全盘/递归搜索日志**(必超时)。大文件先看头尾定时间戳格式, 再按用户说的时刻 grep 时间窗。
- 引擎行([MPx] 实例前缀 / [FF] ffmpeg / [ZL] zlmediakit / io create-open result / addVideoDesc-addAudioDesc / state from X to Y / playing↔buffering / av not align)与 **analyze-log skill 同一套判定表**, 卡顿/打不开的细判直接加载它, 不在本 skill 重复。
- panvox 层特征行: `[panvox-boot]`(启动顺序), `[panvox-shim]`/`[panvox-runner]`(shim/GPU 适配器), `[dart]`(-Log 进程内档的 Dart 行), `[avox][级别]`(引擎桥行), `watchdog armed`(冻结名片机制), unplayable 记录(打不开自动下墙: 引擎败+文件头非容器才标, **网络类错误永不标**, 见 unplayable.json)。
- 归属判定顺序: ①io open 失败/超时/403/404/503 → 源或网络(签名限时源失效**严禁同参自动重播**); ②流信息缺/decode create fail → 编码不支持或硬解问题; ③opening 长停 → IO 慢/假成功; ④buffering 频繁 → 查队列与丢包行; ⑤Dart exception/zone-error → app 层逻辑; ⑥无日志直接退 → 走 dmp 流程。**最有力的归属证据=本地同文件零卡 + http 卡**(病在 http IO 路径)。
- **open 成功却不到 playing 先分两族再归属**: 有 `avcodec_open2 failed`/`videodecode error … open failed` 行=**编码族**(归属②, extradata/编码支持面, 见 §4 wmv3 族); `io open result: success` 后整段静默(无 open_input_ms/流信息/任何错误行)=**长停族**(归属③, 嫌疑 demuxer 卡在容器头或服务端读行为)。两族判定序都落同一档, **归因别并类**(0927 夜巡 7 片两族各半)。
- **时间线重建**: panvox_stdout.log 在 Release 目录常是旧文件; stdout 未重定向的实例死了就无日志(无法尸检, 只能收敛怀疑面让用户带日志复点)。重建用户操作时间线用 media_info.json 的 at/via 逐条排(实测: [FF] 刷屏真凶是几分钟前另开的另一片, 靠它定案)。
- **dll 考古**(判"修复是否真的编入部署位"): 内嵌 commit_hash/build_time 不可信(§1.0); 看 `build/windows/avox/src/<模块>.dir/Release/<改过的文件>.obj` mtime vs 源文件 mtime vs 提交时间; Windows 启动闸 Copy-Item **保留源 mtime**(runner 目录 dll 的 mtime=源构建时间≠同步时间)。
- **seektest 判读**: post0 near-black 且不同目标位置统计全同=seek 前在飞残帧伪影, **勿当 bug 追**; 验收看 post1+alive+耗时指标(测试判定行可能因此恒 FAIL, 属测试口径非缺陷)。
- cli 默认级别看不到 [FF] 桥日志, 要 `-loglevel verbose`(但 open_input_ms/first_frame 等指标与级别无关)。FFmpeg 日志行出处用本机源码树 `D:/Work/github/ffmpeg/` grep 字符串定位最快, 别猜(引擎=FFmpeg 9.0.1, avformat-63)。
- **本机是 BSD grep**: `\|` **不是**「或」, 多选一律 `grep -E "a|b"`——用 GNU 写法会**静默零命中**, 极易误判「日志里没有这条」(0927 实证: `poc restamp active` 明明在, 被 `\|` 吃掉差点翻案)。
- **取码流头字段(判显示序)用 `ffmpeg -bsf:v trace_headers`**: `-i <file> -c:v copy -bsf:v trace_headers -frames:v N -f null -`, 从 stderr 抽 `pic_order_cnt_lsb`/`slice_type`/`nal_unit_type`(本机无 ffprobe, 只有 `~/.local/bin/ffmpeg`)。这是「重打戳对不对」的 ground truth, 比看引擎输出反推可靠。moov 在头时可只下前缀(`curl -r 0-N`)建可解析的局部文件。
- 需脱离 app 隔离引擎时用 §5 探针; 细化旋钮: 引擎级复现按症状加 `-log-packet`(包时间/PTS)/`-log-decode`/`-log-render`(app 内无此开关); `PANVOX_THUMB_TRACE=1`(抽帧链); FFmpeg 桥默认压在 WARNING, info 级需引擎侧另开。

## 4. 已知病族速查(定谳在案, 排查先对表)
签名吻合直接给结论并核对修态(commit 是否已在部署位, §3 dll 考古); 对不上再立新案。

**open/起播**
- 容器头解析失败(EBML header parsing failed 等) → 拿到的非容器: 假 mkv(.torrent)/改后缀 FLV → 源端假片, §1.2 体检定真身。
- wmv3 拒播/有声无画: `io open success`→流信息正常(wmv3+wma2)→`avcodec_open2 failed -1094995529` 且**无任何 [FF][wmv3] 行**(extradata 空即静默拒, vc1_decode_init 形态)→永不到 playing; 或音频代打到 playing 但 video 帧队列恒 0(有声无画)。**判据: 4 字节序列头 extradata 全拒, 5 字节同库可播**(对照 `add video config data:` 字节数, 0927 夜巡 6 片实锤: 拒 `0x4FF11A01`/成 `0x4FF1080100`) → **已修 ee8bf7b(0927)**: 真根因不在 FFVDecoder——MediaPlayer::onPacket 视频分支 `data.size<=4` 垃圾包闸把 4 字节 vconfig(序列头)整包丢掉, aconfig 无此闸故音频 config 照到; 修法=vconfig 豁免尺寸闸。签名仍见于此族=查部署位修态(dll 考古)。
- open 后卡死: `partial file` ×数千同秒 + `seg fetch seek misland got:-541478725(AVERROR_EOF)` + Dart `clock-leak guard seek(0)` 死循环 → 服务端时变收尾连接→FFmpeg http filesize 认知被响应污染(干净 EOF 只在 off≥filesize, 无 "Stream ends prematurely" ERROR 行是判据)→eof 闩+seek 失败路径不清闩→段断供雪崩(0926 定谳未修, 修法=seek 败清闩+EOF 未到真尾重建通道)。**先验文件/服务端(curl 全文件顺序流)排除源端, 再对表**; 二次复现可能不卡(时变), 别因复现不出就翻案。
- http 直链 open 卡 10 分钟+(迅雷逐 GOP 落盘 mp4, 全文件数千个 mdat, FFmpeg 顶层扫描每 mdat 一次 http 断连重连) → 已修 39aa4aa(http 预扫+AVSEEK_SIZE 谎报早退)。
- **open 慢≠败(15s 驻留误判 OPEN_FAIL)→AVI 慢开两族分治已修(ce86db8+271db52+393f954, 0927)**: 同症状两机制——①RIFF 申报大小≠实际(迅雷类虚报, 321 CEAD146 实锤: 申报 1.07GB vs 实际 1.39GB)→ avi_load_index 从申报 movi_end 静默逐块爬找 idx1 把 315MB 尾区走网爬完; **且此类半截片尾本无 idx1(尾部 16B 实锤)→ seek 钳在已读前沿是数据缺失的固有事实(修复前同, 爬找本就空手), 非引擎可治**。②ODML indx 主索引(383 デジタルマスタリング 实锤: 布局健康申报=实际)→ read_odml_index 逐 ix## 叶 ~32MB 步进跨全文件追读(AVOX_SEG_TRACE=1 段缓存日志实锤 37 次 ~150ms 重连)。修法分治: ①剪尾线=申报+8MB(线外段抓快速失败/读回 EOF 与钳尾同通道)+open 窗口内降 wrapPb->seekable 跳索引; ②open 传 demuxer 私有选项 `use_odml=0` 只免逐叶追读, **idx1 照常 open 期加载(就在 movi_end, 半秒), 索引/seek/时钟全保留**(实测 383 open 425ms, seek 5400s 落 5398.8s)。诊断标记: warn 行 `avi tail clip on`(①) / `avi odml direct-idx1 on`(②); 签名仍见=查部署位修态(dll 考古)。**教训在案: AVI 的 pts=ast->frame_offset 读包计数器, 裸字节跳读位(绕过 avi_read_seek)时钟必错(实测 616MB 实报 15s), AVI seek 只能走索引或钳边, 字节估算路线不成立**。夜巡 15s 驻留对真实慢源仍可能掐在 open 前, 复核法不变: 单片 `avox_cli pl -t 30` 复测到 playing 即改判「慢开可播」。
- 开片即崩(avsubtitle_free 栈, 播 PGS 字幕触发; **cli 不渲字幕故不崩=最大迷惑点**) → ffmpeg dll 与 .lib 序号错位 → 已修 c08adb4(按名字重生成导入库); 根训=dll 与 lib 必须成对更新。
- 网络源硬解首帧慢(~4.6s)被 5s 看门狗误杀→vulkan 接棒炸→软解 GOP 中段缺参考, 起播空转 ~15s → 已修 2c2444b(供给窗看门狗+openFailed 瞬时降级+回退吸 IDR)。

**seek 后**
- **seek 落尾/越尾钳尾先分两态**: 健康态=短暂 buffering 后 `io complete`/completed(短片钳尾常态, 0927 夜巡 133 片); 病态=滞留 buffering 无 completed, 三形态——①尾部段 `seg fetch seek misland got:-541478725(AVERROR_EOF)` 重试转 `read frame failed EIO` 源被闩死——**已修 1bcc35a(0927)**: want 恒 256KB 段起点+got=EOF 实锤=钳尾 seek 后 demuxer 越界读, 取段 10 次空转重试后 wrapReadAt 把文件尾事实报成 EIO 当真故障; 修=越界段快速失败+wrapReadAt 按文件大小分真伪回 EOF+寻位前清 eof 闩; ②落尾帧队列空恒 buffering——**部分片已定谳另一宗**: 视频轨 `invalid`(add video track: invalid-…, VideoTrack unsupported codec -1)=引擎不认的老编码(SVQ3 .mov 实锤), 纯音频代打而音频轨仅数秒, seek 越过音频终点后零供给恒 buffering 不 completed; 已补 SVQ3 映射修(bd09de9, 0927), 见同族签名先查 `add video track` 是否 invalid; 其余非 invalid 片=①形同根已随 1bcc35a 待二刷验证; ③改后缀 FLV flvEstimateSeek fallback 落点后 demux 无下文(683)→ 与①同根(misland 闩), 已随 1bcc35a 待二刷验证。
- mkv seek 卡 ~3 分钟(matroska 内部前向扫描兜底) = avio error/eof 闩残留毒死尾部 Cues 解析 → 已修 8d61674(wrapSeekCb 清闩+读满文件尾短段入库)。**真机签名(0927 Android 实证)**: `state playing→seek` 后 86s 零状态转换, 伴 `[FF][matroska,webm] Read error at pos ≈ filesize-5KB`(=尾部 Cues 读被闩杀) + seg-diag `read failed n=AVERROR_EXIT(-1414092869) httpEof=1`; 同片同源新引擎 seekstorm PASS 且 open 6ms vs 旧机 9.5s。**先核 banner(§1.0)再查此族——病根常是"修复没上到该设备的引擎"; 但 banner 是 configure 烤的, 增量重编引擎不重 configure 时新库报旧戳, 判新旧行为以库指纹/md5 为准**。
  - **残宗未修(0927 深夜, 换新库后 Android 真机仍现)**: 8d61674 只除"永久闩死"; 一次 cmdSeek 内 interrupt 窗口(AVERROR_EXIT=avio interrupt 自家中止, avio.c:521, 引擎"打断窗口内 avio_seek 恒吐 EXIT"已知行为)连环杀两段关键读——①在飞取段(seek 命令恰落在 prefetch 读中, WiFi 300ms/段必撞, localhost 微秒级撞不上=桌面 A/B 全绿的盲区); ②重连后的尾部 Cues 段(读到 225KB 再被 EXIT 杀, pos 恒=filesize-5KB) → matroska 退化为从早期 cluster 线性前扫(手机上表现为 seek 20s+ 无进展)。修复域=IOParseFF seek 暂停/interrupt 窗口与 fetchHttpSeg 重连的握手(777/809/1186/1331/1377 注释链), 动手前先想清 seekTo ack 纪律。缓解(已给用户): 关「记住播放位置」免开播自动 seek 撞窗口。
- 改后缀 FLV(无 keyframes 索引) seek 转 25~46s(顺序整扫, 耗时=目标偏移÷实测吞吐可精算对账) → 已修 flvEstimateSeek 字节估算直跳(四点位 0.4~0.6s; IOParseFF bFlvFastSeek 门, 0926 落地, 提交态 `git log -S flvEstimateSeek` 自查); 家族=所有迅雷改后缀网络 FLV。**关键认知**: flvdec 播放期按关键帧/音频包自建流索引但 flv_read_seek 恒不用(委托 avio_seek_time 需 pb->read_seek→ENOSYS); 单点 seektest 绿但拖动(密集 seek)仍冻 → seek 命令层有合并, 杀伤在 ack 等待 200ms 撞 http 重连退避(1s 不可打断)→快速 seek 门(要求 IO acked)全关退回整扫+并发撕 demuxer → 已修 ack 上限 1.2s(IOParseFF seekTo); 复现/回归用 seekstorm 探针(samples/functest, 密集连发 seek 判帧流恢复)。
- seek 进片尾 buffering 死锁(剩余 <垫子 2s 恢复门闸永不满足 + avio pb->error 闩致 EOF 永不到, 读线程 60 次/s 空转) → 已修 a501667(门闸 bIOComplete 逃生+cmdSeek 复位+清闩)。
- seek 后播一会卡一会、pos 半速爬+周期 buffering(rip 音频 mega-chunk 致视频/音频双区读相距数百 MB, 每段一请求 ~150ms 建连) → 已修 af17043(顺流预读 8 段+LRU 16MB); 多段拼装 rip 的常态形态, 会再现。keep-alive(persistent)在极空间实测有害, 默认保持 0。
- Mac seek 冻画只有声(pos 照走), 日志 [vt] BadDataErr(-12909) 风暴数百条 = VT 吃到坏 NAL(seek 落点簇拆出的全零 PPS)后 session 永久 wedge; FFmpeg 车道宽容坏 NAL 故 Windows/软解无恙 → 韧性已修 88b0135(连击→扣帧等 IDR 重建会话, 修复后表现为 ≤1GOP 自愈卡顿); 根因(PPS 视图清零)在 a02 施工域待协调。
- Mac **反向** seek 没反应/进度条弹回旧轨继续走 = VT flush 不丢在飞帧(3 帧旧帧)+VideoTrack restamp 把新流逐帧改戳回旧时间轴 → **未修**; restamp 短路探针已实锤机制(/tmp/probe0926), 修法=flush 世代号+restamp 对落后游标首帧重锚(归 a02)。
- seek 后长冻(GOP≥20s IDR 稀疏, 落点非 IDR 放行缺参考) → 未修(a02 门闸域); 另有渲染侧时钟钉死变体(pos 冻在目标、IO 背压读不到 EOF)。

**播放中**
- `[FF][mlp] Stream parameters not seen` 刷屏(数十条/s) ± 位置 18 倍慢放、无 buffering 状态 = TrueHD 轨车道错配 → 已修 96e205b(MLP 独立 ACodecId)+f58bc94(同文日志折叠); **先用 media_info.json 时间线定刷屏是哪片开的**(常是几分钟前另开的 TrueHD 片); TrueHD 碎片轨(数千包/内容秒)数据完好可解≠损坏。
- 全片**每秒周期跳帧**(快进-冻结循环)、无 buffering、声音正常, 碎片音轨片(TrueHD 40采样/包=0.83ms, 实测1201包/s) = 音频采样游标 ms 整数截断: `getAudioFrameMs(40采样)=0`→游标冻结→解码 pts 漂 ~1s 重锚→音频钟锯齿→视频钟被拖成快进/冻结循环 → 已修 c3f92b0(getAudioFrameUs 微秒游标+WindowRender 升 fps 上限越界伴修); A/B 判据: 修复前视频 zeroDelay 56-78 次/5s 持续(对冻结钟追赶), 修复后 0; 全片解码扫(vptsscan: 视频输出零缺口+音频包 0.83ms/1201包s)定性「片源干净→渲染侧」。
- 全片**每几秒周期跳一下**(丢帧追赶)、无 buffering、无 crash, **Windows 正常仅 Mac(VT)跳** = 丢 ctts 的 B 帧流 POC 显示格 ms 截断: PocRestamper(eb106f0)激活后 pts 按 33ms 平坦格推进, 对 30000/1001(33.367ms) 每帧欠 0.367ms→视频钟持续落后音频→每~4.5s 攒满丢帧门限静默丢一帧(drop 日志被注释); **FFmpeg 车道输出走 dts 链不吃包 pts 故 Windows 无感, VT 车道透传包 pts 吃满漂移**(IOSVDecoder 用 packet.pts 喂 CMTime) → 已修 79abb44(显示格 µs 化, 与 c3f92b0 同 ms 截断族); 判据: cli -log-packet 看包 pts 显示序增量恒 33(旧病)/33 与 34 交替(修复); 片源特征: moov 里 0 个 ctts 有 stss, 迅雷拼装 mp4 高发。
  - **残宗已修(0927, 同族同签名, 别因「79abb44 已修」就结案)**: 同类片源仍每 **2s** 跳一下。根因=PocRestamper::updatePoc 的 IRAP 分支原为 `fullPoc += pocStep`, 用**解码序前一帧**的 POC 定位 IDR 显示格, 而解码序前帧不是该 GOP 显示序末帧(自适应 B 位置) → IDR 抢到已被占用的显示格(**重复 pts**)+ 留一个空格; 重复 pts 触发 VideoTrack::restampFramePts 的 `pts < lastInPts + nominalFrameMs/2`(=16ms) 单调守卫 → 后续帧被强制成**纯 33ms 格**(29.97 真值需 33/34 交替)→ 每帧快 0.367ms, ~43 帧后累积超 16ms 阈值**一次性跳回(16ms 半帧)** = 视觉抖动, 每 IDR(60 帧≈2s)一轮。
    - **判据三连**(缺一不可): ①`grep -E "restamp|reorders"` 见 `poc restamp active: ... (ctts lost)` 或 `SPS VUI declares reorder frames`; ②`-log-decode` 输出序增量出现 **16**(半帧), 且 34ms 占比仅 ~18%(正确应 36.7%); ③`-log-packet` 里**每个 IDR 的 pts == 前 2 帧的 pts**(撞格, 实测 10 个 IDR 中 7 个)。
    - **软解对照干净**(34ms 占比 37.2%, 输出走 dts 链严格单调 → 不触发守卫)= 硬解 VT 车道独有, 与「Windows 正常」一致。
    - **修法(已落地)**: IRAP 分支改 `fullPoc = maxFullPoc + pocStep`, GOP 内维护 `maxFullPoc`; 另加 **VUI 预激活**(SPS 声明 `bitstream_restriction_flag && num_reorder_frames>0` 即首帧 `activate`, 消掉等 POC 倒挂实证期间的接缝错位; 首帧 dispUnits=0 恒等, 带 ctts 的正常流下一帧被 `pts!=dts` 旁路闸拦下)。
    - **验收数据**(同片源 12s, Mac VT 硬解, 修复前→后): IDR 撞格 **7/10 → 0/10**; 34ms 占 (33+34) **17.9% → 37.0%**(理论 36.7%, 四次重跑 36.9~37.1% 稳定); 16ms 半帧 **4 → 0**; IDR 间距 **1968/1969 抖动 → 恒定 2002ms**(=60×33.367ms)。
    - 关键片源参数: POC lsb 仅 5 位(`log2_max_pic_order_cnt_lsb_minus4=1`, wrap=32), 自适应 B 位置(P 间距不匀), stss IDR 每 60 帧。**注意 moov 可能在文件头**(本例 moov 11.7MB 在 ftyp 后, 与「moov 在尾」的常见拼装片相反)。
    - **验证坑**: 改动给 PocRestamper **加了成员**(`sizeof` 变), 只重编 `.cpp` 再手工重链会得到「新头文件对象 + 旧头文件库」的错配 → 库里 `make_unique<PocRestamper>()` 按旧尺寸分配, 新 `.o` 写新成员即**堆越界**; 症状是 `BUG IN CLIENT OF LIBMALLOC: memory corruption of free block`(EXC_BREAKPOINT/SIGTRAP) 而栈落在无关的 AppKit/CoreUI/CoreSVG —— **极具误导性, 必须整库重建**。
    - **同族第三宗(已修 0927, seek 后抖动)**: 连续播放正常, 但 **seek 到任意位置后画面持续抖动**(用户报「seek 到 1:24:13 还是抖」)。根因=PocRestamper 输出的是**帧计数轴**(`pts = pts0 + 显示位×帧长`), 只在**连续播放**时与容器时间轴重合; seek 后容器 dts 大跳而轴**原地续数** → 与容器错开整段(实测 seek 到 5053s 后轴停在 **10.4s**, 容器已到 5051s, 偏差 **5,040,626ms**≈84min) → `AVSource::alignPacketPts` 报 `diff too large,no sync` 把**音频拉到视频轴**(≈19s) → VT 按错误时间轴排帧 → 持续抖动。
      - **原有 seek 自愈为何没救**: feed() 里只认 `dts < lastDts - 帧长×2`(**回跳**), **前向 seek 完全不触发**; 且轴值恰好与容器陈旧值重合时不写回 → `pts!=dts` 旁路闸也永不触发, 永久错位。
      - **判据**: `-log-packet` 里 seek 落点首包的 `pts` 与 `dts` 差**数千万 ms**(正常流两者只差容器毫秒取整, 实测 ≤7ms), 并伴 `alignPacketPts ... diff too large,no sync`; 落点必是关键帧(引擎 seek keyframe gate 保证), 故一定有重锚机会。
      - **修法(已落地)**: feed() 在 **IRAP** 上校验「本轴 vs 容器时间」, 偏差超 `kRebaseOffMs`(=1s) 即判 seek 落点, 把显示格原点重锚到该 IRAP 的 **dts**(IRAP 解码位次==显示位次, 其 dts 即显示时刻; 用 dts 而非 pts 因 dts 单调无野值)。
      - **阈值不可省**: 无条件「每 IRAP 重锚」会把轴钉到容器时钟, 而本片**首样本 dts 自身偏离均匀网格 7ms** → GOP0→GOP1 边界冒出一次 **41ms 杂格**(实测), 反而退步; 加阈值后连续播放与修复前逐格一致。
      - **验收数据**(同片源 seek 到 5053s, Mac VT 硬解, 修复前→后): 落点视频 pts **10437 → 5051063**(=dts, 与音频 5051072 差 9ms); `diff too large` **有 → 0**; 解码输出间隔 **33:734/34:108/35:103/36:12/133:1 → 33:620/34:358**(34ms 占 **12.8% → 36.6%**, 理论 36.7%)。同版连续播放 150s 零回归(34ms 36.7%、半帧 0、IDR 撞格 0、间距恒 2002ms)。现场判读认日志 `poc restamp rebase: axis off <N> ms (seek)`。
- **后向 seek 后画面脱离音轨**(已修 24ff54d, 与上面同族但**不同组件**, 全平台通用): **往回拖进度条**后画面时间轴停在旧位置, 与音轨各走一条。根因在 `VideoTrack::restampFramePts` 的单调守卫(`pts < lastInPts + 帧长/2` → 续格 `lastInPts+帧长`), 它只认倒跳、把 seek 重入的大倒跳当成 B 帧重排乱序; 而后向 seek 时旧位置残留帧的 pts **大于**目标位, 逃过 `seekDiscardPts`(只丢目标位**之前**的帧)进入帧队列并顶高 `lastInPts` → 之后新位置的帧全被一路续格前推到旧轴。**前向 seek 的残留帧 pts 小于目标位本就被丢弃, 所以只有后向拖中招**——「某些时间段/某些操作才抖」的一种典型成因。
  - **判据**: `-log-packet` 与 `-log-decode` 同时开, 比较同一时刻的**包 pts** 与**解码帧 pts**; 后向 seek 后两者相差整段(实测普通片 `wall_long.mp4` seek 200s→20s 后 包 pts=33567 而解帧 pts=213299)。正常时两者只差管线延迟(实测恒 ~6.9s)。
  - **修法**: 守卫前加大跳旁路, `|pts - lastInPts| > kFrameJumpMs`(=1s) 即以本帧为准重开格。重排乱序/容器爆发戳都是帧级小跳(实测 ≤100ms 级), 1s 门限不会误伤。
  - **验收**: `wall_long` 后向 seek 后 包 pts 34233~46233 / 解帧 pts 27333~39333 同轴(差恒 ~6.9s); 本片后向 seek 60s 稳态 34ms 占 **36.4%**、前向 seek 3000s 稳态 **36.7%**、半帧 0; 连续播放 150s 零回归。
- 全片**零规律散点跳画**(一次一帧洞)、日志 `[FF][h264] Invalid NAL unit size (声明>实际)` 与 `partial file` 风暴同现、文件 moov stco/stsz 逐样本对字节完好 = 网关按连接随机掐杀读段: 每次抓段=avio_seek 新建连接都过一次掐杀彩票, 重试耗尽后 EIO 漏给 avio_read 把已缓冲半截样本当成功, mov.c 拼出截断包喂出 Invalid NAL→丢 AU→跳; 中文件的 moov 表区(如 4.2MB)再读被掐=同源。→ 已修 e409b32(抓段重试预算 3→10, 单连被掐率~25% 时连杀 10 次≈百万分之一); **判据: 同内容区段换时间重放 corrupt 位置漂移=读路径, 位置钉死=文件真坏**(curl+moov 对账定谳, avcc 长度前缀逐样本比对)。
- 播放时间来回跳+无限 buffering = 拼装 mp4 音轨锚文件头(前 60s 假音频 stco 锚在偏移 48, 每个音频包把读位拉回文件头跨几十 MB 重连) → 已修 a0107be(http 段缓存+锚点钉住)。
- 直链反复 buffering 无时间跳 = 吞吐临界非 bug → 垫子默认已 2s(2a200e9, 首帧/秒开不吃它); 直播延迟敏感经 `mp.delay.ms` 调回。
- 花屏伴 rtp 丢包/packet dropped=网络; 从 P 起解不自愈(持续到 GOP 边)=落点缺参考; 1~2s 自愈=渲染突发/竞态另有因。

**网络环境(本机代理/TUN, 非引擎病)**
- IPTV/m3u 列表与国内流普遍慢、超时、周期 buffering, 而 NAS/局域网源全正常 → 先查本机 TUN 接管: `route print` 见 Meta Tunnel/Wintun + `tasklist` 见 verge-mihomo(Clash Verge)=全机流量过代理。**对照法: `curl` 默认路由 vs `curl --interface <物理网卡IP>` 直连**(0923 实锤 CCTV1 列表 TUN 19s→直连 1s; 0926 复测首响 3.2s vs 1.2s); 修法=Clash 给国内直播域名加 DIRECT 规则或关 TUN, 不动引擎。**绑定源地址法在部分环境只是绕路成功, 直连腿 000 时先核对绑定语义再下结论**。
- 免费聚合清单(live.zbds 类)两大常态别当 app 病: ①大量「频道」=点播循环(HTTP-FLV 服务端把整剧/整片循环推流, 0926 抽样 542 频道 107 个循环体, metshop 一台 66 个——循环是内容本身, 永不完播); ②死链/整台服务器超时常态(145 台流服务器抽样过半 8s 无响应)。


## 5. 引擎级复现与探针
- `tools/engine_play_test.exe`(须与 avox.dll 同目录, tools/build_engine_play_test.bat 出; `engine_play_test <url> [sec=8] [hard=1] [vulk=1]`, 免窗可过 ssh); 或 avox_cli play(加载 avox-cli skill: `-io ffmpeg` / `-transport tcp` / `-log-packet` / `-log-decode` / `-loglevel verbose`)。
- **seektest**(samples/functest): `seektest <url> <秒> <前缀> [-hard]`, 判定行 `[AVOX][TEST] result=PASS`; 验续播/seek 修复必备(判读见 §3)。
- **seekstorm**(samples/functest): `seekstorm <url> [count=24] [intervalMs=120]`, 密集连发 seek 复刻拖进度条; 判定=风暴后回 playing+末 5s 帧计数推进。单次 seektest 绿但用户拖动冻 → 用它。
- **stutterprobe**(samples/functest): 帧到达间隔+buffering 段+pos 推进, 验供给类修复; 判定要帧数下限防空判。
- **Mac 探针族 /tmp/probe0926**: 链接级覆盖手法——改 IOSVDecoder.mm 等单编 .o 放 libavox.a 前链接即换实现, 不碰共享树(编译参数从 flags 反推); Windows 侧对应 build/probe_seek、build/probe_tailseek(python minidump+ctypes dbghelp 挂 pdb 符号化卡死线程栈, 线程全在 ntdll 等待时找自家读/解码线程的分支)。
- **容器病态结构 python 手解**: stco/stsz/stsc 分布暴露锚位与碎片化(track 块数/块率量化); EBML walker 验轨道表; mp4 fullbox 的 entry_count 在 version+flags 之后(+4); 结构对账别心算对齐用 python。

## 红线
- **凭据脱敏**: sources.json 的 user/pass/token、URL 里的 session/sign 不进结论、不复述、不回显。
- 单实例纪律; avox 构建树有并行会话在用别抢(尤其 WSL 车道)。**并行会话同文件施工**: 提交挑自己 hunk(python 挑选+`git apply --cached -C1` 分摊)别捎带别人改动; dll 被对方 playtest 锁住(LNK1104)等别杀; 动 a02 施工区(AVSource.cpp/VideoTrack.cpp 等)先确认归属。
- 用户数据目录默认只读; 重扫/清缓存/删 unplayable 等写操作先征得用户同意。
- 结论必须附证据行(时间戳+关键行内容), 找不到证据行就明说未复现, 严禁臆测补链; 全绿未定谳就明说全绿+收敛怀疑面, 别硬安根因。
