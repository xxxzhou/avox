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
4. **按用户描述触发, 且对齐用户实际路径**: 能自动化就自动化 —— `PANVOX_AUTOPLAY=<可播URL或路径>` 环境变量让 app 启动后直接播该条(免手点; Windows cmd: `set PANVOX_AUTOPLAY=... && start panvox.exe -Log`)。**用户点开=带续播 seek**(position 从 history.json 读), 全部测试从零起播会漏掉 seek 路径的病(实测教训)——续播场景补 seektest(§5)。需要真实 UI 操作(切轨/倍速/字幕切换)在 Windows 本机可加载 avox-cli skill 用 ops 找字点击。
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
- **时间线重建**: panvox_stdout.log 在 Release 目录常是旧文件; stdout 未重定向的实例死了就无日志(无法尸检, 只能收敛怀疑面让用户带日志复点)。重建用户操作时间线用 media_info.json 的 at/via 逐条排(实测: [FF] 刷屏真凶是几分钟前另开的另一片, 靠它定案)。
- **dll 考古**(判"修复是否真的编入部署位"): 内嵌 commit_hash/build_time 不可信(§1.0); 看 `build/windows/avox/src/<模块>.dir/Release/<改过的文件>.obj` mtime vs 源文件 mtime vs 提交时间; Windows 启动闸 Copy-Item **保留源 mtime**(runner 目录 dll 的 mtime=源构建时间≠同步时间)。
- **seektest 判读**: post0 near-black 且不同目标位置统计全同=seek 前在飞残帧伪影, **勿当 bug 追**; 验收看 post1+alive+耗时指标(测试判定行可能因此恒 FAIL, 属测试口径非缺陷)。
- cli 默认级别看不到 [FF] 桥日志, 要 `-loglevel verbose`(但 open_input_ms/first_frame 等指标与级别无关)。FFmpeg 日志行出处用本机源码树 `D:/Work/github/ffmpeg/` grep 字符串定位最快, 别猜(引擎=FFmpeg 9.0.1, avformat-63)。
- 需脱离 app 隔离引擎时用 §5 探针; 细化旋钮: 引擎级复现按症状加 `-log-packet`(包时间/PTS)/`-log-decode`/`-log-render`(app 内无此开关); `PANVOX_THUMB_TRACE=1`(抽帧链); FFmpeg 桥默认压在 WARNING, info 级需引擎侧另开。

## 4. 已知病族速查(定谳在案, 排查先对表)
签名吻合直接给结论并核对修态(commit 是否已在部署位, §3 dll 考古); 对不上再立新案。

**open/起播**
- 容器头解析失败(EBML header parsing failed 等) → 拿到的非容器: 假 mkv(.torrent)/改后缀 FLV → 源端假片, §1.2 体检定真身。
- open 后卡死: `partial file` ×数千同秒 + `seg fetch seek misland got:-541478725(AVERROR_EOF)` + Dart `clock-leak guard seek(0)` 死循环 → 服务端时变收尾连接→FFmpeg http filesize 认知被响应污染(干净 EOF 只在 off≥filesize, 无 "Stream ends prematurely" ERROR 行是判据)→eof 闩+seek 失败路径不清闩→段断供雪崩(0926 定谳未修, 修法=seek 败清闩+EOF 未到真尾重建通道)。**先验文件/服务端(curl 全文件顺序流)排除源端, 再对表**; 二次复现可能不卡(时变), 别因复现不出就翻案。
- http 直链 open 卡 10 分钟+(迅雷逐 GOP 落盘 mp4, 全文件数千个 mdat, FFmpeg 顶层扫描每 mdat 一次 http 断连重连) → 已修 39aa4aa(http 预扫+AVSEEK_SIZE 谎报早退)。
- 开片即崩(avsubtitle_free 栈, 播 PGS 字幕触发; **cli 不渲字幕故不崩=最大迷惑点**) → ffmpeg dll 与 .lib 序号错位 → 已修 c08adb4(按名字重生成导入库); 根训=dll 与 lib 必须成对更新。
- 网络源硬解首帧慢(~4.6s)被 5s 看门狗误杀→vulkan 接棒炸→软解 GOP 中段缺参考, 起播空转 ~15s → 已修 2c2444b(供给窗看门狗+openFailed 瞬时降级+回退吸 IDR)。

**seek 后**
- mkv seek 卡 ~3 分钟(matroska 内部前向扫描兜底) = avio error/eof 闩残留毒死尾部 Cues 解析 → 已修 8d61674(wrapSeekCb 清闩+读满文件尾短段入库)。
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
- 全片**零规律散点跳画**(一次一帧洞)、日志 `[FF][h264] Invalid NAL unit size (声明>实际)` 与 `partial file` 风暴同现、文件 moov stco/stsz 逐样本对字节完好 = 网关按连接随机掐杀读段: 每次抓段=avio_seek 新建连接都过一次掐杀彩票, 重试耗尽后 EIO 漏给 avio_read 把已缓冲半截样本当成功, mov.c 拼出截断包喂出 Invalid NAL→丢 AU→跳; 中文件的 moov 表区(如 4.2MB)再读被掐=同源。→ 已修 e409b32(抓段重试预算 3→10, 单连被掐率~25% 时连杀 10 次≈百万分之一); **判据: 同内容区段换时间重放 corrupt 位置漂移=读路径, 位置钉死=文件真坏**(curl+moov 对账定谳, avcc 长度前缀逐样本比对)。
- 播放时间来回跳+无限 buffering = 拼装 mp4 音轨锚文件头(前 60s 假音频 stco 锚在偏移 48, 每个音频包把读位拉回文件头跨几十 MB 重连) → 已修 a0107be(http 段缓存+锚点钉住)。
- 直链反复 buffering 无时间跳 = 吞吐临界非 bug → 垫子默认已 2s(2a200e9, 首帧/秒开不吃它); 直播延迟敏感经 `mp.delay.ms` 调回。
- 花屏伴 rtp 丢包/packet dropped=网络; 从 P 起解不自愈(持续到 GOP 边)=落点缺参考; 1~2s 自愈=渲染突发/竞态另有因。

**网络环境(本机代理/TUN, 非引擎病)**
- IPTV/m3u 列表与国内流普遍慢、超时、周期 buffering, 而 NAS/局域网源全正常 → 先查本机 TUN 接管: `route print` 见 Meta Tunnel/Wintun + `tasklist` 见 verge-mihomo(Clash Verge)=全机流量过代理。**对照法: `curl` 默认路由 vs `curl --interface <物理网卡IP>` 直连**(0923 实锤 CCTV1 列表 TUN 19s→直连 1s; 0926 复测首响 3.2s vs 1.2s); 修法=Clash 给国内直播域名加 DIRECT 规则或关 TUN, 不动引擎。**绑定源地址法在部分环境只是绕路成功, 直连腿 000 时先核对绑定语义再下结论**。
- 免费聚合清单(live.zbds 类)两大常态别当 app 病: ①大量「频道」=点播循环(HTTP-FLV 服务端把整剧/整片循环推流, 0926 抽样 542 频道 107 个循环体, metshop 一台 66 个——循环是内容本身, 永不完播); ②死链/整台服务器超时常态(145 台流服务器抽样过半 8s 无响应)。

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
