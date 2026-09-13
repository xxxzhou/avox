# ASS/SSA 字幕渲染计划(avox_ass 磁力模块)

> 2026-09-13 调研与规划,同日启动实施。
> **M1 已完成**:四库产物出全并入库 **avc_library** `3rdparty/library/windows/ass/`
> (Windows: ass-9.dll/ass.lib、fribidi-0.dll、harfbuzz.dll、freetype.dll;
> fribidi/libass 已无 CMakeLists,走 meson + vcvars 批处理编排,pkg-config 用
> pkgconf 自编,构建脚本 script/ass/build_windows.py——与 script/webrtc 同模式:
> 脚本在 avox,大件产物进 avc_library);
> 核心接口 IAssOverlay + assOverlayHub(末尾追加)就位;插件 DYNAMIC 模式编译/
> 加载/查表/降级全通,samples/functest/asstest 全绿。
> **M3 插件侧提前完成**:AssOverlay 真实 libass 渲染(最小 .ass → loadFile →
> render 出 396×60 canvas @底部居中,detect_change 稳定段零开销)。
> 字体设计修正:libass 公开 API 无字体回调,AUTODETECT(Win=DirectWrite) +
> setFontsDir/setDefaultFont 兜底。
> 剩余:M2 解封装选轨 → §3.4 VK 图层合成 → panvox shim → PGS。
> 修订史:①三方库不走 3rdparty 直连,
> 按 [动态加载组件设计](../player/动态加载组件设计.md) 以 `plugins/avox_ass`
> 磁力模块接入,libass/FriBidi/HarfBuzz 独立仓预编译,核心零依赖;②上屏复用
> VK PipeGraph 图层(与现有 SRT 引擎内渲染同槽位),字幕随输出帧到达消费端;
> ③渲染固定 Vulkan(`setVulkan(true)`),shim 不做位图导出——字幕只随帧走,
> 越简单越好。
> 背景:panvox 定位修订(panvox 仓 ADR-0006)把「ASS/PGS 完整渲染」列为播放
> 核心验收硬指标。行号基于当前 main,仅作定位参考。

## 1. 背景与目标

两轮市场痛点调研结论一致:国内 ExoPlayer 系客户端五年未完整支持 ASS(动漫字幕组
场景全员中招);海外 r/jellyfin 抱怨第一名是 PGS/ASS 无法本地渲染 → 服务端烧录 +
全片转码。字幕本地完整渲染是自研引擎对 ExoPlayer 系客户端的核心差异化。

目标(首版):

| 项 | 内容 |
|---|---|
| 内封字幕轨 | MKV 中 ASS/SSA/SRT 轨枚举、选轨、完整渲染 |
| 外挂文件 | .ass / .srt 加载渲染 |
| 特效完整度 | **libass 全量实现**,不是自研子集(\pos/\move/\t 动画/卡拉OK/碰撞/嵌套样式) |
| 接入形态 | `plugins/avox_ass` 磁力模块,三方库独立仓预编译,缺失时优雅降级不崩 |
| 输出形态 | RGBA canvas 经 **VK PipeGraph GPU 合成**进输出帧;输出跨 API 直通(D3D11 共享句柄/AHB/IOSurface)全端可见,零拷贝、不触发 CPU 回读 |
| 字体 | FontCache/FontMap 经 C 回调喂给 libass,CJK 兜底 |

**非目标(首版)**:MKV 内嵌字体附件(attachment)解析(二期,先 FontMap 相似
字体兜底);tx3g/MOV text(顺手项,非验收);RTL 排版专项验证(fribidi 由
libass 内部处理)。**PGS 不再是纯二期**:overlay 通道按"双源"设计,PGS 位图
路径增量极小且零新增依赖,见 3.6,可随 M5 并入。

## 2. 现状盘点

### 2.1 解封装:字幕流被丢弃

- `IOParseFF.cpp:93-94` 显式只放行 `AVMEDIA_TYPE_VIDEO/AUDIO`,字幕流在
  枚举阶段即被丢弃,不进轨道系统。
- `TrackType` 已预留 `subtitle(3)`(`AvoxCodec.h:67`),无消费方。
- 播放器 JSON 描述已有 `subtitleCount` 字段(`MPCommon.hpp:185`),同样无人填充。

### 2.2 解析:仅纯文本 SRT

- `SrtParser.cpp`:逐行读索引/时间码/文本,无任何标签、样式、定位概念。
- `SubtitleFile` 数据结构 `SubtitleItem{startMs, endMs, text, original, language}`
  只能表达纯文本行。

