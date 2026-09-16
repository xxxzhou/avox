# A-8 音频:TrueHD/DTS 软解降级 + AC3/EAC3 全软解兜底

> 状态: 进行中 · 上次核对: 2026-09-16 · 权威源: -


优先级 P1 · 里程碑 M3 · 计划状态:就绪
海外「永不转码直连」卖点的引擎侧一半(另一半是直连推流能力,产品口径)。

## 出口判据

1. TrueHD/DTS(DTS-HD MA)/EAC3 音轨可出声(软解),不再整条音频被禁。
2. 多声道源在 2.0 设备正确下混;5.1 设备行为有明确口径(直出或下混,二选一先定)。
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

- [ ] T1 枚举与映射补齐:ACodecId 加 dts/eac3/truehd;ffACodec/getFFCodecId 双向映射;
      核对并补 truehd/mlp 的 FFmpeg 构建开关(五平台)。
- [ ] T2 下混:swresample 内建 downmix(声道布局协商 + 混音系数)接进解码输出段;
      协商点:解码声道数 vs 设备声道数(`ARenderTask`/`AudioTrack` 衔接处),
      设备 2.0 → 下混,设备 ≥ 源声道 → 直出。
- [ ] T3 TrueHD 注意点:TrueHD 常挂在 MKV(蓝光抽取),存在 MLP 核心 + TrueHD 增强双层结构,
      软解取 TrueHD 层即可;样片需真实蓝光抽取件。
- [ ] T4 用例:ac3/eac3/dts/dts-hd/truehd 样片矩阵(avox-test 资产),三平台出声 +
      5.1→2.0 下混波形抽验(响度不炸、声道数正确)。

## 验收

- 全部样片三平台出声;下混后 RMS 与参考(ffmpeg 命令行下混)对比在合理误差;
  未知编码兜底行为从「关音频」变为「尽力软解 + 明确错误码」。

## 风险与开放问题

- WASAPI 共享模式对多声道 mix format 的真实行为未实测(2.0 设备报 5.1 mix 时的表现),
  T2 先在 Windows 实测定口径,再平移其他平台。
- truehd 构建开关若五平台都要重编 FFmpeg,排期要给库构建留时间。
- 本项与 a12 共用「回退链」思想,但音频已有、视频没有——两计划的公共抽象不必强求统一,
  先各自落地。
