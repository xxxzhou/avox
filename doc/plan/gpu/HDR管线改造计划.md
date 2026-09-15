# HDR 管线改造计划

> 2026-09 全链路现状调查结论 + 分阶段改造计划。调查基线 commit `70cc2cc`,行号仅作定位参考。
> 本文只做规划,未启动实施;与 [颜色空间矩阵统一设计](颜色空间矩阵统一设计.md) 同域,其「单一真相源」原则在 HDR 里继续沿用。
> 2026-09-14 评审定稿:UBO 3a 布局、V5 先于 P010 落地、读函数/色彩尾段分离等实施决策见 §6;§4 待拍板同步更新。

## 1. 背景与目标

当前播放链路从解码到上屏全链 8bit SDR,BT.2020/10bit/HDR 元数据在各层被丢弃或错误处理,10bit HEVC 硬解源目前会直接画面错乱(见 2.2)。

目标:HDR10 源(BT.2020 + PQ + 静态元数据)端到端可用,支持两种输出:

| 模式 | 链路 | 价值 |
|---|---|---|
| HDR→SDR (tone map) | PQ→线性→tone map→色域压缩→sRGB,输出 rgba8 | SDR 屏正确观看,下游特效/互操作链路零改动,**优先** |
| HDR→HDR (直通) | BT.2020+PQ 编码上屏,swapchain HDR10 色彩空间,静态元数据交显示器 | HDR 屏完整观感 |

**非目标(首版)**:HLG、HDR10+ 动态元数据、Dolby Vision、HDR 录制、100+ 特效 shader 线性化。HLG/HDR10+ 依赖元数据结构预留枚举,实现排后续。

## 2. 现状盘点

### 2.1 元数据层

- `YuvType` 有 `yuv420P10`/`uyvy422_10B`,无 P010/P016/yuv444p10(`src/avox/AvoxVideo.h`,AVOX_MAP_YUV 宏表)。
- `ColorSpaceDesc { YuvStandard{bt601,bt709,bt2020}, YuvRange{full,limited} }`:只有矩阵+量程,无 transfer(PQ/HLG)、无 HDR 元数据(`AvoxVideo.h`)。挂在轨道级 `VideoDesc.colorSpace`,帧结构 `YUVFrame`/`VideoFrame`/`GpuFrame` 无色彩字段。
- `FFHelper::ffColorSpace` 只读 `color_space`+`color_range`;`color_primaries`/`color_trc`/chroma_location 不读(`src/avox_ffmpeg/FFHelper.cpp`)。
- `FFVDecoder::onFrame` 只取宽高/格式/data,AVFrame 全部 side data(mastering display、content light level、HDR10+)静默丢弃(`src/avox_ffmpeg/decoder/FFVDecoder.cpp`)。
- 自研 H264/H265 SEI 解析器已能解 mastering displaycolour volume(SEI 137/147)、CLL(144)、ATC(`src/avox/codec/H264Common.hpp`、`H265Common.hpp`、`H264Parse.cpp`),**无任何下游消费方**,是现成可接的半成品。
- BT.2020 矩阵按 601 兜底(`src/avox/video/ColorSpace.cpp`);播放路径不按流自动切矩阵,`setColorSpace` 只有录制路径调用且硬编码 `{bt601, full}`(`SourcePlayer.cpp`、`TranscodeRecorder.cpp`)。
- 编码端 primaries/TRC 硬编码 BT709(`FFVEncoder.cpp`),D3D11 硬编 sw_format=NV12(`FFDx11Encoder.cpp`),muxer 不写色彩标签。

### 2.2 解码层

- DX11VA 硬解只注册 H264/H265(VP9/AV1 无硬解入口);`get_format` 直接 `avcodec_default_get_format`,无 P010 挑选逻辑(`FFDx11Decoder.cpp`)。
- **致命断点**:`Dx11Helper::getDxFormat` 只认 NV12/YUY2,P010 surface 落 `YuvType::other` 后被下游**误当 RGBA 导入 Vulkan,画面错乱**(`src/avox_windows/dx11/Dx11Helper.cpp`)。整条 GPU 管线 NV12-only:DX11 CS 渲染的 SRV 固定 R8/R8G8(`Dx11CSVideoRender.cpp`),CPU YUV 回读明确拒绝非 NV12(`Dx11CSVideoRender.cpp` mapStagingFrame)。
- 软解 `yuv420P10` 通路已闭环:r16 纹理上传 → `glsl/source/yuv2rgbaV4.comp` 取低 10 位 → **输出 rgba8 即刻截断**。
- 纯 CPU 转换 `yuvframe2Rgba` 只支持 yuv420P/nv12,系数写死 601(`src/avox/video/ImageBuffer.cpp`)。
- Android:MediaCodec `COLOR_FormatYUVP010(0x36)` 被错映射为 `uyvy422_10B`(P010 是 420 半平面,不是 packed 422,`src/avox_android/AndCommon.cpp`);全链路无色彩标记。
- iOS/macOS:VT 会话属性写死 8bit NV12 video range,不读 `kCVImageBufferYCbCrMatrixKey`;Metal 采样 shader 硬编码 601 limited(`src/avox_apple/IOSVDecoder.mm`、`MetalRender.mm`)。