### 2.3 渲染:SRT 走 VK 图层,但装不下 ASS/PGS

- 现有引擎内 SRT 渲染机制(`VkFontLayer.cpp`):FreeType 光栅化字形 → blit 进
  **R8 alpha canvas**(尺寸=帧/tscale)→ 仅文字变化时 staging 上传
  (`cpuBuffer` + `bufferToImage`)→ `drawFontBlend.comp` 按 `fontColor +
  opacity` 混合到视频帧;canvas 布局只支持锚点+对齐的文本块。
- 两个装不下的点:**canvas 是 R8 + 全局单色**——ASS 需要逐元素颜色(描边色≠
  填充色、逐事件淡入淡出),PGS 需要逐像素颜色;**排版是简陋文本块**——没有
  ASS 的样式表/\pos/\t/卡拉OK/碰撞。
- 结论:文字光栅化交给 libass(插件内),引擎侧复用的是**图层骨架与混合槽位**,
  不是 drawText 的字形逻辑。SRT 现有路径零改动。
- 渲染管线固定 Vulkan(`setVulkan(true)`):字幕 overlay 只做 VK 图层;
  VK 合成后的输出纹理经既有跨 API 直通(D3D11 共享句柄 / AHB / IOSurface)
  到达各窗口后端——**VK 一处实现,全端可见**,不存在 DX11/Metal 后端缺口。

### 2.4 消费端(panvox)

- panvox 现以 Flutter 层渲染 AI 字幕管线产出的 SRT(见 panvox
  `docs/design/subtitle-pipeline.md`);shim 仅 `pvx_subtitle_load(srt_path)`
  (`panvox_native.cpp:960`)。
- panvox 直通导出的就是 PipeGraph 输出纹理(Android AHB / Windows 共享句柄)
  → 字幕在图内合成后**随帧到达 Flutter,无需 Dart overlay widget、无需跨
  FFI 搬位图**,也天然不触发 CPU 回读。

## 3. 技术方案

### 3.1 avox_ass 磁力模块与预编译产物

**预编译产物**(实施落地:avoxx 仓 `script/ass/build_windows.py` 编排,产物入
**avc_library** 仓 `3rdparty/library/windows/ass/`,与 script/webrtc 同模式)
libass + FriBidi + HarfBuzz(+ 各自依赖)多平台产物——

| 平台 | 产物 | 对应模块模式 |
|---|---|---|
| Windows x64 | 动态 dll + 导入库(libass/fribidi/harfbuzz/freetype 各自 dll)✅ 已出 | DYNAMIC,`DEP_DLLS` 随插件自包含复制 |
| Android arm64 | 静态 .a(NDK) | STATIC(静态注册,jniLibs 不能枚举目录) |
| Apple(iOS/macOS) | 静态 .a | STATIC(App Store 禁止加载第三方可执行代码) |

- 版本 pin 死(脚本头注释),升级 = 改 tag 重出;产物随 avc_library 仓分发,
  插件 CMake 默认探测 sibling 目录,找到即真实链接。
- FreeType:依赖栈自带一份给 libass;核心 avox_freetype 的既有副本不动。
  Windows DYNAMIC 模式进程内两份 FreeType——无全局单例冲突,已知 tradeoff。
- 字体(实施修正):libass 公开 API 无字体回调(fontselect.h 为内部头),
  用 AUTODETECT(Win=DirectWrite/mac=CoreText/linux=fontconfig)+
  setFontsDir/setDefaultFont 兜底,比原设计更简单。

**plugins/avox_ass 骨架**(完全对齐既有模式,sherpa/onnx/cv 四例已验证):

- 核心持纯接口 `IAssOverlay`(放 `src/avox/subtitle/`,核心零 libass 依赖,
  参照 `IONNXSession` 归核心模式):喂 extradata/chunk/文件、`render(ptsMs)`
  出 RGBA canvas、字体 C 回调注入。
- `AssModule` : `IModule`,`AVOX_REGISTER_MODULE(AssModule, avox_ass)`,
  `loadModule` 里向 `AvoxManager` 工厂表 `assOverlayHub.reg("libass")` 注册工厂。
- 核心调 `ModuleMgr::ensureStarted()` lazy 触发后查表;**插件缺失/加载失败
  → 查表为空 → ASS 轨自动跳过,降级为不渲染,不崩**。
- **跨 DLL 安全**(机制文档 §6 规范):接口只传 `const char*`/原始类型/C 函数
  指针,无 STL;canvas 内存归插件所有,双缓冲 + 显式 swap,调用方只读。

