# Linux 硬解/硬编与 VK 管线对齐方案

> 状态: 进行中 · 上次核对: 2026-10-05 · 权威源: -


> 目标: Linux 硬解从「GPU→CPU 回读再走软解链路」升级为与 Windows(D3D11)/Android(AHB) 同构的
> 「解码 GPU 帧 → dma-buf 零拷贝导入 Vulkan」车道；硬编从零补一个 FFVaapiEncoder，
> 挂进既有 vEncoders 注册表。HDR/10bit 按 [多平台HDR统一重构方案](gpu/多平台HDR统一重构方案.md) 的
> lane 口径接入，不在本期单独发明。
>
> v2(2026-10-05): 并入三路独立评审(解码链路/硬编链路/VK设计)。主要修正: ①预编译 FFmpeg
> 缺 libdrm, DRM_PRIME 映射当前一票否决(P1 重编时一并解决); ②「FFVk HEVC 协商失败」欠账
> 已还(2c2444b), 残余缝隙改述; ③dmabuf 帧生命周期改 Android 式解码器池契约(勿抄 dx11);
> ④设备一致性改 render-node 显式匹配(fstat dma-buf rdev 在内核不成立); ⑤P010 枚举名勘误;
> ⑥FFVaapiEncoder 第四处接线(encode(YUVFrame) 上载 override)+自动注册陷阱; ⑦P3 改走
> FFmpeg 原生 DRM_PRIME→VAAPI 导入, 且 VkSharedImage 的 Linux 腿是新建不是扩展。

## 0. 一句话结论

- **硬解**: 车道框架已就位（FFVADecoder/FFVkDecoder 已注册、能出帧、失败降级链完好），差的只是
  「最后一公里」——帧目前 `av_hwframe_transfer_data` 回读成 CPU NV12（10bit 素材回读出的是 P010）
  再上传 VK。对齐工作 = 把回读换成 dmabuf 导入（`AV_PIX_FMT_DRM_PRIME` → `VK_EXT_external_memory_dma_buf`
  + `VK_EXT_image_drm_format_modifier`），并补 vp9/av1 车道。
- **硬编**: 注册/选型/转码管线三件套全平台对称且现成，Linux 只是没有任何视频编码器可注册——
  缺一个 `FFVaapiEncoder` + 四处接线 + FFmpeg 白名单重编（encoder 与 libdrm 一次编完）。
- **硬约束**: WSL2 无 /dev/dri 且 dzn/lavapipe 无 video 队列——DRM_PRIME 零拷贝与 vaapi 硬编
  无法在 WSL 验证（WSLg 有 mesa d3d12 VA-API 驱动，解码→CPU 映射可做功能 smoke）；
  P1 起的实质验收需要真 Linux 机（Intel 核显优先）。

## 1. 现状盘点（证据）

### 1.1 硬解