### 2.3 渲染层

- YUV→RGB 两条路径:DX11 CS(默认 DX11 渲染器,HLSL 内嵌字符串硬编码 601 full,`Dx11CSVideoRender.cpp`);Vulkan compute(主管线,矩阵 UBO 可切换,`VkYUV2RGBALayer.cpp` + `ColorYuvUBO`,但如上 bt2020 无真矩阵、播放路径无人调)。
- 全链 rgba8 gamma 空间流转:约 180 个 compute shader(`glsl/source/`)in/out 全 rgba8;唯一线性段是 FSR 自包含的 sRGB↔linear rgba16f 往返,不可复用为骨架。
- 无 PQ/HLG 传递函数、无 tone mapping、无 nits/SDR 白点概念。
- 输出:`VkOutputLayer` 用 `vkCmdCopyImage` 把 rgba8 拷入 swapchain 或 DX11 共享图;格式映射只有 rgba8/bgra8/rgba16f(`src/avox_vulkan/` VkHelper/VkOutputLayer)。

### 2.4 呈现层

- DX11 窗口:`D3D11CreateDeviceAndSwapChain` + `R8G8B8A8_UNORM` FLIP_DISCARD(`src/avox_windows/dx11/Dx11Window.cpp`);DX12 样例同样 8bit。
- Vulkan 窗口:格式选择优先 B8G8R8A8/R8G8B8A8,色彩空间恒为驱动默认 `SRGB_NONLINEAR`(`src/avox_vulkan/vulkan/VkWindow.cpp`);实例/设备扩展均无 `VK_EXT_swapchain_colorspace`(`VkCommon.cpp`)。
- 全仓无 `SetColorSpace1`/`DXGI_HDR_METADATA_HDR10`/`CreateSwapChainForComposition`/HDR 检测代码。

### 2.5 互操作(UE/Unity)

- avox 导出端写死 RGBA8:`Avox.cpp` enableVkOutput 句柄导出、`Dx11CSVideoRender` 共享输出纹理、`Dx11SharedTex` NT 句柄。
- UE(avox-ue 私仓):`AvoxGpuInterop.cpp` 三后端全部 `R8G8B8A8`/`PF_R8G8B8A8`,反向 DXGI→PF 映射只认两种 8bit 格式,其余直接失败。
- Unity:`GpuPassthrough.cpp` Vulkan/AHB 导入硬编码 R8G8B8A8;C# 包裹层 `TextureFormat.RGBA32`;CPU 回退按紧凑 NV12 上传。色彩空间仅一个 standard|range 打包码,无 transfer。

### 2.6 测试基建

- `assets/video/test/` 10 个标准源全部 8bit;playmatrix 26 条用例无任何 10bit 项,yuvout 哨兵判定只认 nv12/yuv420P(`tests/playmatrix/PlayMatrix.hpp`);`doc/test/功能测试矩阵.md` 中 10bit 明确「未跑」。

### 2.7 有利条件

- 3rdparty ffmpeg 8.0 预编译(LGPL)HDR API 齐全:`mastering_display_metadata.h`、hwcontext 各平台头均在,纯 SDK 未使用。
- SEI 解析器现成(2.1),接上即可拿到无封装流的静态元数据。
- `yuv420P10` 软解→Vulkan 通路闭环,`ImageType` 已有 `rgba16f`,D3D11↔Vk external memory 互操作现成。
- FSR 链有 rgba16f 线性段先例可参考。

## 3. 分阶段改造

每阶段独立可验收,默认行为不变(不碰 HDR 流时零回归)。

### 阶段 0:测试基建(先行)

- 补 1-2 条 10bit HEVC HDR10 素材(PQ + BT.2020 + mastering SEI)进 `assets/video/test/` 与 playmatrix。
- playmatrix yuvout 哨兵判定扩 10bit 期望值(否则 10bit 用例直接 FAIL)。

### 阶段 1:元数据贯通(改结构,量不大)

- `ColorSpaceDesc` 增 `YuvTransfer { gamma, linear, pq, hlg }`;新增 `HdrMeta { maxLum/minLum/maxCLL/maxFALL/mastering primaries+white }`,挂轨道级 `VideoDesc`(静态 HDR10 流级够用),帧级字段留待后续。
- `YuvType` 增 `p010`(软解映射、Android/VT 映射同步补)。
- `FFHelper::ffColorSpace` 读 `color_primaries`/`color_trc`(SMPTE2084→pq、ARIB-B67→hlg,default 一律落 gamma 见 §6.3);`FFVDecoder::onFrame` 读 `AV_FRAME_DATA_MASTERING_DISPLAY_METADATA`/`CONTENT_LIGHT_LEVEL`;自研 SEI 解析结果接为裸流兜底。
- 顺手修两处现存错误:录制路径强制覆写 `{bt601,full}`、Android P010 错映射 `uyvy422_10B`。
- 公共 API:`setHdrMode { auto, forceSDR, forceHDR }` + 显示 HDR 能力查询;流 HDR 元数据经现有 desc/info 回调透出供宿主适配 UI。