### 3.2 解封装:IOParseFF 放行字幕轨(核心侧)

- 新增 `AVMEDIA_TYPE_SUBTITLE` 分支:字幕流入轨道枚举(TrackType::subtitle),
  记录 codec_id(ass/subrip/pgs/mov_text)、extradata(MKV 的 ASS 剧本头)。
- 补齐轨道明细 JSON 与选轨通路(`subtitleCount` 填充 + setTrack)。
- 字幕 packet 走旁路队列,不进音视频同步时钟;由播放时钟驱动消费。
- 此部分属核心,不进插件——插件只做"字幕数据进、RGBA canvas 出"。

### 3.3 光栅化(avox_ass 插件内)

- 初始化:ass_library + ass_renderer;`ass_set_fonts` 用字体 C 回调接
  FontCache/FontMap(无 fontconfig 平台的唯一字体入口),CJK 缺字 fallback。
- 内封 ASS:extradata → `ass_process_codec_private`;packet → `ass_process_chunk`
  (MKV 的 ASS packet 天然是 chunk,零转换)。
- 内封 SRT:FFmpeg subrip 解码 → 转 ASS event(FFmpeg 自带 srt→ass 转码路径)。
- 外挂文件:.ass → `ass_read_file`;.srt → 同上转换后 `ass_read_memory`。
- 帧渲染:`ass_render_frame(track, renderer, ptsMs, &detect_change)` →
  ASS_Image 链 → 合成单张 **RGBA8 canvas**(**按联合 bounding box 裁剪**,
  典型对白是底部横条,不传全帧),双缓冲;`detect_change` 为空时零开销跳过。
- 尺寸:`ass_set_storage_size`(视频原始分辨率);canvas 即视频分辨率坐标系
  的裁剪区,rect 随 canvas 交给图层。
- 时钟:挂播放时钟,参照 SubtitleView 的 Clock sync 模式(`AVTrack.cpp:149`)。

### 3.4 上屏:复用 VK PipeGraph(核心侧,本期主交付)

与 `VkFontLayer` 同一套骨架,画布升级为 RGBA8:

1. **上传**:照抄 VkFontLayer 的 staging 模式——canvas 变化时
   `cpuBuffer->upload()` + `bufferToImage` 进 VkTexture(rgba8, SAMPLED),
   静止段零上传。
2. **合成**:用现成 **`VkBlendLayer` + `sourceOverBlend.comp`**(shader 即
   标准 alpha-over:`mix(base, overlay, overlay.a)`),layer 自带归一化矩形
   (fx/fy/width/height/opacity)——canvas bbox 直接映射 rect 参数。不新写
   compute shader。
3. **挂载位**:与 VkFontLayer 相同——视频 YUV2RGBA/特效之后、输出/导出之前。
   顺序上晚于所有画质层,保证字幕不被超分/增强重采样。

由 2.4,panvox 直通拿到的就是图输出 → 字幕随帧出。**渲染管线固定
Vulkan(`setVulkan(true)`):overlay 只实现 VK 图层;合成后的输出纹理沿用
既有跨 API 直通(D3D11 共享句柄 / AHB / IOSurface)上 DX11/Metal 窗口,
VK 一处实现即全端覆盖。`drawText`/SRT 现有路径零改动;与 SubtitleView
互斥:ASS overlay 激活时 `drawText` 让位。**

### 3.5 核心接口与消费端

- overlay 通道定义为**通用 RGBA canvas** 接口(源无关):`{rgba 指针, w, h,
  stride, x, y, pts}` + 变更回调。双源共用:ASS 插件(3.3)/ PGS 解码(3.6)。
- 主消费方 = 3.4 的 VK 图层,**唯一通道,不做位图导出**(字幕只随帧走,
  简单优先)。shim 只有两个加载接口:
  `pvx_subtitle_load_ass(pl, track_index)` /
  `pvx_subtitle_load_ass_file(pl, path)`。
  查表失败(无插件)返回明确错误码。
- panvox 侧剩余工作只有选轨 UI 与"片源字幕 vs AI 字幕"互斥开关。

### 3.6 PGS(蓝光位图字幕)

PGS = 蓝光碟字幕格式:带时间戳的**图片**+调色板,无文字、无字体、无样式,
remux 原盘片源的主流字幕轨(海外收藏党/Emby 资源,含外语片 forcing 轨)。