| 项 | 现状 | 证据 |
|---|---|---|
| VAAPI 车道 | 已注册（Linux 专属，h264/h265），出帧回读 CPU 后走软解链路；无设备自动降软解 | `FFVADecoder.cpp:14-36, 38-42, 96-121` |
| Vulkan Video 车道 | 已注册（`AVOX_ENABLE_VULKAN` 门, 非 Apple 全平台，h264/h265），同样回读；FFmpeg 自建 VkDevice | `FFVkDecoder.cpp:21-43, 45-51, 102-127`；注册点 `AvoxManager.cpp:161-162` |
| 选型优先序 | **Linux 硬解主路按名选型是 VAAPI**（`getDefaultDecoderName` Linux 分支返 vaapi 名，注释明写「主路 VAAPI，失败回退 vulkan→软解」）；FFVk 注册序靠前只在按名未命中回退时吃到 | `AVTrack.cpp:279-281, 291-293`；`AvoxManager.cpp:162` vs `:194` |
| Windows GPU 车道参照 | D3D11 纹理直通: `GpuFrame{buffer=常驻纹理, context=解码器(兼 Dx11Context)}` → `dispatch(onDecodeGpu)`；**零逐帧释放**（releaseGpuFrame 在 Win/Linux 是空实现，只有 Android/Apple 分支） | `FFDx11Decoder.cpp:119, 224-244`；`FFDx11Decoder.hpp:15`；`VideoBuffer.cpp:104-124` |
| vp9/av1 车道 | Windows dx11 有（含 profile 探测）；Linux 两条车道都没注册 | `FFDx11Decoder.cpp:94-114, 125-141` vs `FFVADecoder.cpp:14-36` |
| 帧载荷契约 | `GpuFrame`（AvoxSource.h:75-84）+ `VCodecTh{cpu,vulkan,iosVT,androidMC,dx11}`（AvoxCodec.h:97-109）；VK 渲染链**只消费 `frame.context`**（全链 cast context，无任何路径读 frame.buffer） | `SurfaceRenderVk.cpp:378-384`→`WindowRender.cpp:202-217`→`VkInputLayer.cpp:342` |
| 硬解重置保护 | `bHardDecode` 白名单硬编码 dx11/androidMC/iosVT 三值；Linux 两车道 `codecTH=cpu` 不在其列，seek/换轨重建时不走 `flushFrames` | `VDecoderTask.cpp:164-166, 260-261, 310-312` |
| 失败上浮 | 已闭环: open 失败→openFailed；未出帧连续 send 失败≥3→openFailed(2c2444b)；选型链 tryNextFallback；零帧 5s 看门狗。**残余缝隙仅一处**: `avcodec_receive_frame` 非 EAGAIN 错被吞成 dataNoReady，只能等看门狗而非立即降级 | `FFVDecoder.cpp:120-124`, `FFDecoder.cpp:26-33, 52-55`；`VDecoderTask.cpp:426-429, 447-465` |
| VK 平台导入件 | VkWinImage(DX11)/VkAndImage(AHB)/VkIosImage(IOSurface) 齐备；**VkSharedImage 的 Linux 腿结构性缺位**: createExportable/exportHandle/importFromHandle 全只有 _WIN32/__ANDROID__ 分支，Linux 下 importFromHandle 不分配内存仍无条件返回 true，全仓 `vkGetMemoryFdKHR` 零命中 | `share/VkSharedImage.cpp:78-321`（106/193-232/234-321） |
| Linux 缺口 | dmabuf 导入零实现（`VK_EXT_external_memory_dma_buf`/`VK_EXT_image_drm_format_modifier` 全仓无命中）；**且预编译 FFmpeg 未编 libdrm**（libavutil.pc Libs.private 无 -ldrm，CONFIG_LIBDRM=0 → hwcontext_vaapi/vulkan 的 DRM_PRIME 映射分支整体不存在） | grep 实测；`3rdparty/library/linux/ffmpeg/lib/pkgconfig/libavutil.pc` |
| 文档欠账 | 「VAAPI→Vulkan 零拷贝 (DMA-BUF 导入)」挂在待办 | `doc/platforms/linux/构建与验证.md:71` |

### 1.2 硬编

