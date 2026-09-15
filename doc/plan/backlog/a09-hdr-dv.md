# A-9 HDR/DV:P5 兼容层 + 高质量 tone mapping + HDR 直通 + SDR↔HDR

优先级 P1 · 里程碑 M4 · 计划状态:就绪(基线比 backlog 想象的好)
对标 libplacebo 关键路径;SDR↔HDR 转换是 SenPlayer 同款卖点。

## 出口判据

1. DV Profile 5 素材可播、色彩合理(兼容层口径,不承诺 RPU 精确)。
2. tone mapping 质量:与 libplacebo 同素材对比截图可打(色映射/高光滚降不翻车)。
3. HDR 直通三平台真机出图(HDR 显示器)。
4. SDR→HDR 上变换可用(或明确降级为「不做」的结论文档)。

## 现状(代码落点)

- **tone map 三车道已完成**(比 HDR管线改造计划.md 文档自述的「未启动」新):
  Vulkan compute `glsl/source/yuv2rgbaV5.comp:65-103`(PQ EOTF/HLG 逆 OOTF/ACES 峰值压缩/
  BT2020→BT709)+ DX11 CS(`src/avox_windows/dx11/Dx11CSVideoRender.cpp:41-92`)+
  Metal(`src/avox_apple/MetalRender.mm:37-40`)+ EGL(`src/avox_egl/EglVideoRender.cpp:41-79`);
  三态 `HdrMode{follow,forceSDR,forceHDR}`(`src/avox/AvoxVideo.h:65-68`)。
  实测记录:原生车道 vs vk 中转车道 luma 差 ≤0.2,三平台对齐(HDR管线改造计划.md §6.18)。
- **元数据解析三路冗余已通**:帧 side data + 自研 SEI 裸流兜底
  (`FFVDecoder.cpp:171,202,260` + `src/avox/codec/H265Common.hpp:793-815`),
  `HdrMeta` 下发渲染与宿主回调 onHdrMeta(SWIG 已导)。
- **HDR 直通只有半截**:forceHDR 跳过 tone map(PQ 编码值原样落帧),
  但 swapchain 无 HDR 输出——Vulkan 无 VK_EXT_swapchain_colorspace、恒 SRGB_NONLINEAR,
  Windows 无 SetColorSpace1/DXGI_HDR_METADATA;文档明确「待 HDR 真机」(§2.4/§6.16)。
- **Dolby Vision 纯空白**:无 RPU/EL/profile 识别任何代码;HDR管线改造计划.md:18,319
  明确首版非目标、验收口径「DV 走 HDR10 兼容层,Profile 5 无 RPU 处理不承诺」。
- **SDR→HDR 上变换零代码**;HDR10+ 动态元数据(ST2094)未解析。
- **文档漂移警告**:HDR管线改造计划.md 自称基线未实施,实际块 1(硬解 P010)/块 2(三车道
  tone map)已完成——引用其「现状盘点」须以代码为准。

## 任务拆解

- [ ] T1 P5 兼容层(先行):识别 DV config(dvh1/dvcC box / NAL rpu 标记)→ 按 PQ(HDR10)
      车道处理,忽略 RPU;P5 无兼容层色彩会偏(单一 PQ 曲线近似),可加静态补偿曲线选项。
      落点:FFVDecoder/FFHelper 流标签识别 + 现有 PQ 车道,无新渲染代码。
- [ ] T2 tone mapping 增强(对标 libplacebo 关键路径):当前 ACES;补 BT.2390 EETF 选项 +
      峰值处理(maxCLL 静态已有 → 帧级 maxRGB 动态可选);参数(对比度/饱和度)进现有
      UBO 通道(`VkYUV2RGBALayer` setHdrMeta/setHdrMode 模式,运行时重传不重建)。
      三车道 shader 同源原则不破。
- [ ] T3 P7→P8.1 评估:P7(BL+EL+RPU)剥 EL 得 HDR10 兼容 BL 的可行性——ffmpeg 解封装侧
      EL 剥离行为先验证(hevc bsf / 提取参数),出评估文档再决定做不做。
- [ ] T4 HDR 直通收尾:Vulkan VK_EXT_swapchain_colorspace(HDR10_ST2084)+
      Windows SetColorSpace1 + DXGI_HDR_METADATA;三平台 HDR 真机验证(HDR管线改造计划.md
      块 3 脚手架已就绪)。
- [ ] T5 SDR→HDR 上变换:逆 tone map/EETF 反推 + 亮度提升;先出可行性小样
      (质量不达「卖点」线就降级为不做,别硬上)。
- [ ] T6 文档更新:HDR管线改造计划.md 加「基线过时」注记,块 1/2 状态改为完成。

## 验收

- P5 样片(DV mkv)三平台播放,截图对比 libplacebo 同参数;HDR 直通真机截屏归档 avox-test;
  tone map 增强前后同帧对比图。

## 风险与开放问题

- HDR 真机(显示器/电视)设备依赖,T4 排期绑设备。
- P7 的 EL 剥离在 ffmpeg 侧行为未知(双层流的 nal 单元顺序/时间戳),T3 评估可能直接否掉,
  不阻塞 T1/T2。
- P5 补偿曲线是「画质口」问题,验收标准主观性强,以对比截图 + 维护者拍板为准。
