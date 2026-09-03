---
name: analyze-log
description: 分析已采集的 avox 播放日志(渲染日志)。**仅在用户提供了具体日志名/路径**(如 play_*.log / renderer.log / 完整文件路径)时使用;严禁基于"为什么下不来 / 播放失败 / 卡顿"等宽泛描述自动加载。判定 IO 成败、流信息、解码、耗时、音视频同步、playing↔buffering、[ZL]/[FF] 根因。avox_cli 的 play_*.log 在 <运行目录>/logs/, hysp_pc 的 renderer.log 在 %APPDATA%/hysp_pc/logs/。
whenToUse: **仅当用户明确给出日志路径或文件名**(如 play_*.log / renderer.log 等)且要求分析根因时使用。若用户只描述问题现象("为什么下不来 / 卡顿 / 失败"),不要自动加载本 skill —— 应先询问用户日志位置,或在用户提供路径后再加载。不涉及重新采集/播放。
---

# avox 播放日志分析

分析已采集日志(链接可能已失效, 不重播)。日志位置: play_*.log→<运行目录>/logs/; 路径固定, 直接按路径读; **严禁全盘/递归搜索日志文件**(必超时)。

## 输出
- **总结(必写)**: 一句话根因 + 关键判定(IO成败/是否playing/音视频同步/是否buffering) + 归属(源端/网络/本地) + 1条建议。结尾不重复结论。
- **细节**: 逐项1-3句, 正常项一句带过。不罗列原始日志行。

## 判断规则
- **多播放器**: [MPx]前缀=实例, 仅[MP0]=单实例不提; 多个前缀按 MP 分归类, 总结点明个数。不单列标题。
- **证据校验**: 结论前先 grep 核对 io create/open result、add/decode create result、media player state from X to Y、result:fail、buffering timeout 等关键行文本, 找不到=未发生, 严禁臆测; 不摘录原始行; 结论与核对逐字一致。
- **IO失败**: io open fail/超时/拒绝 且无 addVideoDesc/addAudioDesc → IO失败即根因, 给方案(换 -io ffmpeg 或确认链接)后不逐项展开。含 session/sign 的限时源失效重播必再败: 严禁自动重播, 先确认链接有效期/能否提供新链接, 确认有效才可重试(仅换 -io ffmpeg 或 -transport)。
- **大日志**: 有已知时间窗就先 read 看时间戳格式, 再 grep 按时间正则筛行再分析。

## 判定项
1. **IO**: '8*.' io create/open result 定方案(zlmediakit/ffmpeg)与成败。503/404/403/超时/refused=链接不可达; SSL 证书警告忽略。
2. **流信息**: addVideoDesc→视频编码-分辨率@帧率; addAudioDesc→音频编码-采样率-声道; '2*.'实际视频参数(更准); '3*.'实际音频(异常可能无声); duration 0=直播, >0=点播。
3. **解码**: decode create result success/fail(fail=编码不支持或硬解问题试调 -hard); [FF] av_hwdevice_ctx_create failed→硬解失败关 -hard。
4. **耗时**: '0*. state from none to opening'→'Pipegraph reset success' 按阶段算毫秒与占比指瓶颈。RTSP>2s网络, Vulkan>200ms GPU, 解码初始化>500ms硬解。用户反馈打开慢必给占比。
5. **音视频同步**: 'av not align ioDiff:X renderDiff:Y' 时——pts差距大=时间基不同; audio pts≈video×9~11=PTS倍速偏移; 'audio pts span not match'+playing↔buffering=PTS跨度异常; 以上均源端问题(共SSRC亦源端)。'cmdSyncPts close sync'为临时降级。
6. **Playing↔Buffering**: 偶发1-2次(间隔>5s,<2s)=正常; 频繁(10s>3次)或>5s=异常。查'7*.'队列: frame:0 queue:0=IO无新数据(网络/源端慢); frame:0 queue>0=解码慢(关-hard); 伴丢包=网络丢包(默认tcp,udp换tcp); 伴pts异常=假buffering(源端); 伴auto speed=数据不稳定。buffering timeout 可能关播放器。
7. **[ZL]/[FF]关键日志**: 关注 rtp丢包/packet dropped(花屏)、ssrc mismatch(源端)、超大rtp/缓存溢出、[FF] open input/stream failed、[FF] av_hwdevice_ctx_create failed。忽略: [FF] no frame、[ZL] SSL 警告、rtp stamp abnormal、Invalid sender report rtcp。
8. **重抓建议**: 基础日志不定因才建议 avox_cli 重抓(需用户给有效链接)。包级PTS→-log-packet; 解码问题→-log-decode; 渲染问题→-log-render; UDP丢包→-transport tcp; zlmediakit打不开→-io ffmpeg; 硬解→关-hard; 超时→加大-timeout。io open fail 时先确认链接有效期, 再建议重抓。hysp_pc 场景(有 SN 可查)不必向用户要链接, 转 hysp-pc-log skill 自动取链重采。