| 项 | 现状 | 证据 |
|---|---|---|
| 注册框架 | 与解码对称: `regFFCodec()` 把编码器注册进 `vEncoders`（工厂=通用 FFVEncoder），按名选型，未命中回退首个注册项 | `FFHelper.cpp:69-97`, `VideoStream.cpp:24-63` |
| Linux 选型现状 | 白名单只编 `--enable-encoder=aac` → `regFFCodec` 注册不出任何视频编码器 → `VideoStream` 查 codecId 直接提前 return（**走不到按名回退那一步**）；结论=转码链彻底不可用 | `build_ffmpeg_linux.sh:46`；`VideoStream.cpp:26-29` |
| 死宏 | `AVOX_FFVAAPI_H264/H265_ENCODER="ff_h264_vaapi"` 已定义零使用（注意值不是 FFmpeg 真名 `h264_vaapi`；`AVOX_FFVULKAN_*_ENCODER` 同款死宏） | `Muxer.hpp:22-27` |
| 参照实现 | Windows FFDx11Encoder 完整但注册被注释；**它只 override 了 encode(GpuFrame)**，而转码车道喂的是 YUVFrame（FFVEncoder::encode 直接把 CPU 指针塞 data[]——对 h264_vaapi 必死） | `FFDx11Encoder.cpp:34, 85`；`FFVEncoder.cpp:180-192`；`RawMuxer.cpp:113-122`→`VideoStream.cpp:65-73` |
| FFmpeg 白名单 | Linux 只开 `--enable-encoder=aac`，无视频编码器；hwaccel 已含 h264/hevc_vaapi + h264/hevc/vp9/av1_vulkan（全解码）；libva 已随包链接（-lva/-lva-drm/-lva-x11），**缺 libdrm**；`--disable-swscale/avfilter` 不影响上载（hwcontext 回调不经 swscale） | `build_ffmpeg_linux.sh:35, 46-47`；libavutil.pc |
| 渠道兜底 | commercial 渠道软编兜底名注入无 Linux 分支；agpl 分支同样没有 → Linux 兜底名恒 libx264（库里没有） | `cmake/AVOXOptions.cmake:51-63` |
| 转码管线 | `TranscodeRecorder` 按「硬编给 NV12、软编给 yuv420P」分流，`rec.hard.encode` 默认 true；Linux 从未跑过 GPU 帧 | `TranscodeRecorder.cpp:326-333`, `OptionKey.hpp:59` |
| 测试口径 | rec-transcode 两用例在 `LINUX_OFFLINE_SKIP`（avox-test 仓，本仓 `script/testenv` 已删；ANDROID_OFFLINE_SKIP 同款名单勿误删） | `../avox-test/script/testenv/play_regress.py:114-117` |

### 1.3 构建/验证环境

- WSL2 Ubuntu 24.04 开发环已跑通（构建/软解/窗口/矩阵离线子集），构建目录须在 ext4（`doc/platforms/linux/构建与验证.md`）。
- WSLg 的 Vulkan 是 dzn(D3D12 上的 Mesa Dozen)/lavapipe：无 video decode/encode 队列；WSL2 无 /dev/dri。
  但 WSLg 自 2023 起提供 D3D12 GPU video acceleration（mesa d3d12 VA-API 驱动）——**解码→CPU 映射路径
  可在 WSL 做功能 smoke**；DRM_PRIME 导出与 vaapi 硬编无门。实质验收必须真机。
- 真机首选 Intel 核显 + iHD（PRIME_2 导出+modifier 生态最成熟，mpv/firefox/gstreamer 主力路径，
  i915 内核隐式 fence 稳）；AMD radeonsi 二级验证；NVIDIA 私有驱动不押 VAAPI 腿（见 R1）。
- Linux FFmpeg 为预编译包（`3rdparty/library/linux/ffmpeg`），白名单改动须在 WSL 重编并更新该目录。

## 2. 目标架构

### 2.1 解码: 两条腿都出 DRM_PRIME，VK 侧一个导入件

```
FFVADecoder (VAAPI) ──av_hwframe_map→ AV_PIX_FMT_DRM_PRIME (AVDRMFrameDescriptor)
FFVkDecoder (Vulkan Video, 有前提, 见 P2-3) ─┘
                    │  GpuFrame{ context=解码器(兼 Linux IRenderContext, 携 DmaBufDesc) }
                    ▼
   VkInputLayer 新增 dmabuf 分支 → 新 VkDmaImage (src/avox_vulkan/linux/)
                    │  VK_EXT_external_memory_dma_buf + VK_EXT_image_drm_format_modifier
                    ▼
   既有 VK 管线 (按平面拷贝或 ycbcr 采样, 见要点6)
```

要点:

1. **载荷走 context 不走 buffer**: 渲染链全按平台 cast `GpuFrame.context`，故 `FFVADecoder` 照
   `FFDx11Decoder : Dx11Context` 样式实现 Linux 版 `IRenderContext`，携 DmaBufDesc
   （objects[].fd、layers[].offset/pitch、modifier、fourcc、宽高）。**生命周期抄 Android 不抄 dx11**:
   dx11 是常驻纹理零逐帧释放，dmabuf 是每帧 fd 必须逐帧管理——fd 所有权挂解码器侧池（Android
   GLESContext/AndVDecoder.cpp:502 的 onFrameRelease 同款），GpuFrame 拷贝（VideoTrack 与
   AMediaSource 各一份、逐份 release）只带池句柄，杜绝双重 close。`releaseGpuFrame` 加 Linux 分支
   （`VideoBuffer.cpp:104-124`），`needReset` 加 Linux 的 context/池代次比对（:126-154 现只认
   WIN32/ANDROID），`HwVideoBuffer` 恒置 `bufferType=dx11`（:86）需补枚举值或注明不适用。
2. **`VCodecTh::dmabuf` + 两张硬编码名单**: 新枚举值加进 `VDecoderTask.cpp:164-166, 260-261` 的
   `bHardDecode` 判定，否则 seek/重建不走 `flushFrames`，队列残留帧悬空 fd。`selectRenderType`
   default 落 Vulkan 无需改（:506-522）。
3. **fd 语义**: FFmpeg `vaapi_map_to_drm` 接管 vaExportSurfaceHandle 的 fd 不 dup、unmap 时 close——
   map 出的 AVFrame 须保活到 `vkBindImageMemory2` 完成；`VkImportMemoryFdInfoKHR` 导入**成功**后
   fd 所有权归驱动，失败才由我方 close。两态都要在池里分得清。
4. **新 `VkDmaImage`**（`src/avox_vulkan/linux/VkDmaImage.{hpp,cpp}`，与 VkWinImage/VkAndImage 同辈）:
   - 建图: tiling=`VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT` + `VkImageDrmFormatModifierExplicitCreateInfoEXT`；
     modifier 先过 `VkDrmFormatModifierPropertiesListEXT` 白名单（(format,modifier) 不在枚举里不建）；
     `DRM_FORMAT_MOD_INVALID`（=0x00ffffffffffffff）不当真 modifier 用，直接落 LINEAR
     （LINEAR 本尊=0）；`drmFormatModifierPlaneCount` 必须 == 属性枚举值（VUID-02265），且注意
     iHD Gen12 NV12 可导出 GEN12_MC_CCS，plane 数=4（2 数据面+2 CCS 辅助面），按面数展开别写死 2；
   - 内存: **dedicated allocation 必带**（VkAndImage.cpp:95-100 先例；VkSharedImage 非 dedicated
     导出有 SIGSEGV 真机教训），`vkGetMemoryFdPropertiesKHR(DMA_BUF, fd)` 拿 memoryTypeBits 求交
     （VkWinImage.cpp:136-148 同款），`vkAllocateMemory`+`VkImportMemoryFdInfoKHR`+`vkBindImageMemory2`；
   - 采样: plane i 对应 `VK_IMAGE_ASPECT_MEMORY_PLANE_i_BIT_EXT`，Ycbcr 转换复用
     `decode/VkYcbcrConversion`；fourcc: NV12→`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`、
     P010→`VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16`。
5. **设备一致性**: dma-buf fd 是匿名 inode，`fstat rdev` 不携带导出设备——判定改解码侧显式做:
   `FFVADecoder` 枚举并打开 `/dev/dri/renderD*`，`vaGetDisplayDRM(fd)` 建 VADisplay（替换现在的
   NULL 自选，`FFVADecoder.cpp:41,67`），fstat **该节点 fd** 得 major/minor 记入 DmaBufDesc，与
   `VkPhysicalDeviceDrmPropertiesEXT`（VK_EXT_physical_device_drm，Mesa 全系有、NVIDIA 无）比较；
   扩展不在场或节点不匹配 → 一律回读回退。判定工具加进 `share/VkShareChecker`（bSameGpu 的 drm 版）。