### 阶段 2:10bit 解码与正确上屏(SDR 输出)

- `FFDx11Decoder::get_format` 对 10bit 流(HEVC Main10/VP9/AV1 profile)主动选 P010 并手工建 hw_frames_ctx;顺带补 VP9/AV1 硬解注册。
- `getDxFormat` 补 `DXGI_FORMAT_P010 → p010`;P010 供给链二选一:
  - **A(推荐,省事)**:P010 纹理直接 external memory 导入 Vulkan,新增 `yuv2rgbaV5.comp`(P010 内存布局、PQ EOTF、BT.2020 真矩阵、tone map、色域压缩、sRGB 输出 rgba8),复用 `ColorYuvUBO` 机制;下游特效/输出链零改动。定稿细化(3a 布局、读函数分离、V5 先于 P010 落地)见 §6.1/§6.2。
  - B:`Dx11CSVideoRender` 加 P010 分支(纹理 typed P010,平面 SRV cast R16/R16G16,采样后 >>6),内部完成 tone map 再出 rgba8。
- `ColorSpace.cpp` 补 BT.2020 实矩阵;播放路径按流矩阵自动下发(切 UBO 链路已存在,只差调用)。
- 验收:10bit HEVC SDR 屏色彩正确。**至此 SDR 观感问题全部解决,后续 HDR 只是尾部增量。**

### 阶段 3:HDR 呈现(Windows 优先)

- 能力检测:`IDXGIOutput6::GetDesc1` 查 HDR;SDR 屏自动落阶段 2 的 tone map 模式。
- **推荐 DXGI 通道**:Dx11Window 换 `CreateSwapChainForHwnd`(FLIP_DISCARD),HDR 时格式 `R10G10B10A2_UNORM` + `IDXGISwapChain4::SetColorSpace1(RGB_FULL_G2084_MINIP_P2020)` + `SetHDRMetaData`(流静态元数据);管线尾端出 rgba16f/R10G10B10A2,PQ 编码为最后一个 pass。备选 R16G16B16A16_FLOAT + scRGB(`G10_NONE_P709`)。
- Vulkan 通道:启用 `VK_EXT_swapchain_colorspace`,选 `HDR10_ST2084_EXT`,配 `VK_EXT_hdr_metadata`。Windows 驱动支持参差,Vulkan HDR 排在 DXGI 验证之后。
- 特效策略:HDR 直通模式首版跳过 100+ gamma 空间特效(仅直通输出);tone map 模式照常走特效。全特效线性化不进首版。
- HDR 混流:SDR 流在 HDR 屏按桌面 SDR 白点走原 rgba8 路径,不强制换道。

### 阶段 4:互操作与其他平台

- 引擎互操作二选一:**(a) 推荐**:SDK 内部 tone map,继续导出 RGBA8,UE/Unity 零改动;(b) HDR 直通引擎,导出加 `R16G16B16A16_FLOAT`,需同步改 `Avox.cpp` 导出、DX11 共享纹理/SRV、UE `AvoxGpuInterop` 正反向映射、Unity `GpuPassthrough` + C# `TextureFormat`。
- macOS/iOS(EDR):VT 解码放开 `kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange('x420')`,读 `kCVImageBufferYCbCrMatrixKey`,`MetalRender.mm` 去硬编码;`CAMetalLayer` EDR(`rgba16Float` + `wantsExtendedDynamicRangeContent`)。
- Android:P010 经 AHB + dataspace(BT2020/PQ/FULL);Unity AHB 路径同步。

### 阶段 5:回归

- playmatrix 增 10bit/HDR 用例(解码、yuvout、渲染哨兵)。
- HDR 屏人工走查 PQ 高光/暗部与元数据生效;SDR 屏走查 tone map 一致性(与 ffmpeg `zscale`+`tonemap` 参考输出对拍)。
- 录制策略首版明确降级:HDR 源录制时 tone map 成 SDR(编码端 10bit/PQ 标注另立任务)。

## 4. 待拍板决策

1. **制式范围**:首版只做 HDR10;HLG 枚举/解析/V5 分支预留到位,验收素材以 HDR10 为主(部分收敛,2026-09-14,见 §6.3)。
2. **互操作格式**:SDK 内 tone map 保 RGBA8 契约(推荐),还是引擎侧 HDR 直通(牵动 UE/Unity 双仓)。
3. **呈现通道**:Windows HDR 首版走 DXGI(推荐,驱动稳)还是 Vulkan swapchain(跨平台一致但风险高)。
4. ~~P010 供给链~~ **已拍板(2026-09-14):方案 A(Vulkan 侧处理)**;V5 读函数/色彩尾段分离,P010 作为第二个读函数接入,见 §6.2。