- **不经 libass**(没有文字可排)。FFmpeg 内置 `pgssub` 解码器(LGPL 默认含,
  属核心已有依赖)解出位图矩形 + 调色板 → 查表转 RGBA → 直接喂 3.5 通用
  canvas 通道,上屏走同一 VK 图层。渲染成本近零,**零新增三方库**。
- 强制字幕(forced)标志随轨枚举透出,选轨 UI 可用。
- 增量工作:解码 + 调色板转换 + 旁路队列对接,约 1~2 天;建议随 M5 并入,
  一次填平两个死穴。DVB/VOB 等其他位图字幕同管道可收编。

### 3.7 与 AI 字幕管线的关系

- AI 字幕继续走 Flutter 渲染(SRT);ASS/PGS overlay 服务片源自带字幕轨。
- 互斥规则:overlay 激活时 Flutter 字幕层隐藏,避免双字幕。

## 4. 里程碑(单人 1.5~2 周)

| 阶段 | 内容 | 估时 |
|---|---|---|
| M1 预编译仓与插件骨架 | 独立仓 CI 出三平台产物;plugins/avox_ass 注册/查表/缺失降级全通(DYNAMIC + STATIC 两模式冒烟) | 2~3 天 |
| M2 解封装 | 字幕轨枚举/选轨/旁路队列;MKV ASS packet 打通到插件(不上屏) | 1 天 |
| M3 渲染闭环 | 插件:ass_render_frame→RGBA canvas(bbox 裁剪)+时钟同步;核心:canvas 变化时上传 + VkBlendLayer(sourceOver) 合成进输出帧;特效样片视觉验收 | 2~3 天 |
| M4 外挂与接口收口 | .ass/.srt 外挂加载;字体回调接 FontMap;shim 两接口收口 | 1~2 天 |
| M5 加固(可选并入 PGS) | seek 清帧重渲染、轨切换、字体 fallback、CPU 兜底回读验证、性能达标;**可选:PGS 位图路径(+1~2 天)** | 1~3 天 |

## 5. 风险

- **FFmpeg 构建裁剪**:确认 `build_*.py` 未裁 subtitle decoder
  (ass/subrip/pgs 均为 LGPL 默认内置,但需核实定制构建项)。
- **MSVC 预编译**:libass 官方支持 MSVC,CMake 选项在预编译仓里过一遍即可;
  NDK 侧社区验证充分;风险集中在独立仓,不污染核心构建。
- **跨 DLL 规范**:严格按机制文档 §6(无 STL 跨界、canvas 归插件堆、C 回调
  注入);STATIC 模式(iOS/Android)无此问题。
- **CPU copy 兜底**:CPU 回读点必须在 overlay 层之后,否则兜底路径无字幕;
  若回读在图层之前,调整图序或明确标注兜底路径无片源字幕。
- **烙帧的取舍**:字幕进 VK 图后,App 无法从 Dart 侧二次移动/缩放(ASS/PGS
  定位本就由片源决定,影响小);用户级字幕偏移/缩放设置需走引擎参数。
- **字体**:FontMap 回调是唯一字体入口;缺字 fallback 观感一般但可接受,
  二期内嵌字体解决。
- **性能**:libass 渲染 1080p 单帧亚毫秒量级;canvas 仅变化时上传(底部横条
  约 2~3MB/次,数秒一次);合成复用现成 blend pass,常态零额外开销。
- **双渲染通道回归**:引擎 OSD(SRT/ASR)与 overlay 的互斥开关需显式测试,
  避免双字幕叠加。
- **降级路径**:插件缺失时 ASS 轨自动跳过——需在选轨 UI 呈现"不可用"而非
  静默无声。

## 6. 验收标准

1. MKV 内封 ASS 动漫样片(含 \pos/\t/\move/卡拉OK)与 mpv 同帧对比:
   允许字体差异,不允许缺层、错位、时间偏移。
2. 外挂 .ass/.srt:加载、切换、seek 后无残留帧、无双字幕。
3. panvox GPU 直通路径:字幕随输出纹理到达 Flutter(无 Dart 叠层、无 FFI
   位图搬运);CPU 兜底路径(`PANVOX_GPU_SHARED=0`)验证回读含字幕(或明确
   标注不支持)。
4. Windows DYNAMIC + Android/iOS STATIC 三种形态编译、加载/降级冒烟通过;
   **删掉/改名 avox_ass 插件文件后播放不崩,ASS 轨自动跳过**。
5. 性能:字幕活跃帧 CPU 增量 < 3ms@1080p;静止段无额外 CPU 与上传。
6. (若并入 PGS)原盘 remux 样片 PGS 轨正常显示,forcing 轨可单独选。