6. **同步**: 机制归因是**内核层隐式 fence**（i915/amdgpu 对共享 BO 自动在 dma-buf resv 等待/追加），
   Mesa 用户态刻意不做隐式同步（Collabora 2022 权威文），Vulkan 规范对 dmabuf 隐式同步只字未提，
   Xe 新内核驱动未验。因此: **首版默认 mpv 同款保险——出帧后 `vaSyncSurface` 再交付**（只等该
   surface 非全管线串行），隐式同步作为实测通过后的优化项；external fence SYNC_FD 放 P3 之后。
   编码方向（VK 渲染完→VA 编码读）同款问题同款档位。
7. **回退链**: dmabuf map/import 任一步失败 → 解码器回退现有 `av_hwframe_transfer_data` 回读
   （已在线上跑，是保底不是新代码）；硬解整体失败 → openFailed/decodeLaneDead → 软解（链路已闭环）。
   **录制/转码侧**（AMediaSource.pushFrame→TranscodeRecorder）在 Linux 从未消费过 GPU 帧，
   P2 落地时明确「录制链遇 dmabuf 车道强制走回读拷贝或降软解」，P3 之前不喂它 dmabuf。
8. **VkInputLayer 不是零改动**: 现有 GPU 输入分支假设单平面 RGBA `copyImage`（VkInputLayer.cpp:201-262），
   NV12 dmabuf 双平面要么按面双拷到双纹理、要么走 ycbcr 采样 shader，图形路径要写明。

### 2.2 硬编: FFVaapiEncoder + 四处接线 + 两个陷阱

```
CPU NV12 (P1) ──encode(YUVFrame) override: av_hwframe_get_buffer+transfer_data 上载→ VAAPI surface ─→ h264_vaapi/hevc_vaapi
VK 输出图 (P3) ──vkGetMemoryFdKHR 导出 dmabuf→ 组 AVDRMFrameDescriptor → av_hwframe_ctx_create_derived+av_hwframe_map ─→ VAAPI 帧
```

- 新 `src/avox_ffmpeg/encoder/FFVaapiEncoder.{hpp,cpp}`: `: public FFVEncoder`（照 FFDx11Encoder
  结构换 VAAPI），override 两处——`onAttachContext`（VAAPI hwdevice+hwframes: sw_format=NV12、
  initial_pool_size 设 surface 数——**别抄 dx11 的 `av_dict_set("surfaces",8)`，那是 QSV 词汇**；
  `codecCtx->pix_fmt=AV_PIX_FMT_VAAPI`、hw_device_ctx/hw_frames_ctx）与 `encode(const YUVFrame&)`
  （CPU→VAAPI surface 上载，这层 FFDx11Encoder 没有对应物，必须自己做；TranscodeRecorder/VideoStream
  不用动）。
- 四处接线:
  1. 注册: `AvoxManager.cpp` Linux 分支（:192-194）加 `regFFVaapiEncoder()`；
  2. 选型: `Muxer.cpp:36-64` 非 Android/Apple 分支拆出 Linux 分支返回 vaapi 名；
  3. preset 分发: `FFVEncoder.cpp:117-135` 加 vaapi 分支（按 FFmpeg 真名 `h264_vaapi` 匹配，
     rc=ICQ/QP、compression_level）；
  4. **encode(YUVFrame) 上载 override**（上面说的第四处，最易漏）。
- FFmpeg 白名单（**P1 一次编齐**）: `--enable-encoder=h264_vaapi,hevc_vaapi` +
  `--enable-hwaccel` 补 `vp9_vaapi,av1_vaapi` + **`--enable-libdrm`**（需 `apt install libdrm-dev`；
  不加它 DRM_PRIME 解码映射在 P2 也是死路）。FFmpeg 原生 vaapi 编码器/hwcontext 是 LGPL，商业渠道无碍。
