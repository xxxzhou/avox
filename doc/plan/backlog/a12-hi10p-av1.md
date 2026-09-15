# A-12 Hi10P/AV1 自动软解兜底

优先级 P1 · 里程碑 M4 · 计划状态:就绪
探测 → 动态切软解;「已有动态切换框架」的说法要修正:音频有回退链,视频没有。

## 出口判据

1. Hi10P(10bit H.264)在不支持硬解的设备上自动软解播放,不黑屏不报错退出。
2. AV1 8/10bit 可播(软解兜底;有硬解的设备命中硬解)。
3. 解码失败自动回退对用户无感(切换日志可见,无需用户手动 setHardDecode)。

## 现状(代码落点)

- **视频解码是「启动一次性选型」**:`VDecoderTask.cpp:44-56` 按 setHardDecode 选注册名,
  失败(openFailed/timeout :206-218)→ onDecodeError → `MediaPlayer.cpp:153-167`
  仅 close() 播放器——**无运行中硬解→软解回退**。
- **手动切换通路已有**(可复用):`MediaPlayer::setHardDecode`(:633-650,ResetDecode 命令)
  → `VDecoderTask.cpp:138-145` 切换时保存配置帧重建;**雷区**:硬解 GpuFrame 已入渲染队列的
  失效问题(代码注释 :148-151)。
- **回退链范本在音频**:`ADecoderTask.cpp:54-59,121-138`(fdk-aac 失败换 ffmpeg_aac 重放配置)。
- **Hi10P**:像素格式链路全(YUV420P10/P010 映射 `FFHelper.cpp:281-298`、
  渲染支持 10bit `src/avox/video/Video.cpp:74,99-100`),但无「10bit → 硬解能力预判 →
  选软解」的选型逻辑;DX11 get_format 只做诊断打印(`FFDx11Decoder.cpp:82-103`)。
- **AV1 是枚举级缺失**:`AvoxCodec.h` VCodecId 无 av1、`FFHelper.cpp:109-145` ffVCodec
  无分支 → AV1 流直接 VCodecId::none **轨都不注册**;`AVTrack.cpp:266-297` 名字映射无。
  FFmpeg 包有 av1 硬解壳(d3d11va2,`build_ffmpeg.py:102-104`)但**无 dav1d 软解**(minsize 裁剪)。
- 失败原因无分类:openFailed/timeout 不区分「硬解不支持」与「流损坏」。

## 任务拆解

- [ ] T1 视频回退框架:对齐音频 fallbacks 模式——解码器候选链(硬解名→软解名),
      open 失败/连续解码失败/超时 → 自动降档重建;复用 ResetDecode 通路;
      处理渲染队列中硬解 GpuFrame 的失效(等待排空或失效标记)。
- [ ] T2 失败分类:open 阶段失败(hw 不支持/流参数异常)vs 解码期失败(损坏),
      前者触发回退,后者上报错误——防止坏文件无限回退重试。
- [ ] T3 10bit 预判选型:流 desc.type 为 yuv420P10/p010 时按平台硬解 10bit 能力直接选软解
      (Android 设备碎片化重点;Windows D3D11VA P010 通常可,VT 10bit 可)。
      **依赖 a02 的保底字段口径**:probe 降档后 pix_fmt 必须仍可得,否则预判失效。
- [ ] T4 AV1 接入:VCodecId 枚举 + ffVCodec/getFFCodecId 映射 + AVTrack 解码器名映射三处;
      软解兜底要 dav1d——**库层决策**:minsize 包加编 dav1d 或单引 dav1d 库,
      与 a06 的裁剪口径一起定;硬解探测(d3d11va2 壳已编,MediaCodec/VT 平台差异)。
- [ ] T5 用例:Hi10P mkv(10bit H.264)与 AV1 8/10bit 样片矩阵,低端 Android 设备 +
      三平台桌面,验证自动回退与硬解命中日志。

## 验收

- 不支持 10bit 硬解的设备:Hi10P 自动软解,日志显示一次回退,播放无感;
  AV1 全矩阵(硬解命中/软解兜底)可播;坏文件不进入无限回退。

## 风险与开放问题

- dav1d 引入是库构建变更(五平台重编或加包),排期风险主要在这里,先跟 a06 合并决策。
- 回退重建的 GpuFrame 失效是已知雷区(:148-151),T1 第一件事就是设计帧排空口径。
- 与 A-2 probe 降档的交叉依赖(保底字段),两计划合并验收一次。
