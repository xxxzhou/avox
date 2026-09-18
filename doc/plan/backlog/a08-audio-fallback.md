# A-8 音频:TrueHD/DTS 软解降级 + AC3/EAC3 全软解兜底

> 状态: 进行中 · 上次核对: 2026-09-18 · 权威源: -


优先级 P1 · 里程碑 M3 · 计划状态:T1/T2 代码落地(2026-09-18, 按海外卖点优先级提前), T3/T4 维持 M3
海外「永不转码直连」卖点的引擎侧一半(另一半是直连推流能力,产品口径)。

## 出口判据

1. TrueHD/DTS(DTS-HD MA)/EAC3 音轨可出声(软解),不再整条音频被禁。
2. 多声道源在 2.0 设备正确下混;5.1 设备行为有明确口径(已定:跟设备 mix format 走,设备声道≥源直出,否则下混)。
3. avox-test 三平台出声用例全绿。

## 现状(代码落点)

- **音频全软解,无透传**:首选 fdk-aac + 回退链(`src/avox/player/ADecoderTask.cpp:42-58,121-138`,
  失败换 ffmpeg_aac 重放配置——**这个回退链就是 a12 视频要抄的范本**)。
- **关键缺口是枚举级**:`ACodecId` 无 dts/eac3/truehd(`src/avox/AvoxCodec.h:12-27`),
  FFmpeg 映射表只有 AC3(`FFHelper.cpp:148-185`,:164-165)→ 未知编码在
  `IOParseFF.cpp:358-364` 直接 `bDisableAudio=true` **整条音频关闭**。
  FFmpeg 库本身 dca/eac3 已编入(`build_ffmpeg.py:89-94`),**truehd(mlp 解码器)需核对构建开关**。
- **downmix 全仓为零**:解码输出原样透传,渲染描述直接用解码 desc
  (`AudioTrack.cpp:42-58` → `ARenderTask.cpp:20-32`),WASAPI 端读设备 mix format
  (`src/avox_windows/audio/WasAudioRender.cpp:121`);FFResample 只改采样率不改声道
  (`ARenderTask.cpp:126-155`)。

## 任务拆解

- [x] T1 枚举与映射补齐(2026-09-18):ACodecId 加 dts=17/eac3=18/truehd=19(只增不改);
      ffACodec/getFFCodecId 双向映射补齐,MLP→truehd 枚举合一;四平台构建脚本
      (py+android/apple/linux sh)白名单补 `mlp,truehd`;csharp 绑定 ACodecId.cs 已同步。
      **交接:本机无 MSYS2 且 FFmpeg 源码树在 n7.0.3(部署库是 9.0.1),truehd 的库重编
      需在编译机按各平台脚本重跑 minsize 并 --deploy;dca/eac3 已在部署库内,无需重编。**
- [x] T2 未知音频不再株连(2026-09-18):`IOParseFF` 未知编码从 `bDisableAudio=true`
      (一条未知轨关掉全部音频)改为**单流跳过**——`skipAudioStreams` 记流号 +
      `AVDISCARD_ALL` 省字节,包循环与 extradata 路径按流号守卫(aIndexMaps 无映射会
      错轨/越界,不能裸跳);实例重开时清集合。其余音轨照常出声,未知轨留 warn 日志;
      明确错误码沿用 ADecoderTask 既有 "not register decoder" fail 事件面。
- [ ] T3 下混:swresample 内建 downmix(声道布局协商 + 混音系数)接进解码输出段;
      协商点:解码声道数 vs 设备声道数(`ARenderTask`/`AudioTrack` 衔接处),
      设备 2.0 → 下混,设备 ≥ 源声道 → 直出。Windows 先实测 WASAPI shared 模式
      对多声道 mix format 的行为定口径,再平移其他平台。
- [ ] T4 TrueHD 注意点:TrueHD 常挂在 MKV(蓝光抽取),存在 MLP 核心 + TrueHD 增强双层结构,
      软解取 TrueHD 层即可;样片需真实蓝光抽取件。
- [ ] T5 用例:ac3/eac3/dts/dts-hd/truehd 样片矩阵(avox-test 资产),三平台出声 +
      5.1→2.0 下混波形抽验(响度不炸、声道数正确)。

## 验收

- 全部样片三平台出声;下混后 RMS 与参考(ffmpeg 命令行下混)对比在合理误差;
  未知编码兜底行为从「关音频」变为「尽力软解 + 明确错误码」(**已落地一半**:可软解的
  轨不再被株连关闭;明确错误码沿用解码任务既有 fail 事件,应用面事件另立项)。

## 风险与开放问题

- ~~WASAPI 共享模式对多声道 mix format 的真实行为未实测~~ → 归入 T3 首步。
- ~~truehd 构建开关若五平台都要重编 FFmpeg,排期要给库构建留时间~~ → 脚本已改,
  重编交接见 T1;Windows 部署库为 minsize(LGPL)已核对 configure 串。
- 商业口径(2026-09-18 定):交付走 LGPL flavor(minsize 白名单/LGPL 全量均为 LGPL 集,
  mlp/truehd/dca/eac3 均 native LGPL 组件);商店文案只做"TrueHD/DTS-HD MA 解码输出"
  描述性表述,不打 Dolby/DTS 商标、不写全景声(软解出 7.1 声床,无 Atmos 对象渲染)。
- 本项与 a12 共用「回退链」思想,但音频已有、视频没有——两计划的公共抽象不必强求统一,
  先各自落地。
- 附带发现(2026-09-18):swig/csharp/files 为 gitignore 的本地生成物(不入库),
  本地副本与当前头文件存在漂移(缺 ISTrackDesc 等),已手补 ACodecId.cs 三枚举值;
  绑定全量重生成另立小项,不在本项展开。