- **陷阱A 自动注册**: 白名单加进 h264_vaapi 后，`regFFCodec` 会按 codec->name=`h264_vaapi` 把通用
  FFVEncoder 自动注册进 vEncoders，且注册序（AvoxManager.cpp:158）早于 Linux 分支——「未命中回退
  首个注册项」会静默命中这个没有 hw_device_ctx 的必坏实例。对策: regFFVaapiEncoder 的注册名与
  getDefaultEncoderName 返回值必须严格一致（现成宏值 `ff_h264_vaapi` 与 FFmpeg 真名不一致，二选一:
  自定义注册名，或宏值改成真名），并让必坏的自动注册项不可达（选型名优先命中自定义项）。
- **陷阱B 渠道兜底**: `AVOXOptions.cmake:51-63` 加 Linux 分支（commercial 与 agpl 都要给兜底策略）；
  P1 验收加一条「无 VAAPI 设备时 rec-transcode 必须给出明确 openFailed/选型降级，而非静默无输出」
  （现在只有 VideoStream.cpp:48 一条 warn，失败拖到首帧才爆）。

### 2.3 平台对照（对齐后的形态）

| 平台 | 解码腿 | 帧载荷 | VK 导入 | 硬编 |
|---|---|---|---|---|
| Windows | FFDx11Decoder | ID3D11Texture2D*（常驻纹理） | VkWinImage (win32 shared) | h264_mf/hevc_mf |
| Android | AndVDecoder | AHardwareBuffer*（池+归还） | VkAndImage (AHB) | MediaCodec |
| Apple | VT 解码 | CVPixelBuffer/Metal | Metal 腿 | VideoToolbox |
| **Linux(本方案)** | FFVADecoder/FFVkDecoder | **DmaBufDesc（解码器池，Android 式）** | **VkDmaImage (dma-buf)** | **h264/hevc_vaapi** |

## 3. 分期实施

每期独立可验收、可停靠。依赖: P2 依赖 P0 与 P1 的 FFmpeg 重编；P0/P1 代码面互相独立。

### P0 清障加固（纯代码 + FFVk 侧 vp9/av1，不动白名单）

1. Linux 车道补 vp9/av1——**只做 FFVk 侧**（`vp9_vulkan/av1_vulkan` hwaccel 已在白名单，
   build_ffmpeg_linux.sh:47）；onVaild 加能力探测（Vulkan video queue caps / 后续 VAAPI 同款），
   照 FFDx11Decoder 的 profile 探测样式（FFDx11Decoder.cpp:125-141）。VAAPI 侧 vp9/av1 的
   `vp9_vaapi,av1_vaapi` hwaccel 并进 P1 重编。
2. FFDecoder 残余缝隙: `avcodec_receive_frame` 非 EAGAIN 错（FFDecoder.cpp:52-55）现在被吞成
   dataNoReady 只能等 5s 看门狗——升格为立即计失败上浮 openFailed（连续 send 失败机制 2c2444b
   的同款思路）。注意: 原方案此处的「FFVk HEVC 协商失败未上浮」经评审证实在 2c2444b 已修。
3. 验收: WSL 全自动降软解零回归（离线矩阵，含解码→CPU smoke）；真机探测通过的 codec hw=1 绿，
   探测不过的静默降软解（av1_vaapi 需 Gen12+、vp9_vaapi 需 Gen9.5+，**别按「全 codec 全绿」验收**）。

### P1 Linux 硬编可用 + FFmpeg 重编（一次编齐 encoder+libdrm+vaapi hwaccel）

1. FFVaapiEncoder + 四处接线 + 白名单重编（§2.2；重编产物同步更新
   `3rdparty/library/linux/ffmpeg`，验收项加「CONFIG_LIBDRM=1 与新 encoder 在包里」检查）。
2. 解锁矩阵: 摘除 `../avox-test/script/testenv/play_regress.py:114-117` `LINUX_OFFLINE_SKIP`
   的 rec-transcode 两项（ANDROID_OFFLINE_SKIP 的同名项别动）。**勘误(1005夜)**: CI release.yml
   的 Linux runner 无 /dev/dri，摘出必红——离线子集保持跳过，仅真机环境本地解锁验收。
