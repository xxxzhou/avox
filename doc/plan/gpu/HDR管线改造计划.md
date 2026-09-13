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
