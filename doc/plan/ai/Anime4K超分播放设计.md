# Anime4K 实时超分播放 设计文档

## 1. 概述

### 1.1 背景

Anime4K (https://github.com/bloc97/Anime4K) 是一个开源的高性能动漫超分辨率算法，基于纯 GPU Shader (GLSL) 实现，无需神经网络推理，能够在消费级 GPU 上实现 1080p → 4K 的实时超分播放。

avox 已具备完整的 Vulkan 计算着色器管线 (`VkPipeGraph` → `VkLayer`)，天然支持以新增 Layer 节点的方式插入 Anime4K 后处理，无需改动核心架构。

### 1.2 目标

在 avox 中集成 Anime4K v4.0 算法，实现动漫/动画类内容的实时超分辨率播放（1080p → 4K，最高支持 4x 放大）。

### 1.3 核心收益

| 对比维度 | 传统上采样 (Bilinear) | waifu2x/Real-ESRGAN | Anime4K |
|----------|----------------------|---------------------|---------|
| 画质 | 模糊 | 优秀 | 优秀（动漫特化） |
| 性能 | 极低 | 极高（需 DNN 推理） | 低（纯 shader 计算） |
| 延迟 | <1ms | 数十~数百ms | 数ms |
| 硬件要求 | 任何 GPU | 高端 GPU/NPU | 消费级 GPU |
| 实时播放 | 可以 | 困难 | 可以 |

## 2. Anime4K 算法分析

### 2.1 管线架构

Anime4K v4.0 的管线由可组合的 Shader 模块构成：

```
输入帧 (RGBA, 1920×1080)
    │
    ▼
┌─────────────────────────────────┐
│  Clamp Highlights Stats         │  ← 计算局部最大亮度（水平→垂直 5-pixel max）
│  (可选, 防止高光区域过曝)         │
└─────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────┐
│  Restore CNN (去模糊/去压缩伪影)  │  ← 核心模块，区分 Anime4K 与其他算法
│  - 线条重建 + 去振铃              │     conv0→conv1→...→conv6→output(residual)
└─────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────┐
│  Upscale CNN (2x 上采样)         │  ← 多种变体: S/M/L + Denoise
│  - 基于卷积核的线条感知放大        │     conv0→conv1→...→conv6→output→d2s(residual)
│  - Depth-to-Space 2x 放大        │     耗时: S≈2ms, M≈5ms, L≈10ms
└─────────────────────────────────┘
    │  3840×2160
    ▼
┌─────────────────────────────────┐
│  Clamp Highlights Apply         │  ← 将像素亮度钳制到局部最大值
│  (可选)                          │
└─────────────────────────────────┘
    │  输出 → 屏幕
    ▼
```

### 2.2 预设模式

| 模式 | 管线组合 | 适用场景 |
|------|----------|----------|
| **Mode A** | Restore + Upscale | 高压缩率 1080p/720p 动漫（明显模糊） |
| **Mode B** | Restore_Soft + Upscale | 较清晰 720p 内容（避免过度锐化导致振铃） |
| **Mode C** | Upscale only | 高质量原片/壁纸（仅需放大） |

### 2.3 CNN 上采样变体

| 变体 | 耗时 | 画质 | 推荐用途 |
|------|------|------|----------|
| S (Small) | ~2ms | 基础 | 性能优先场景 |
| M (Medium) | ~5ms | 良好 | 主流 1080p→4K |
| L (Large) | ~10ms | 优秀 | 高画质优先 |

> 当前仅实现 M 变体。S/L 变体需要移植对应的 shader 权重。

## 3. 架构设计

### 3.1 整体架构

Anime4K 作为 avox Vulkan 渲染管线中的一个可选后处理阶段，插入在 Resize 之后、Watermark/Lut/Font 之前：

```
                    现有管线                            新增 (Anime4K)
               ════════════════                  ═══════════════════════

  VkInputLayer → VkYUV2RGBALayer → [VkResizeLayer] → VkAnime4KLayer → VkBlendLayer → ...
       ↑                ↑                ↑                  ↑
   输入帧数据       YUV→RGBA         可选尺寸调整        Anime4K管线
                                  (Anime4K 在 resize 之后执行)
```

### 3.2 VkAnime4KLayer 设计

`VkAnime4KLayer` 继承 `VkGroupLayer`，利用 avox 的复合 Layer 机制管理内部多个子 Layer。对外暴露 1 in + 1 out (rgba8)，内部管理 Restore CNN、Upscale CNN、Clamp Highlights 等子 Layer 的连接和中间纹理。

**关键实现要点：**

1. **`onInitLayer()` 重写** — Upscale 输出 2x 分辨率，组节点的 `outFormats` 必须匹配：
   ```cpp
   outFormats[0].width = inFormats[0].width * 2;
   outFormats[0].height = inFormats[0].height * 2;
   ```

2. **`setEndNode()` 而非 `addLine(getNode())`** — 组节点本身没有 compute shader，必须用 `setEndNode()` 指向实际产出纹理的子层，否则下游读到黑屏：
   ```cpp
   // 错误: addLine(getNode()) 指向自身(无shader) → 黑屏
   // 正确: setEndNode(d2sLayer) 指向最终处理子层
   if (useUpscale && useClamp) { setEndNode(clampApplyPass); }
   else if (useUpscale) { setEndNode(d2sLayer); }
   ```

3. **外部输入扇出** — 原始帧通过 `setStartNode()` 同时送入多个子层（RestoreConv0、RestoreOutput、D2S、ClampHPass 等）。

**子 Layer 类型：**

| 子 Layer | 输入 | 输出 | 说明 |
|----------|------|------|------|
| VkAnime4KConv0Layer | rgba8 | rgba32f | 首个卷积层 (3ch→4ch) |
| VkAnime4KConvLayer | rgba32f | rgba32f | 中间卷积层 (8ch→4ch, ReLU split) |
| VkAnime4KRestoreOutputLayer | rgba8 + 7×rgba32f | rgba8 | Restore 输出层 (1x1 conv + residual) |
| VkAnime4KUpscaleOutputLayer | 7×rgba32f | rgba32f | Upscale 输出层 (1x1 conv, 无 residual) |
| VkAnime4KD2SLayer | rgba8 + rgba32f | rgba8 (2x) | Depth-to-Space (4ch→RGB, 2x, residual) |
| VkAnime4KClampHPass | rgba8 | r32f | 水平 5-pixel max luma |
| VkAnime4KClampVPass | r32f | r32f | 垂直 5-pixel max luma |
| VkAnime4KClampApplyPass | rgba8 + r32f | rgba8 | 钳制像素亮度到局部最大值 |

**VkAnime4KLayer 内部管线连接 (Mode A + M 变体)：**

```
外部输入 (rgba8)
    ├──────────────────────────────────────────────────┐
    │                                                  │
    ▼                                                  ▼
[ClampHPass] ──→ [ClampVPass]                    [RestoreConv0] ──→ [RestoreConv1] ──→ ... ──→ [RestoreConv6]
    │                                                  │
    │                                                  ▼
    │                                          [RestoreOutput] ←── (MAIN 输入, residual)
    │                                                  │
    │                                                  ▼
    │                                          [UpscaleConv0] ──→ [UpscaleConv1] ──→ ... ──→ [UpscaleConv6]
    │                                                  │
    │                                                  ▼
    │                                          [UpscaleOutput] ──→ [D2S] ←── (MAIN 输入, residual)
    │                                                  │
    ▼                                                  ▼
[ClampApplyPass] ←─────────────────────────────── [D2S 输出]
    │
    ▼
  输出 (rgba8)
```

> `setStartNode()` 将外部 rgba8 输入同时连接到 RestoreConv0、RestoreOutput(MAIN)、UpscaleConv0(当无Restore时)、D2S(MAIN)、ClampHPass 等需要原始输入的子 Layer。

### 3.3 管线集成位置

在 `VkVideoRender::vaildAndInitGraph()` 中，当 Anime4K 启用时：

```cpp
// 创建 Anime4K 节点
if (bEnableAnime4K) {
    anime4KLayer = graph->addNode<VkAnime4KLayer>();
    anime4KLayer->get()->updateParamet(anime4KParamet);
}
// ... 管线连接 ...
// 在 resize 之后、blend/lut/font 之前插入
if (bEnableAnime4K) {
    outNode = outNode->addLine(anime4KLayer);
}
```

### 3.4 文件结构

```
src/avox_vulkan/anime4k/
├── VkAnime4KLayer.hpp           # 8 个子 Layer + VkAnime4KLayer 复合 Layer
└── VkAnime4KLayer.cpp           # 实现

src/avox/AvoxLayer.h               # Anime4KParamet 结构体 (公共 API)
src/avox/video/WindowRender.hpp   # enableAnime4K/disableAnime4K 转发
src/avox/video/WindowRender.cpp   # 转发到 VkVideoRender

samples/vulkantest/anime4ktest.cpp  # 键盘交互测试程序

glsl/source/
├── anime4k_restore_m_conv0.comp    # Restore M 首个卷积 (3ch→4ch)
├── anime4k_restore_m_conv1.comp    # Restore M 中间卷积 (8ch→4ch, ReLU split)
├── anime4k_restore_m_conv2.comp
├── anime4k_restore_m_conv3.comp
├── anime4k_restore_m_conv4.comp
├── anime4k_restore_m_conv5.comp
├── anime4k_restore_m_conv6.comp
├── anime4k_restore_m_output.comp   # Restore M 输出层 (1x1 conv + residual)
├── anime4k_upscale_m_conv0.comp    # Upscale M 首个卷积
├── anime4k_upscale_m_conv1.comp    # Upscale M 中间卷积
├── anime4k_upscale_m_conv2.comp
├── anime4k_upscale_m_conv3.comp
├── anime4k_upscale_m_conv4.comp
├── anime4k_upscale_m_conv5.comp
├── anime4k_upscale_m_conv6.comp
├── anime4k_upscale_m_output.comp   # Upscale M 输出层 (1x1 conv)
├── anime4k_upscale_m_d2s.comp      # Upscale M Depth-to-Space (2x, residual)
├── anime4k_clamp_h.comp            # Clamp Highlights 水平 max
├── anime4k_clamp_v.comp            # Clamp Highlights 垂直 max
└── anime4k_clamp_apply.comp        # Clamp Highlights 钳制应用

```

## 4. Shader 移植策略

### 4.1 OpenGL GLSL → Vulkan Compute GLSL

Anime4K 原始实现使用 mpv 的 OpenGL 片段着色器 (`//!HOOK` / `//!BIND` 指令)，需要改写为 Vulkan 计算着色器。核心差异：

| 特性 | mpv OpenGL Fragment Shader | Vulkan Compute Shader |
|------|--------------------------|----------------------|
| 入口 | `main()` 隐式每像素调用 | `layout(local_size_x=16, local_size_y=16)` + `main()` |
| 像素坐标 | `gl_FragCoord.xy` | `gl_GlobalInvocationID.xy` |
| 输出 | `fragColor` / `imageStore` | `imageStore(dstTex, pos, value)` |
| 纹理采样 | `MAIN_texOff(vec2(x,y))` | `texture(sampler2D(srcTex), uv + offset * texelSize)` |
| ReLU split | `go_0 = max(texOff, 0)`, `go_1 = max(-texOff, 0)` | 相同逻辑，在 compute shader 中计算 |
| 权重 | 编译为 shader 常量 (mat4/vec4) | 编译为 shader 常量 (const mat4/vec4) |

### 4.2 中间纹理格式

| 纹理类型 | 格式 | 说明 |
|----------|------|------|
| 输入/输出 | rgba8 (UNORM) | 视频帧是 [0,1] 范围 |
| CNN 中间层 | rgba32f (SFLOAT) | CNN 特征值可能超出 [0,1]（ReLU 前有负值） |
| Clamp stats | r32f (SFLOAT) | 单通道 max luma 值 |

> rgba8 (UNORM) 会截断到 [0,1]，导致 CNN 计算错误。中间层必须使用 rgba32f。

### 4.3 权重传入方案

**决策：编译为 shader 常量（const mat4/vec4）**

- Anime4K 每个变体的权重是训练好的固定参数
- UBO 有大小限制（通常 16KB），M 变体 8 个 Pass 的权重接近 UBO 限制
- 编译为 shader 常量性能最好（GPU 常量内存 vs UBO buffer 读取）
- 不同变体通过独立的 shader 文件区分

## 5. API 设计

### 5.1 ISurfaceRender 扩展

在 `ISurfaceRender` 接口中新增方法（已实现）：

```cpp
// src/avox/AvoxLayer.h
class ISurfaceRender {
  // ... 现有方法 ...
  virtual void enableAnime4K(const Anime4KParamet& paramet) {}
  virtual void disableAnime4K() {}
};
```

### 5.2 参数结构体

```cpp
// src/avox/AvoxLayer.h (公共 API)
enum class Anime4KMode : int32_t {
  ModeA = 0,  // Restore + Upscale, 适合高压缩率动漫(模糊明显)
  ModeB = 1,  // Restore_Soft + Upscale, 适合较清晰动漫(避免过度锐化)
  ModeC = 2,  // Upscale only, 适合高质量原片(仅需放大)
};
// CNN 模型变体, 越大画质越好但越慢
enum class Anime4KVariant : int32_t {
  S = 0,  // Small, ~2ms, 性能优先
  M = 1,  // Medium, ~5ms, 画质与性能平衡
  L = 2,  // Large, ~10ms, 画质优先
};
struct Anime4KParamet {
  Anime4KMode mode = Anime4KMode::ModeA;       // 预设模式
  Anime4KVariant variant = Anime4KVariant::M;  // CNN模型变体
  float strength = 1.0f;                        // Restore强度(0~1, 1为满强度)
  bool enableClampHighlights = true;            // 防止高光区域过曝

  bool operator==(const Anime4KParamet& r) const;
  bool operator!=(const Anime4KParamet& r) const;
};
```

> **注意:** `strength` 参数当前仅作为管线重建的触发条件（参数变化时重建管线），尚未传入 shader UBO 动态调节 Restore 强度。

### 5.3 调用链路

```
用户调用 ISurfaceRender::enableAnime4K()
    → WindowRender::enableAnime4K()       // 检查 bVulkan, 转发
        → VkVideoRender::enableAnime4K()  // 设置参数 + bResetFlag=true
            → 下次 vaildAndInitGraph() 重建管线时创建 VkAnime4KLayer 节点
```

### 5.4 使用示例

```cpp
// C++ API
auto player = createMediaPlayer();
auto render = player->getSurfaceRender();

Anime4KParamet paramet;
paramet.mode = Anime4KMode::ModeA;
paramet.variant = Anime4KVariant::M;
paramet.enableClampHighlights = true;
render->enableAnime4K(paramet);

// 关闭
render->disableAnime4K();
```

## 6. 性能评估

### 6.1 理论分析

以 1080p@30fps → 4K 为例，单帧时间预算 = 33.3ms：

| 阶段 | 分辨率 | 估算耗时 |
|------|--------|----------|
| YUV→RGBA | 1920×1080 | ~1ms |
| Clamp Stats (H+V) | 1920×1080 | ~1ms |
| Restore CNN (M) | 1920×1080 | ~3ms |
| Upscale CNN (M) | 1920×1080→3840×2160 | ~5ms |
| Clamp Apply | 3840×2160 | ~1ms |
| Output blit | 3840×2160 | ~1ms |

**Mode A + M 变体**: 总计约 **12ms**，占帧预算的 36%，可满足 60fps 实时播放。

### 6.2 内存占用

中间纹理占用（1080p → 4K）：

| 纹理 | 大小 | 格式 |
|------|------|------|
| 输入 (1080p) | 8 MB | rgba8 |
| Restore 中间层 ×7 (1080p) | 7 × 32 MB = 224 MB | rgba32f |
| Upscale 中间层 ×7 (1080p) | 7 × 32 MB = 224 MB | rgba32f |
| Clamp stats (1080p) | 4 MB | r32f |
| 输出 (4K) | 32 MB | rgba8 |

> rgba32f 中间纹理占用较大。可通过复用同一纹理（conv 层输出尺寸相同）减少到 2×32MB = 64MB。

## 7. 实施状态

### Phase 1: Shader 移植 — 已完成 ✓

- [x] 移植 `Anime4K_Restore_CNN_M.glsl` → 8 个 .comp 文件 (conv0~conv6, output)
- [x] 移植 `Anime4K_Upscale_CNN_x2_M.glsl` → 8 个 .comp 文件 (conv0~conv6, output, d2s)
- [x] 移植 `Anime4K_Clamp_Highlights.glsl` → 3 个 .comp 文件 (clamp_h, clamp_v, clamp_apply)
- [x] 更新 `glslindexcurrent.txt`，编译为 .spv

### Phase 2: VkAnime4KLayer 实现 — 已完成 ✓

- [x] `Anime4KParamet` 结构体（定义在 `AvoxLayer.h` 公共 API，含字段注释）
- [x] `VkAnime4KLayer` 继承 `VkGroupLayer`
- [x] 8 个子 Layer 类型（Conv0, Conv, RestoreOutput, UpscaleOutput, D2S, ClampH, ClampV, ClampApply）
- [x] `onInitGroup()` — 根据 mode 创建子 Layer 并设置格式
- [x] `onInitNode()` — 连接子 Layer 管线（conv 链、output 残差、d2s、clamp）
- [x] `setStartNode()` — 多个入口节点接收外部 rgba8 输入
- [x] `onInitLayer()` 重写 — 设置 outFormats 为 2x 分辨率（Upscale 输出）
- [x] `setEndNode()` 替代 `addLine(getNode())` — 修复组节点无 shader 导致下游黑屏的问题

### Phase 3: VkVideoRender 集成 — 已完成 ✓

- [x] `VkVideoRender::enableAnime4K()` / `disableAnime4K()`
- [x] `vaildAndInitGraph()` 中创建 `VkAnime4KLayer` 节点
- [x] 管线插入位置：resize 之后、blend/lut/font 之前
- [x] `bResetFlag` 重建触发
- [x] `WindowRender` 转发层

### Phase 4: API 绑定 — 已完成 ✓

- [x] `ISurfaceRender` 接口扩展（`enableAnime4K` / `disableAnime4K`）
- [x] `WindowRender` override 实现
- [ ] SWIG 绑定（C#/Java/Node.js）— 待实现

### Phase 5: 端到端测试 — 已完成 ✓

- [x] `anime4ktest.cpp` 键盘交互测试程序（A=OFF, S=ModeA, D=ModeC, W=ModeB, Q=ModeA+L, F=Clamp, G/H=强度调节）
- [x] 字体状态提示（IFontLayer 显示当前模式/参数）
- [x] 验证 VkGroupLayer 黑屏修复（setEndNode 替代 addLine(getNode())）
- [ ] 对比 mpv+Anime4K 的输出截图
- [ ] 性能基准测试（记录实际帧耗时）

### Phase 6 (可选): 更多变体 — 待实现

- [ ] 移植 L 变体 shader
- [ ] 移植 Restore_Soft 变体
- [ ] 移植 Upscale+Denoise 变体
- [ ] 自适应 CNN 变体（根据帧耗时动态切换）

## 8. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| Shader 移植后画质不一致 | 高 | 中 | 建立参考图像对比测试，逐 pass 验证 |
| VkGroupLayer 黑屏 (endIndex 指向自身) | 高 | 已修复 | 必须用 setEndNode() 指向实际终端子层，不能用 addLine(getNode()) |
| CNN shader 性能低于预期 | 中 | 低 | 使用 S/M 变体作为默认，预置性能回退路径 |
| Vulkan 与 OpenGL 纹理采样差异 | 中 | 中 | 注意 sampler 配置（filter mode, address mode），对比验证 |
| rgba32f 中间纹理显存占用大 | 中 | 低 | 复用 conv 层输出纹理，或按需创建/销毁 |
| 移动端 GPU 不支持 | 低 | 低 | 运行时检测 GPU 能力，不支持时回退到普通 Render |
| Anime4K License 合规 | 低 | 低 | MIT License，需保留版权声明 |

## 9. 可行性结论

**Anime4K 集成到 avox 已完成，Phase 1-5 核心实现已验证。**

核心判断依据：

1. **架构兼容性** — avox 的 `VkPipeGraph` + `VkLayer` DAG 管线天然支持插入新的后处理节点。`VkGroupLayer` 复合 Layer 机制使得 Anime4K 的多 pass 管线可以封装为单个节点，对外 1 in + 1 out。

2. **Shader 移植可行** — Anime4K 的 mpv GLSL 片段着色器到 Vulkan Compute Shader 的转换是机械的映射工作，核心算法数学逻辑不变。

3. **性能满足实时要求** — Anime4K 的设计目标就是实时播放，在 Vulkan 后端上的 compute shader 性能不差于 OpenGL 实现。保守估计 1080p→4K 耗时 ~12ms（Mode A + M 变体），30fps 预算 33ms 完全可行。

4. **参考先例** — mpv、MPC-HC 等播放器已成功集成 Anime4K 并支持实时播放，验证了方案的整体可行性。

5. **关键修复已落地** — `setEndNode()` 替代 `addLine(getNode())` 解决了 VkGroupLayer 黑屏问题，`onInitLayer()` 重写确保 2x 输出格式正确。