3. 验收: 真机 rec-transcode-* 出片，码率/参数与 Windows h264_mf 对照合理；无设备环境明确失败
   可见（陷阱B 验收项）；离线矩阵全绿。

### P2 解码零拷贝 dmabuf（本方案主体）

1. 载荷与生命周期: FFVADecoder 出 DRM_PRIME 帧 + Linux IRenderContext + 解码器 fd 池；
   `VCodecTh::dmabuf` + bHardDecode 名单 + releaseGpuFrame/needReset/HwVideoBuffer 三处适配
   （§2.1 要点1/2）。
2. VkDmaImage + VkInputLayer dmabuf 分支（图形路径按要点8 落）+ VkCommon 装配:
   instance Linux 分支补 `VK_KHR_EXTERNAL_MEMORY_CAPABILITIES`+`GET_PHYSICAL_DEVICE_PROPERTIES_2`
   （VkCommon.cpp:67-88 现只有 surface 扩展）；device 侧 Linux 分支（:454-473 现完全没有）补
   `VK_KHR_EXTERNAL_MEMORY/_FD`、`BIND_MEMORY_2`、`GET_MEMORY_REQUIREMENTS_2`、
   `DEDICATED_ALLOCATION`、`SAMPLER_YCBCR_CONVERSION`、`VK_EXT_EXTERNAL_MEMORY_DMA_BUF`、
   `VK_EXT_IMAGE_DRM_FORMAT_MODIFIER`；`bInterpDmaBuf()` 能力探测（:324-368 旁边）；设备一致性
   （要点5）。同步首版 vaSyncSurface 保险（要点6）。
3. FFVk 腿验证 DRM_PRIME，两个前提写死在实现里: (a) FFmpeg 自建设备须带
   VK_EXT_image_drm_format_modifier（否则 vulkan_map_to_drm ENOSYS）；(b) 帧内存只有
   hwframes ctx 的 tiling=DRM_FORMAT_MODIFIER_EXT 才挂 DMA_BUF 导出，而 vulkan_decode 默认收成
   OPTIMAL——须自建 hw_frames_ctx 强制 modifier tiling（FFmpeg 9.0.1 hwcontext_vulkan.c:2865-2868、
   vulkan_decode.c:1238-1239）。不通则维持回读，不阻塞。
4. 验收: 真机 Intel 核显 4K 播放 CPU 占用对比回读显著下降；avox-test shot 像素对照（dmabuf vs
   回读 PSNR/逐位）；**同步翻车判据先行定量化**（隔帧撕裂/花屏的复现口径），Prime 混合显卡与
   老 i915（无 vaExportSurfaceHandle）回退不炸；录制链按要点7 处理不悬空。

### P3 编码零拷贝 + 10bit/HDR 对齐

1. VK→VAAPI 导入**走 FFmpeg 原生路**: hwcontext_vaapi 的 `av_hwframe_map`(DRM_PRIME) 内部就是
   `vaCreateSurfaces(VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)` 且带 prime_2 不支持时的降级——
   我方只需 `vkGetMemoryFdKHR` 导出 fd 组 `AVDRMFrameDescriptor` + `av_hwframe_ctx_create_derived`
   + map，无需手写 libva surface 管理（外部注入 VASurface 的 AVVAAPIFramesContext 路线上游已不公开）。
2. 导出侧前置: 被导出的渲染图必须建在 DRM modifier tiling + 可导出内存上——`VkSharedImage.cpp:131/255`
   现写死 `VK_IMAGE_TILING_OPTIMAL`，且 Linux 的 createExportable/exportHandle/importFromHandle
   三条腿都是空的（§1.1）——**这是 P3 最大隐性成本**，等于把 share 框架的 Linux 腿按 VkDmaImage
   经验补建。
3. 编码器 hwdevice 必须显式 pin 与 VK 同一 render node（`av_hwdevice_ctx_create` 的 device 串），
   否则跨卡导入必败。