## 5. 验证清单

- 元数据:ffprobe 对照流 maxLum/maxCLL 与 SDK 读出值一致;无 HDR 标签流不产生 HdrMeta。
- 色彩:PQ→SDR tone map 与 ffmpeg 参考管线对拍(纯色卡/灰阶);BT.2020 矩阵与已知系数逐位对拍。
- 呈现:HDR 桌面(R10G10B10A2+SetColorSpace1)与 SDR 桌面行为均正确,SDR 流零回归。
- 回归:ctest + playmatrix 离线子集全绿,新增 10bit 用例通过。

## 6. 实施决策记录(2026-09-14 定稿)

### 6.1 UBO 扩展:3a 定稿——槽位语义升级 + 尾部追加,旧 shader 零改动

`transfer` 占用现有 `_pad` 槽位(offset 12),tone map 参数追加在 `colorMat` 之后(offset 80),UBO 80B→96B:

```cpp
struct ColorYuvUBO {
  int32_t width = 0;
  int32_t height = 0;
  int32_t yuvType = 0;
  int32_t transfer = 0;          // 0=gamma 1=linear 2=pq 3=hlg, 枚举声明序直接 cast
  Mat4x4f colorMat = {};         // offset 16 不动
  // --- 追加区(仅 V5 声明) ---
  float maxLuminance = 1000.0f;  // offset 80 内容峰值 nits
  float sdrWhiteNits = 100.0f;   // offset 84 SDR 白点 nits
  int32_t _pad2[2] = {};
};
static_assert(sizeof(ColorYuvUBO) == 96, "UBO layout must match shader std140");
```

- 旧 shader 零改动:offset 12 在旧 GLSL 块里本就是隐式 padding,mat4 仍落 offset 16;旧块声明 80B < buffer 96B,Vulkan 合法且旧 shader 永不读追加区。仅 V5 声明完整块。
- **否决高位编码方案**(transfer 塞 yuvType 高位):`yuv2rgbaV1.comp:137`、`yuv2rgbaV2.comp:24` 存在 `ubo.yuvType == 4` 整值比较,高位污染会失配分支;3a 无此风险。
- `ColorYuvUBO` 消费方仅 `VkYUV2RGBALayer`/`VkRGBA2YUVLayer`,均 `setUBOSize(sizeof(...))`,扩容自动生效。

### 6.2 V5 结构:读函数与色彩尾段分离,V5 先于 P010 落地

- V5 内部分层:按格式读函数(`yuv420p10`:低 10 位 `&1023`、三平面;后续 `p010`:高 10 位 `>>6`、UV 交错双平面)→ 共用色彩尾段(PQ/HLG 解码 → tone map → BT.2020→709 色域压缩 → sRGB 编码),尾段只吃归一化 YUV。
- **落地顺序:V5 先挂现有软解 `yuv420p10` 通路验证全部色彩数学**(禁硬解放 HDR10 测试片即可端到端验收);P010 接入只是加第二个读函数,验收方式为同片软解/硬解对比。避免「P010 先行但无 HDR 数学可验」的空转。
- 单 V5 通吃原 V4:transfer 为运行时 UBO 分支(SDR 直通),不按 transfer 分流 shader 文件——避免直播流晚到的 transfer 因换 shader 触发 graph 重建(`setColorSpace` 仅重传 UBO)。
- **transfer 必须在 `refreshColorMat()` 里更新**:`setColorSpace` 运行时路径只走它,只写在 `onInitLayer` 的话晚到的标志进不了 UBO。

### 6.3 解析与矩阵

- `ffColorSpace` 读 `color_trc`:default 一律落 gamma,仅显式 SMPTE2084/ARIB-B67 标记进 HDR 路径——无标记/老素材流零回归的关键。
- BT.2020 真矩阵系数已验算:Kr=0.2627/Kb=0.0593;正向 Y 行 0.2627/0.6780/0.0593,反向 R=Y+1.4746Cr、G=Y−0.16455Cb−0.57135Cr、B=Y+1.8814Cb。

### 6.4 转换后端多样性与解码层现状(2026-09-14 核实)

YUV→RGB 与 tone map 数学在各渲染后端为独立实现,Vulkan 先做,其余移植同一套数学:

| 后端 | 转换实现 | 说明 |
|---|---|---|
| Vulkan compute(主管线) | `glsl/source/yuv2rgbaV*.comp` | 本次改造主场 |
| Android GLES | `EglVideoYuv.cpp` 内联 fragment shader | 独立实现,需移植 |
| Apple Metal | `MetalRender.mm` | §2.2 已记硬编码 601 |
| Windows DX11 CS | `Dx11CSVideoRender.cpp` HLSL 内嵌 | §2.3 已记,方案 B 备选 |

