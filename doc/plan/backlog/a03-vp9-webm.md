# A-3 VP9/WEBM 硬解兼容排查

> 状态: 进行中 · 上次核对: 2026-09-16 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:需复现(W38 仓内零记录) · 来源:backlog A-3
W38 是 backlog 引用的「已知问题」编号,本仓 src/doc/tests 全部无记录,第一步必须复现定性。

## 出口判据

1. W38 现象复现并定性(是硬解缺失、软解首 GOP 问题、还是容器层问题,三选一或组合)。
2. 支持 VP9 硬解的平台命中硬解;不支持的设备自动回退软解,无黑屏/无花屏。
3. webm 首 GOP 丢关键帧问题(FFVDecoder.cpp:130 注释)有对策。

## 现状(代码落点)

- **VP9 三平台都没有硬解注册**:codecId=vp9 在枚举里(`src/avox/AvoxCodec.h:46`),
  但 Windows D3D11VA 只注册 H264/H265(`src/avox_ffmpeg/decoder/FFDx11Decoder.cpp:20-34`)、
  Vulkan 解码注册整体注释停用(`FFVkDecoder.cpp:14-39`,构造注释称外部注入 vkInstance
  「不可行」:44-52)、Android mime 映射只有 avc/hevc(`AndVDecoder.cpp:45-52`)、
  Apple 只有 h264/h265(`IOSVDecoder.mm:15,24`)。→ **现状 VP9 全部走 FFVDecoder 软解**。
- **软解已知坑**:`FFVDecoder.cpp:130-132` 注释记录 vp9/webm 无带外参数集会丢起始关键帧,
  首 GOP 报 "Not all references are available";noConfig 门控(:31-34)只对 H264/H265 生效。
- **排查痕迹**:FFDx11Decoder get_format 候选诊断日志(`FFDx11Decoder.cpp:82-103`)已埋。
- 选型链:`AVTrack.cpp:266-297` 名字映射只有 h264/h265/vp9 三种(vp9 已映射,但落软解名)。

## 任务拆解

- [ ] T1 复现定性:avox-test 标准源 + 真实 webm/VP9 样片(9/10bit、Profile 0/2),三平台跑
      playmatrix,记录现象与解码路径日志;向 backlog 维护者确认 W38 原始现象后归档到本仓 doc。
- [ ] T2 Windows D3D11VA VP9:FFDx11Decoder 注册表加 VP9(profile 0;Intel 老核显覆盖面先查),
      get_format 诊断确认命中;不支持的卡自动落软解(依赖 a12 回退框架,先行可手动兜底)。
- [ ] T3 Android MediaCodec VP9:mime `video/x-vnd.on2.vp9` 映射 + MediaCodecList 能力查询
      (区分软硬实现,`c2.android.vp9.decoder` 是软解不算命中)。
- [ ] T4 Apple VideoToolbox VP9:macOS 11+/iOS 14+ 部分支持,`VTIsHardwareDecodeSupported`
      探测后再注册。
- [ ] T5 webm 首 GOP 对策:首包参数集缺失容错(丢帧重试/容忍第一组报错),
      与 noConfig 门控对齐补 vp9/webm 分支。

## 验收

- avox-test playmatrix VP9 用例:支持平台日志显示硬解路径,不支持平台软解无黑屏;
  webm 首 GOP 不再报错丢帧(或可解释降级)。

## 风险与开放问题

- **不走 Vulkan 硬解路线**:FFVkDecoder 复活需重设计 hwcontext 与渲染设备共享,成本高且注释已判
  「不可行」——各平台走 D3D11VA/MediaCodec/VideoToolbox 原生路线。
- 老设备 VP9 硬解覆盖碎片化,探测失败回退是主路径,硬解命中是加分项。
- W38 上下文缺失,若复现不出,以 T1 归档结论回写 backlog 备注后再定验收口径。