4. P010 全链（硬解 10bit dmabuf → VK ycbcr 10bit → tonemap/直通），按
   [多平台HDR统一重构方案](gpu/多平台HDR统一重构方案.md) 的链 A/F/E lane 口径接入；注意 Android
   P010 round-trip 双重归一化（`>>6` 两次）的前车之鉴。
5. 验收: HDR10 素材硬解硬编端到端，色准对照 Windows 车道；同步双向（解码+编码方向）无撕裂。

### P4 观望项（明确不做进本期）

- 自研 VK Video encoder（`VK_QUEUE_VIDEO_ENCODE_BIT_KHR` 全仓未用，太新）；`avox_vulkan/decode`
  自研 VkDecoder 维持搁置（启用前先过 mpv 6086fcc 帧交接锁语义审计）。
- NVENC 正式支持（需 nv-codec-headers 重编；CUDA↔VK 输入互操作难，POC 走 CPU 上载）。
- external fence SYNC_FD 同步档（vaSyncSurface 不够时才上）。
- Linux 原生渲染器（车道B/EGL）是另一笔账（构建与验证.md:72），与本方案正交。

## 4. 风险与拍板项

| # | 项 | 说明 |
|---|---|---|
| R1 | 同步机制归因与适用面 | 隐式同步在**内核层**（i915/amdgpu dma-buf resv fence），Mesa 用户态刻意关闭、Vulkan 规范不保证、Xe 新驱动未验；首版 vaSyncSurface 保险（mpv 同款）做双向（解码+编码方向）；NVIDIA 私有驱动不参与隐式 fence 且无 VK_EXT_physical_device_drm → VAAPI 腿整体不押 NVIDIA |
| R2 | modifier 方言 | iHD GEN12_MC_CCS 的 4-plane（含 CCS 辅助面）、INVALID modifier、(format,modifier) 属性枚举白名单、老 i915 无 vaExportSurfaceHandle（FFmpeg 自动回退 vaAcquireBufferHandle 路径，再不行回读） |
| R3 | 真机资源 | P1 起实质验收需真 Linux 机（Intel 核显优先，/dev/dri + iHD）；WSL 只能验降级与解码→CPU smoke |
| R4 | FFmpeg 重编是共同前置 | libdrm（一票否决项）+ encoder + vaapi vp9/av1 hwaccel 在 P1 一次编齐；产物按库仓规矩更新 `3rdparty/library/linux/ffmpeg` |
| R5 | 自动注册陷阱 | h264_vaapi 入白名单即被 regFFCodec 以通用 FFVEncoder 自动注册且注册序靠前，未命中回退会静默命中必坏实例（对策见 §2.2 陷阱A） |
| D1 | 拍板: 车道优先序 | **现状是 VAAPI 主路、FFVk 兜底**（AVTrack 默认名，:279-293），建议维持；注意 FFVk 注册依赖 `AVOX_ENABLE_VULKAN`，发行配置若关掉则 Linux 硬解只剩 VAAPI |
| D2 | 拍板: P0-P3 排期 | P0/P1 可先行且 P1 重编是 P2 前置；是否一次排到 P3 |
| D3 | 拍板: nvenc 要不要进 | NVIDIA 用户在 P3 前无硬编（解码不受影响，走 FFVk/Vulkan Video） |
| D4 | 拍板: agpl 渠道 Linux 策略 | agpl 分支无 Linux 兜底注入（恒 libx264 必未命中）；注入 vaapi 名还是明确禁编报错 |

## 5. 落地后回写

- `doc/platforms/linux/构建与验证.md`: 状态总览两行（VAAPI 硬解、h264 软编）+ 待办四项随 P0-P2 结账；
  顺带修正 :50/:53 的 `script/testenv/play_regress.py` 过时路径（实际在 ../avox-test）。
- 本文档状态头随实施推进改「已落地」；分期记录写进各期提交，不在本文堆日志。