解码层现状:Android `AndVDecoder.cpp:146` 写死 NV12 8bit 直出、iOS/macOS `IOSVDecoder.mm:171` 写死 8BiPlanar NV12——两平台硬解 HDR 的前置是**解码器升级本身**(MediaCodec `COLOR_FormatYUVP010`、VT `'x420'`),非格式映射。Linux VAAPI/Windows D3D11VA 硬解已有,10bit 出 P010(`FFVADecoder.cpp:108` 下载回内存后 sw_format 仍为 P010,下载 ≠ 格式转换)。

### 6.5 新 shader 注册

`compileglsl.py` 实际读取 `glsl/glslindexcurrent.txt`(**不是** `glslindex.txt`);V5 需在该文件加行并重跑编译,改错文件会静默不生效。

### 6.6 阶段 1 核心落地记录(2026-09-14)

- 已落地 commit `9304cdc`:`YuvTransfer` 贯通(`ffColorSpace` 读 `color_trc`,default 落 gamma)、BT.2020 正反向真矩阵、`ColorYuvUBO` 3a(96B)、`yuv2rgbaV5.comp` 通吃 10bit(V4 退役)、transfer 经 `refreshColorMat` 运行时更新。`tests/test_colorspace.cpp` 锚定矩阵已知系数/往返一致/UBO 布局契约,ctest 全绿,play_regress 离线子集 8/8 通过。
- tone map 算子定为 **ACES 近似(Narkowicz)**,`maxLuminance` 峰值映射到 1;输出编码用 **BT.709 OETF**(替代本文早前「sRGB 输出」措辞——与 SDR 直通路径同一 gamma 域,下游特效零适配)。数值验证:PQ 解码 100/203/1000/10000nit 四参考点命中;灰阶 tone 点 0→0.00、100→0.80、203→0.91、1000→1.00;HLG 0.75 码值→203nit 漫反射白。
- `maxLuminance`/`sdrWhiteNits` 暂用 UBO 默认值(1000/100);MaxCLL 接入随阶段 1 余项(`HdrMeta` 结构、`FFVDecoder` side data、SEI 兜底、`setHdrMode` 公共 API)。
- E2E 缺口:本机 PATH 无带 x265 的 ffmpeg,阶段 0 的 HDR10 测试素材尚未生成;素材到位后再接 playmatrix 10bit 用例。
- 「录制路径覆写 {601,full}」复核(2026-09-14 深夜,未动):该覆写与 FFVEncoder 硬编码 709 标签在往返上互相抵消——709 片源录制后 YUV 原值保留 + 709 标签,结果恰好正确;实际误差仅落在 601 标签源(录后按 709 解出轻微色偏)与 cs≠709 的其他制式。修法须成片:录制 cs 取流 cs(transfer 强制 gamma,V5 已 tone map)+ FFVEncoder 标签改由 cs 驱动,单独改任意一侧都会打破现有巧合一致,归入阶段 1 余项连同编码标注一起做。

### 6.8 深夜推进落地(2026-09-14 凌晨,commits 12bd367/8321943/49b265d)

- **存量 bug 修复**:`applyLimitedInverse` 组合顺序反了(`d.multiply(mFull)` → `mFull.multiply(d)`)。本体系行=输出通道,先应用的变换须乘在右边;旧行为 limited 往返最大偏 0.013。此前全链默认 full 未触发——播放按流下发激活 limited 路径前被单测拦截。
- **播放链路贯通**(V5 真正生效的前提):`VideoTrack::onVideoDesc` 按流下发 colorSpace(此前普通播放无人调 setColorSpace,全链默认 601/full);`VkVideoRender::setColorSpace` 变化检测补 transfer 维度。
- **HdrMeta 贯通**:结构体(AvoxVideo.h)+ `FFVDecoder` 读 mastering display/CLL side data + `IVideoDecoderOb::onHdrMeta`(新增默认空回调)→ VideoTrack 一次下发 → 级联至 yuv2rgba UBO `maxLuminance`(hdrPeakNits: CLL 优先→mastering→1000 默认)。硬解下载帧不带 side data,hw 路径暂走默认兜底。
- **P010 CPU 路径闭环**(§6.7 约束的原子落地):`YuvType::p010` + `ffYuvType` 映射 P010LE/BE + `copyPlaneYUV2TightlyBuffer` 归一化(高 10 位右移对齐 + UV 交错拆分,产出与 yuv420P10 相同紧排布局,**shader 零改动**)+ yuv2rgba 层同配置走 V5 + 打包契约单测。比 §6.7 原设想更优:shader 不需要第二个读函数。
- 余项不变:GPU 导入路径(FFDx11Decoder/getDxFormat)待真机;HDR10 素材待带 x265 的 ffmpeg;`setHdrMode` 公共 API 与 SEI 裸流兜底待拍板;Android AndCommon 0x36 错映射修复随阶段 3(当前解码端只出 NV12,错映射休眠)。

### 6.7 阶段 2 断点备注(P010 硬解,待真机验证后实施)

