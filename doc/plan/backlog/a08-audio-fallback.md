# A-8 音频:TrueHD/DTS 软解降级 + AC3/EAC3 全软解兜底

> 状态: 进行中 · 上次核对: 2026-09-18 · 权威源: -


优先级 P1 · 里程碑 M3 · 计划状态:T1/T2 已落地, T3 走查定案(已有实现零新增), 余 T4/T5(库重编+用例)
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
- **downmix**:渲染器内部各自转换(2026-09-18 走查修正: 原「FFResample 只改采样率
  不改声道」说法有误——swr 出入布局按 desc 建, 能力完整; 差异只在各渲染器给它的
  目标 desc),详见 T3 结论;WASAPI 端读设备 mix format
  (`src/avox_windows/audio/WasAudioRender.cpp:121`)。

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
- [x] T3 下混/声道协商(2026-09-18 代码走查结论: **各平台渲染器已有实现, 无需新增协商点**):
      原计划的「ARenderTask/AudioTrack 衔接处协商」不需要——那会造成双重重采样。
      实际口径 = 渲染器内部把输入 desc 全量转到设备真实输出格式(FFResample 本就支持
      声道布局转换, 原状态行「只改采样率」说法有误, 已就地修正):
      - Windows(WasAudioRender::onInit): 取设备 mix format 当 renderDesc,
        `resample->init(desc, renderDesc)` 逐帧转换 → 5.1 源自动下混到 2.0 设备,
        5.1 设备报 6ch mix 即直出/上混, 正是「设备声道<源下混, ≥源直出」口径;
      - iOS/macOS(IOSAudioRender): 固定立体声输出(`renderDesc.channels = 2`) + 同款转换;
      - Android(AndAudioRender): 源声道直接建 AudioTrack, 依赖 AudioFlinger 平台
        downmix(内置扬声器默认会混)——**M4 真机验证项**: 若机型表现异常, 平移
        Windows 的渲染器内转换模式(固定 2.0 + FFResample)。
- [ ] T4 TrueHD 注意点:TrueHD 常挂在 MKV(蓝光抽取),存在 MLP 核心 + TrueHD 增强双层结构,
      软解取 TrueHD 层即可(枚举已合一, FFADecoder 按 avFrame 实际格式出帧, 双层无感知);
      样片需真实蓝光抽取件, truehd 解码器待部署库重编后才能实测。
- [ ] T5 用例:ac3/eac3/dts/dts-hd/truehd 样片矩阵(avox-test 资产),三平台出声 +
      5.1→2.0 下混波形抽验(响度不炸、声道数正确)。
      素材已有: `test_h264_dts_640x360.mkv` / `test_h264_truehd_640x360.mkv`
      (avox-test assets/video, eac3/ac3 待补)。判定钩子: ADecoderTask 首帧成功推
      `AudioInfo`(带声道/采样率)、失败推 MediaAction fail——playmatrix 加 audio
      断言(收到 AudioInfo 且无音频 error 事件)即可自动化"链路通", 真听感留人工走查。

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
