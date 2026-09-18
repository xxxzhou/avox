# A-3 VP9/WEBM 硬解兼容排查

> 状态: 进行中 · 上次核对: 2026-09-19 · 权威源: -


优先级 P0 · 里程碑 M1 · 计划状态:施工中(T2/T3/T4 三平台硬解已落地; 软解为自动回退项) · 来源:backlog A-3
W38 是 backlog 引用的「已知问题」编号,本仓 src/doc/tests 全部无记录;2026-09-18 标准源复现,现象未出现。

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
      **进展(2026-09-18)**:`test_vp9_opus_640x360.webm` Windows 软解播放+seek 正常
      (首帧 49ms/seek 13ms),W38「首 GOP 丢关键帧」未复现;三平台硬解注册表确无 VP9
      (FFDx11Decoder 仅 h264/h265、AndVDecoder 仅 avc/hevc、Apple 侧无)。
      剩:W38 原始样片(向提出方索要)再定性 + 硬解/固化软解路线拍板。
- [x] T2 Windows D3D11VA VP9(2026-09-19, `1c195f5`): FFDx11Decoder 注册 VP9
      (AVOX_FFDX11_VP9_DECODER), onVaild 以 MS-PEPF VP9 profile GUID 枚举 GPU 解码
      profile(不支持的老核显上浮失败); **VDecoderTask 选型回退**新增: 首选解码器
      init/setContext 失败→按软解名回退一次(全编码生效, a12 回退框架的先行手动兜底)。
      本机 RX 9070 XT 实测: select ff_vp9_dx11, renderType:d3d11, seektest PASS。
- [x] T3 Android MediaCodec VP9(2026-09-19, `19e4729`): mime video/x-vnd.on2.vp9;
      onVaild 经 dlsym AMediaCodec_getName(API 28+, 编译目标 26)按名排除平台软实现
      (c2.android.*/omx.google.*), 命中软实现回退 ffmpeg 软解; 无 csd 分支。
      NDK 单 TU 编译过, **待真机**。
- [x] T4 Apple VideoToolbox VP9(2026-09-19, `19e4729`): onVaild 探测系统版本
      (iOS 14+/macOS 11+)+VTIsHardwareDecodeSupported(kCMVideoCodecType_VP9);
      bMustVcc 对 vp9 关闭(annexb 重排会破坏 in-band 帧); CMVideoFormatDescriptionCreate
      直建(srcDesc 宽高)。**待 mac 编译+真机**(本机无工具链)。
- [x] T5 webm 首 GOP 对策(2026-09-19 关闭): W38「首 GOP 丢关键帧」09-18 复现定性未复现
      (FFVDecoder 首包建上下文后继续喂当前包已是修复后行为, 注释已更新); 三平台硬解
      落地后由 playmatrix file-vp9/file-vp9-seek 用例钉成回归基线(avox-test 侧)。
      若 W38 原始样片到位后复现, 再开新卡。

## 验收

- avox-test playmatrix VP9 用例:支持平台日志显示硬解路径,不支持平台软解无黑屏;
  webm 首 GOP 不再报错丢帧(或可解释降级)。

## 风险与开放问题

- **不走 Vulkan 硬解路线**:FFVkDecoder 复活需重设计 hwcontext 与渲染设备共享,成本高且注释已判
  「不可行」——各平台走 D3D11VA/MediaCodec/VideoToolbox 原生路线。
- 老设备 VP9 硬解覆盖碎片化,探测失败回退是主路径,硬解命中是加分项。
- W38 上下文缺失,若复现不出,以 T1 归档结论回写 backlog 备注后再定验收口径。