- 落点分层:`YuvType` 加 `p010`(AVOX_MAP_YUV,group=4/groupsize=12,与 yuv420P10 同构×2B)→ `ffYuvType` 加 `AV_PIX_FMT_P010LE/BE`(hwdownload 后 sw_format 即 P010)→ V5 加 p010 读函数(高 10 位 `>>6`;UV 交错双平面按 2i/2i+1 寻址,UV 平面宽=width)→ FFVDecoder/r16 上传路径核对 P010 行对齐 stride。
- **枚举、shader 分支、上传路径必须同一提交闭环**:只加枚举会让 p010 流落到 layer 不支持的默认分支出花屏,比现在落 `other` 更糟。
- GPU 导入路径(`FFDx11Decoder::get_format` 主动选 P010、`getDxFormat` 映射、手工建 hw_frames_ctx)依赖真机调试,与 CPU 下载路径分开推进。
- **2026-09-14 晚硬解实测(hdrtest -hard)**:negotiated D3D11 帧正常出(104 帧全到),画面全黑 meanY=16(limited 黑)——纹理内容没被正确采样,不是帧断流。断点三点:`Dx11Helper::getImageType(DXGI_FORMAT)` 无 NV12/P010 行(default 落 rgba8/other);`getDxFormat` 无 P010 行(落 `YuvType::other`,下游连帧类型都无法命名);`AVOX_MAP_VK_YCBCR_FORMAT` 仅 8bpp 一行(16bpp 采样的 ycbcr 转换缺位)。`VkInputLayer::inputGpuData` 从 DX 纹理描述反推 imageType,P010 必落 other。阶段 2 开工须 DXGI 映射 + VkFormat 16bpp 行 + ycbcr/shader 读函数同一提交闭环(与上面「枚举/shader/上传三合一」同理)。

### 6.9 首次真实 E2E 验证(2026-09-14 下午,补真素材打通)

阶段 0 的「无 x265 ffmpeg」缺口已破:PATH 上的 ffmpeg 7.0.2 full build 自带 QSV,`hevc_qsv -profile:v main10` 在本机 UHD 770 实测可用(注意:只吃 `p010le` 直喂,带滤镜图的上传路径报 -22;zscale 在 7.0.2 各组合均 no path,libplacebo 需 Vulkan 设备本机不可用)。素材内容保持 SDR 采样值,靠 VUI+SEI 标签声明 PQ/BT.2020——SDR 值按 PQ 解码中间调仍在 SDR 白附近,高光(码值 1.0)落 10000nit,足以触发/验证 tone map。生成器:`script/testenv/gen_hdr10_asset.py`(QSV 编码 → Python 手注 SEI 137/144 NAL(带 emulation prevention,插首个 IRAP 前)→ remux;ffmpeg<7.1 的 hevc_metadata BSF 无 master_display 选项)。产出 `assets/video/test/test_h265_hdr10_pq_640x360.mp4`(Main 10/PQ/BT.2020/bt2020nc + mastering SEI + CLL)与 SDR 参考伴生文件。

**E2E 首跑即揪出两个存量断点:**

1. **`getYuvFrameSize` 10bit 高估 2 倍(已修)**:AVOX_MAP_YUV 里 yuv420P10/p010 的 groupsize 写 12(把 2B 像素重复计入),按字节 rowPitch 算出 1382400,而 r16 紧排实际 691200 → `SwVideoBuffer::to()` 的 bufferSize 契约恒 false → **静默 return → 渲染侧零消费、解码队列积压、全链黑屏**。这正是「软解 10bit 闭环从未跑过真实文件」藏住的雷;修复为 groupsize=6(系数=2B×1.5 平面/字节),§6.7 里「groupsize=12」的记录同步作废。`VideoRender::renderFrame` 的 to() 失败补了 warn 日志(静默黑屏必须留痕)。
2. **HDR 元数据未达解码器(初判有误,6.10 已修正)**:解码帧 `nb_side_data`/`decoded_side_data`/`nb_coded_side_data` 三处实测全 0,当时误判为 ffmpeg9 导出行为变化;真因是 **AVSource 把独立 SEI NAL 整个丢弃**(见 6.10)——解码器根本没见过 SEI。SEI 随流到达后 ffmpeg 9.0.1 帧级 side data 导出正常(sd:2)。

