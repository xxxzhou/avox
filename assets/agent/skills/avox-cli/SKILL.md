---
name: avox-cli
description: avox_cli 命令速查:GUI 自动化(ops 截图/找字找图+动作、input 键鼠模拟、vision 识别调参)与媒体采集(play 播放、record 录流、device 摄像头/屏幕/窗口采集)。凡要实际操作桌面/窗口或播放录制流,先加载本技能再按指令用 pwsh 执行。播放失败的诊断分析走 diagnose-play。
whenToUse: 需要截图取证、在窗口/桌面找字找图并点击、键鼠输入、验证识别阈值、实际播放一条链接、录流成文件、枚举/预览/录制摄像头屏幕窗口时。
---

全部经 pwsh 执行 avox_cli 命令完成(它们是命令行,不是 agent 工具)。参数拿不准就跑 `avox_cli <命令> -help` 现查,不要凭猜组合;路径含空格/URL 含 & ? % 时该参数整体加双引号;pwsh 超时 = 预期时长(-t 秒数)+60s。

【ops —— 窗口/桌面截图与定位动作,一步式】
- 目标二选一: -w <窗口标题子串> 或 -d <桌面索引>(0=主屏);不传默认桌面0。**-t/-p/-O/-M 必须配 -w 或 -d**,不能凭空调用。
- 找字+点击: ops -w 记事本 -t 文件 -a click;找图+点击: ops -d 0 -p logo.png -a click。一次调用完成截→识→动,**严禁拆两步**(先 -s 截图再 -O 识别会重新截图,识别的不是前面那张)。
- 动作 -a: none|click|move|dblclick(默认 none 只定位);阈值 -e [0,1](找字默认0.3/找图默认0.7,识别不稳先调它)。
- 截图存盘: -s,默认 运行目录/screenshots 时间戳命名;-o 指定路径;-c x,y,w,h 截完裁剪。
- 全量识别: -O 整屏OCR 列出全部文字; -M 配 -p 列出全部命中。其他: -l 列可捕获窗口/显示器(不知道标题子串先跑它); -f 前置窗口。
- 输入一步带过: ops -w 窗口 -T "文本" / -K enter / -H ctrl,c (可选 -w 先激活窗口再输入)。

【input —— 键鼠模拟,坐标=屏幕物理像素】
- 点击 input -a click -x 120 -y 80;拖拽 -a drag -x.. -y.. -x2.. -y2..;滚轮 -a scroll -dy -3(-dx 横向,-dy 负=向下,单位滚轮格);右键加 -b right;拟人轨迹加 -humanize。
- 打字 -t 你好 / 按键 -k enter / 组合键 -keys ctrl,s(action 由它们隐含,不必写)。
- 定位来源: 先用 ops 的 -t/-p 拿目标坐标(stdout 有输出),再用 input 做点击以外的动作。

【vision —— 静态图像识别调参,只出标注图不动鼠标】
- vision -s 场景.png 走 OCR;加 -p 模板.png(-p 可多次)走模板匹配;调参 -e 阈值 / --nms 去重 / -r ROI / --green 绿幕透明。
- 要"找到并点击"永远直接 ops 一步完成,vision 只做离线调参与取证。

【play —— 播放URL/流】
- 最简: play -i "<URL>" -t 15(-t 秒后自动退出,0=无限)。弹窗是正常表现(证明在播);不需看画面加 -offscreen 离屏。
- stdout 含 log-file: <路径> 即本次日志;未指定时在运行目录 logs/play_*.log 按修改时间取最新。
- 调参: -hard 硬解 / -transport tcp(RTSP丢包卡顿)/ -io ffmpeg(zlmediakit打不开换)/ -timeout <毫秒> / -sync-type 2(音频PTS异常)。
- 截图取证: -screenshot <目录> + -shot-at <毫秒> 单张 / -shot-interval <毫秒> 周期,通常配 -offscreen。
- 【纪律】含 session/sign 的限时签名源只能采一次,严禁同参重播,重试必须有新链接;play 卡在退出阶段(ZL 析构慢)时日志已完整——kill 进程直接拿日志,严禁因"卡住"重播。深挖加 -log-packet/-log-decode/-log-render;完整诊断流程加载 diagnose-play skill。

【record —— 录制URL/流为文件】
- record -i "<URL>" -t 30,输出自动命名到 records/,-o 指定路径。默认转封装(快、无损),要处理/换编码才加 -tc(配 -hard 硬编码);-novideo/-noaudio 只录单轨。录屏幕/摄像头不用本命令。

【device —— 摄像头/屏幕/窗口采集预览与录像】
- 先 device -list 拿设备索引与名称(stdout 直接列出 video/audio 含 monitor/window 条目);-vi/-ai 选索引或 -vname/-aname 名称子串选;-no-video/-no-audio 关一路。
- 预览: device 默认第0路摄+麦弹预览窗;录像: device -vi 0 -record D:/out.mp4(格式按扩展名)。无时长参数——结束录制=终止进程(pwsh 工具超时会杀进程树,把超时设为想要的录制时长+余量)。

【低频命令】assets 插件资源管理 / python 代跑脚本(run_code 工具已覆盖多数场景)/ voice 全局语音热键:需要时跑 `avox_cli -help` 列全部命令。