**探针与硬解雷区实测**:`samples/functest/hdrtest.cpp`(软解 HDR vs SDR 参考对比 Y 均值,PQ 路径中灰应显著抬升;`-hard` 档观察雷区)。修复 1 之后软解链路全通(解码→r16 上传→V5→rgba2yuv→CPU 帧,PNG 出图正常)。**「tone map 输出仍≈SDR」为均值判据误判,6.10 已证伪**——总均值被暗部压暗与亮部饱和抵消(+4),像素级映射呈标准 tone map 特征(中间调 +35、灰 128→226/理论 227、99.3% 像素差异)。另:Vulkan 离屏(空 surface)模式下 `screenShot` 的 checkShot 无人调用恒超时,onFrame(enableYuvOut) 通道不受影响;`-hard` 档 DX11VA 10bit 实测**黑屏**(meanY=16=limited 黑),比 §2.2 预判的花屏更早死,坐实 P010 硬解须按 §6.7 整层闭环。play_regress 离线子集 8/8 通过,ctest 全绿。可观察性补强:`VkVideoRender::setColorSpace`(transfer 变化)/`setHdrMeta`(元数据值)/FFVDecoder 前 3 帧 side data 计数/`VkYUV2RGBALayer::refreshColorMat`(UBO 内容)四处一次性日志。

**API 面现状(探针源码注释固化)**:`setHdrMode` 全仓无实现;`ISurfaceRender` 仅 `setColorSpace` 无 `setHdrMeta`;`IMediaPlayerOb`/`ISurfaceRenderOb` 无 onHdrMeta 回调——宿主既拿不到元数据也无法强制 SDR,公开 API 仍欠阶段 3 落地。

### 6.10 tone map 验收通过 + 元数据链打通(2026-09-14 傍晚)

**tone map 端到端验收 PASS**。6.9 末「输出≈SDR」系均值判据误判:总均值被「暗部压暗+亮部饱和」抵消(仅+4),而像素级映射正是 tone map 签名——SDR→HDR 输出逐像素分桶:中间调 +18.6/+35.6、阴影 -6.5、99.3% 像素差异(maxdiff 140);中性灰 128→226(理论 227,误差 1 LSB);黄 (255,240,0)→(255,255,0)。「灰区不变」的假象来自 testsrc2 顶部黄蓝交替纹的均值巧合。判据已重写:hdrtest 末帧 RGB 逐像素对比(rgb max-diff>15 占比>50% + sdr 亮度 110..180 区间中位抬升>10),**实测 PASS(midLift 13.5/diffRatio 0.69)**。探针双通道化:enableImage 抓 V5 后 RGBA(rgba2yuv 之前),与 onFrame YUV 通道对照可把失效面切到具体阶段——本次即靠它排除 rgba2yuv。

**元数据链打通,真因是 AVSource 丢 SEI 而非 ffmpeg9 导出行为**。链路取证:mp4 样本内 SEI 存在 → ffmpeg 解封装保留 → **AVSource 合并循环把「组开头的非新帧 NAL」静默丢弃**(combineBufs 为空时 else 分支为空,注释 NAL_SEI_PREFIX 即此缺口),[VPS/SPS/PPS][SEI] 布局还会因「配置帧后不并包」continue 丢弃。修复两处:①组空时 SEI 作为合并组起点单独下发;②前包为配置帧时 SEI 也单独成组(不并进配置包)。`naluDropAble` 同步移除 SEI(仅留 AUD)——SEI-only 包对 ffmpeg 只是无帧 EAGAIN,实测零 "no frame" 刷屏(老版本的问题已不存在)。SEI 到达解码器后:ffmpeg 9.0.1 帧级 side data 导出正常(sd:2),同时 FFVDecoder 新增 `scanHdrSei` 裸流兜底(包内 prefix SEI 提取 137/144 固定宽载荷,反仿真后解析,与帧级/ctx 三路互为冗余,`updateHdrMeta` 统一变化下发)——实测 SEI 值与注入值逐字段一致,`VideoTrack::onHdrMeta`→`setHdrMeta` 链路日志全通。**遗留边界:[AUD][SEI][IDR] 布局 AUD 作组起点仍会并包吞 SEI,待真实素材触发再修。**

验收:hdrtest PASS、ctest 全绿、play_regress 离线子集 8/8。**HDR10 播放→自动 tone map 到 SDR 的核心链路就此可用**(软解路径);硬解 P010 黑屏(6.9)与 API 面(setHdrMode/宿主 onHdrMeta)仍是后续阶段主要缺口。

### 6.11 [AUD][SEI] 边界修复 + HDR 用例接入回归矩阵(2026-09-14 晚)

**`[AUD][SEI][IDR]` 吞 SEI 已修,但第一版修法本身是错的(记录防复发)**。`gen_hdr10_asset.py --aud` 产出 `_aud` 变体(QSV 自带 pic-timing SEI,真布局是 `[SEI_qsv][AUD][SEI_hdr][IDR]`)后元数据三路全空。第一版在合并循环里把 AUD 从组起点跳过——**破坏了合并循环「lastBuf.size 顺序扩展」的连续性前提**:SEI_hdr 并组时 `size += 40` 覆盖的是 SEI_qsv 起点后 40 字节,中间隔着 AUD 的 7 字节,拼出的包内容错位(SEI#2 被截断),自研解析和 ffmpeg 导出双双失效,而 tone map 判据因只依赖 transfer(PQ 声明)照样 PASS——差点击穿「过了判据=对了」的盲区,包级日志(log.source.packet)定位。**正解:丢弃点在 singleVideo**——纯 AUD 组照旧丢(无 slice 空包刷 no frame);`[AUD][SEI..]` 混合组在 singleVideo 重拆 NAL,跳过头部 AUD 从下一 NAL 重新起头下发,合并循环保持连续性不动。实测 SEI#2 独立成 40B 包,scanHdrSei 提取 + 帧级 sd:2 + setHdrMeta 三路全通,hdrtest 对 base/_aud 双素材 PASS(midLift 11.9/16.1)。

**HDR/10bit 用例接入 playmatrix(26→29 条)**:Endpoints 加 `fileHdr10/fileHdr10Aud`(素材缺失自动 off,平台不同步素材不拖垮整表);新用例 `file-hdr10-soft`(tone map 链路拉流)、`file-hdr10-aud`(合并边界回归)、`yuvout-h265-10bit`(软解交付 yuv420P10 的帧契约哨兵,getYuvFrameSize groupsize 修复的看门人)。play_regress FILE_ASSETS 改 (开关,路径) 表驱动,Android 全量 push,离线子集含三条新用例,LINUX_OFFLINE_SKIP 补 yuvout-h265-10bit(车道 B 原生渲染缺口)。首跑 yuvout-h265-10bit 即暴露:交付契约成立(type=yuv420P10,211 帧),挂的是 FrameOb 出 PNG 用的 sw yuvframe2Rgba(不支持 10bit,tone map 属 GPU 车道)——观察器对 10bit 跳过出图,只判契约。

硬解 P010 实测补进 §6.7:帧全到但 meanY=16(limited 黑),断点是 getImageType/getDxFormat 无 P010 行 + ycbcr 表只有 8bpp,属阶段 2 整层闭环。

验收:play_regress 离线子集 11/11(含三条新用例),ctest 全绿,hdrtest 对 base/_aud 双素材 PASS。

### 6.12 API 面落地:onHdrMeta 宿主回调 + setHdrMode(2026-09-15)

**回调挂在 IMediaPlayerOb(非 ISurfaceRenderOb)**:HdrMeta 是流级媒体属性(每流至多一次),与 ISurfaceRenderOb 现有四个回调(onFrame/onRender/onSurface/onWinSizeChange,全是渲染循环/帧级事件)语义不合;宿主消费场景(HDR 标识/显示器切换/模式决策)也是 player 级。派发点 `VideoTrack::onHdrMeta`——内部喂 `windowRender->setHdrMeta` 的同时经 `mediaPlayer->Observer<IMediaPlayerOb>::dispatch` 给宿主。**track 跨 open 复用**,`bHdrMetaSent` 必须在 `onVideoDesc`(每次开流解码器构造后必回调)复位,否则第二个 HDR 流不回调。

**setHdrMode {follow/forceSDR/forceHDR} 挂 ISurfaceRender**(渲染策略,与 setColorSpace 并排;默认实现 {}, 既有实现者零影响)。语义:follow=按流自动(HDR 进 tone map 出 SDR);forceSDR=恒 tone map(当前与 follow 等价,HDR 直通输出落地后才有分野);forceHDR=shader `processColor` 入口直接返回(PQ 编码值原样落帧,SDR 表面过曝属预期,等 HDR 交换链)。贯通链:SurfaceRenderVk→VkVideoRender→VkYUV2RGBALayer→ColorYuvUBO.hdrMode(offset 88,原 pad)→V5 shader 分支。

**顺手挖出一个潜伏缺陷**:`VkLayer::updateUBO` 只 memcpy CPU 暂存,真正上传在 `onPreFrame` 且需 `bParametChange` 置位——旧 `setHdrMeta` 运行时更新从未真正到过 GPU(被「meta 恒先于建图到达」掩盖,setHdrMeta 补置位修复),新 setHdrMode 首版同样踩中(实测 forceHDR 后画面纹丝不动才暴露)。

**判据教训两条**:①forceHDR 档等首帧后才切模式,末帧流内时间与 follow/SDR 播错位 0.5s,testsrc2 移动图案把逐像素对比变噪声(vs follow midLift -2.6 虚报)——改为**按固定渲染帧序号采样**(三档都取第 90 帧,内容按帧序天然对齐,vs sdr diffRatio 0.71→0.02);②midLift>10 阈值贴边(AUD 素材 9.4)且 ACES 压暗/抬升在频带内正负抵消——tone map 的硬信号改 diffRatio(无 map ≈0.02),midLift 降为方向哨兵(>3)。forceHDR 三向判定:follow≠sdr(map 开)+force≠follow(diffRatio>0.5)+force≈sdr(<0.1)。

验收:hdrtest 对 base/_aud 双素材 PASS(meta=1/1000, force diffRatio 0.02/0.69),play_regress 离线子集全绿,ctest 全绿。HDR API 面缺口就此关闭,剩余:硬解 P010(§6.7 三断点)/非 Vulkan 后端 tone map/HDR 直通输出。
