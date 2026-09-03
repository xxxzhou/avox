# OffscreenCanvas 自适应分辨率设计

## 1. 问题

当前 canvas 缓冲区固定 1280×720（`<canvas width="1280" height="720">`），`transferControlToOffscreen()` 后 offscreen 大小也固定。

**导致的问题：**

| 场景 | canvas 缓冲区 | 视频源 | CSS 显示大小 | 结果 |
|------|-------------|-------|------------|------|
| 4K屏全屏 | 1280×720 | 3840×2160 | 3840×2160 | 糊，3倍放大 |
| 多屏4路 | 1280×720 | 1920×1080 | ~960×540 | 浪费，缓冲区远大于显示区 |
| 视频源小 | 1280×720 | 640×360 | 1280×720 | 浪费，缓冲区远大于视频源 |

**核心矛盾：** offscreen canvas 缓冲区大小应跟随外部 canvas 的实际显示大小，而非写死。

## 2. 目标

### 2.1 外部 canvas 大小同步到 offscreen

外部 canvas 的 CSS 显示大小变化时，offscreen 缓冲区应同步调整，保证：

- 外部大、视频大 → offscreen 跟着大，不糊
- 外部小 → offscreen 跟着小，不浪费 GPU/内存
- offscreen 大、视频小 → 至少不会降低清晰度（插值放大比固定小缓冲区好）

### 2.2 自动缩放（Vulkan enableSizeScale 联动）

引入开关 `autoSizeScale`，启用后结合 Vulkan 的 `enableSizeScale`：

- 当 **外部 canvas 显示大小 < 视频源分辨率** 时，自动计算 scale 值，让 Vulkan 渲染降分辨率
- 降分辨率不影响观感（显示区本身就小，看不到细节），同时降低 GPU 渲染压力和 IO 带宽
- 需要启用 Vulkan 才生效（`enableSizeScale` 是 Vulkan SurfaceRender 的功能）

## 3. 技术分析

### 3.1 offscreen resize 机制

根据 [WebGL 规范](https://github.com/KhronosGroup/WebGL/blob/master/sdk/tests/conformance/offscreencanvas/offscreencanvas-resize.html) 和 [Chromium 实现](https://issues.chromium.org/40534776)：

- `transferControlToOffscreen()` 后，原 canvas 变为 **placeholder**
- **改 placeholder 的 `width/height` 不会影响 offscreen**（单向的）
- **正确做法：在 offscreen 那边改 `offscreen.width / offscreen.height`**
- 改完后，下次 `commit()`（WebGL `present` / WebGPU `submit`）时，placeholder 的 `width/height` 会异步同步

### 3.2 当前渲染路径

```
渲染进程 (avox_player.js)              主进程 (preload.js)
──────────────────────              ──────────────────
canvas.transferControlToOffscreen()
  → postMessage(TRANSFER_CANVAS)  →  JsSurfaceRenderOb.setOffscreen(offscreen)
                                       → new YuvGLRender(offscreen) 或 YuvWebGPURender(offscreen)
                                         → gl = offscreen.getContext('webgl2')
                                         或 gpuContext = offscreen.getContext('webgpu')

  每帧回调:                            onFrame(frame)
                                       → renderer.renderPbo(w, h, stride, format)
                                         → texSubImage2D (WebGL) 或 writeTexture (WebGPU)
                                         → drawArrays / draw (自动 commit)
```

**关键点：** offscreen canvas 被传到主进程后，由 `YuvGLRender` / `YuvWebGPURender` 持有。渲染器用 offscreen 获取 GL/GPU 上下文，每帧渲染时自动 commit。

### 3.3 resize 对渲染器的影响

改 `offscreen.width/height` 后：

| 组件 | 影响 | 是否需处理 |
|------|------|----------|
| WebGL viewport | 不会自动跟随 canvas 大小变化 | **需调 `gl.viewport()`** |
| WebGPU swapchain | `getCurrentTexture()` 大小会自动跟随 | 不需额外处理 |
| PBO / tempBuffer | 帧大小由视频源决定，与 canvas 无关 | 不需额外处理 |
| Texture | 帧大小由视频源决定，与 canvas 无关 | 不需额外处理 |

### 3.4 enableSizeScale 的作用

C++ 层 `SurfaceRender.enableSizeScale(scale)` 的效果：

- 让 Vulkan 管线以 `scale` 倍分辨率渲染（如 0.5x → 1920×1080 → 960×540）
- 渲染结果写回 JS 的 `tempBuffer`，帧的 `width/height/stride` 也会相应缩小
- JS 渲染器（YuvGLRender/YuvWebGPURender）拿到的就是缩小后的帧，直接渲染

所以 `enableSizeScale` 改变的是**输出帧的分辨率**，不影响 canvas 大小。

## 4. 设计方案

### 4.1 整体架构

```
渲染进程 (avox_player.js)                    主进程 (preload.js)
──────────────────────                    ──────────────────

ResizeObserver 监听 canvas 大小变化
  → 计算 newWidth, newHeight
  → postMessage(RESIZE_CANVAS, {         →  JsSurfaceRenderOb.resize(w, h)
      playerId, width, height               →  this.offscreen.width = w
    })                                      →  this.offscreen.height = h
                                            →  WebGL: gl.viewport(0, 0, w, h)
                                            →  通知 SurfaceRender 重新计算 scale
```

### 4.2 渲染进程改动 (avox_player.js)

#### BasePlayer 构造函数

```js
class BasePlayer {
  constructor(playerId, canvas) {
    // ... 现有代码 ...
    this.bindResizeObserver();
  }

  bindResizeObserver() {
    // ResizeObserver 监听 canvas 的 CSS 显示大小变化
    // 注意: 这里监听的是 canvas 的 contentRect, 即 CSS 像素大小
    this.resizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const { width, height } = entry.contentRect;
        if (width > 0 && height > 0) {
          // 用 devicePixelRatio 获取物理像素大小
          const dpr = window.devicePixelRatio || 1;
          const physWidth = Math.round(width * dpr);
          const physHeight = Math.round(height * dpr);
          this.notifyResize(physWidth, physHeight);
        }
      }
    });
    this.resizeObserver.observe(this.canvas);
  }

  notifyResize(width, height) {
    window.postMessage({
      type: 'RESIZE_CANVAS',
      playerId: this.playerId,
      width: width,
      height: height
    }, '*');
  }
}
```

**要点：**
- `ResizeObserver` 监听 canvas 的 CSS 大小变化（窗口 resize、全屏切换、grid 布局变化都能捕获）
- 乘 `devicePixelRatio` 转换为物理像素（4K 屏 DPR=2 时，CSS 1920 → 物理 3840）
- 通过 `postMessage` 通知主进程

#### MediaPlayer 新增 autoSizeScale 开关

```js
class MediaPlayer extends BasePlayer {
  constructor(playerId, canvas) {
    super(playerId, canvas);
    this.autoSizeScale = true; // 默认开启
  }
}
```

### 4.3 主进程改动 (preload.js)

#### JsSurfaceRenderOb 新增 resize 方法

```js
class JsSurfaceRenderOb {
  setOffscreen(offscreen) {
    this.offscreen = offscreen;
    // ... 现有初始化代码 ...
  }

  resize(width, height) {
    if (!this.offscreen) return;
    if (this.offscreen.width === width && this.offscreen.height === height) return;

    this.offscreen.width = width;
    this.offscreen.height = height;

    // WebGL2 需要更新 viewport
    if (this.renderer && this.renderer.gl) {
      this.renderer.gl.viewport(0, 0, width, height);
    }
    // WebGPU 的 getCurrentTexture() 会自动使用新大小, 无需处理

    // 通知 C++ 层重新计算 scale
    this.updateAutoSizeScale();
  }

  // 自动缩放计算 (分级策略, 考虑 aspect-fit)
  updateAutoSizeScale() {
    if (!this.autoSizeScale) return;
    const sourceInfo = this.player.sourceInfo;
    if (!sourceInfo || sourceInfo.videoSize() <= 0) return;

    const videoDesc = sourceInfo.getVideoDesc(0);
    const videoW = videoDesc.desc.width;
    const videoH = videoDesc.desc.height;
    const canvasW = this.offscreen.width;
    const canvasH = this.offscreen.height;

    if (videoW <= 0 || videoH <= 0 || canvasW <= 0 || canvasH <= 0) return;

    // 计算 aspect-fit: 视频在 canvas 中实际显示区域
    // 视频保持原始宽高比, fit 到 canvas 内 (类似 CSS object-fit: contain)
    const videoAspect = videoW / videoH;
    const canvasAspect = canvasW / canvasH;
    let displayW, displayH;
    if (canvasAspect > videoAspect) {
      // canvas 更宽, 视频按高度适配, 两侧有黑边
      displayH = canvasH;
      displayW = canvasH * videoAspect;
    } else {
      // canvas 更窄/同比例, 视频按宽度适配, 上下有黑边
      displayW = canvasW;
      displayH = canvasW / videoAspect;
    }

    // 用实际显示区域与 video 的最小边比
    const ratio = Math.min(displayW / videoW, displayH / videoH);

    // 分级: >1/2 不缩放, ≤1/2且>1/4 → 0.5x, ≤1/4 → 0.25x
    let scale = 1.0;
    if (ratio <= 0.25) {
      scale = 0.25;
    } else if (ratio <= 0.5) {
      scale = 0.5;
    }

    const render = this.player.winRender;
    if (scale < 1.0 && render) {
      render.enableSizeScale(scale);
    } else if (render) {
      render.disableSizeChange();
    }
  }
}
```

#### 消息处理新增 RESIZE_CANVAS

```js
// preload.js 消息监听中新增
window.addEventListener('message', (event) => {
  if (event.data.type === 'RESIZE_CANVAS') {
    const { playerId, width, height } = event.data;
    const player = playerInstances.get(playerId);
    if (player && player.jsWinOb) {
      player.jsWinOb.resize(width, height);
    }
  }
});
```

### 4.4 autoSizeScale 联动逻辑

#### 分级缩放策略

不能简单按 `canvas/video` 比例设 scale。原因：

- `enableSizeScale(0.5)` 会让 Vulkan 输出 0.5x 分辨率的帧，**丢掉的像素信息无法恢复**
- 即使 canvas 只比 video 小 2 倍（如 video 1920、canvas 960），直接 scale=0.5 也会明显降低清晰度
- 在 canvas 很小的情况下，人眼已经分辨不出细节，此时降 scale 才不影响观感

因此采用**分级降 scale**，只在 canvas 足够小时才触发：

| canvas / video 最小边比 | scale | 说明 |
|------------------------|-------|------|
| > 1/2 | 不缩放 (1.0) | canvas 还够大，保留原始分辨率由 GL 插值缩放 |
| ≤ 1/2 且 > 1/4 | 0.5 | canvas 约 video 的一半，细节已不可辨 |
| ≤ 1/4 | 0.25 | canvas 非常小，大幅降分辨率 |

**示例：**

| video 分辨率 | canvas 大小 | aspect-fit 显示区域 | ratio | auto scale | 说明 |
|-------------|------------|-------------------|-------|-----------|------|
| 1920×1080 | 1920×1080 | 1920×1080 | 1.0 | 1.0 | 原始分辨率 |
| 1920×1080 | 960×540 | 960×540 | 0.5 | 1.0 | 不缩放，GL 插值保留细节 |
| 1920×1080 | 480×270 | 480×270 | 0.25 | 0.5 | Vulkan 渲染 960×540 |
| 1920×1080 | 240×135 | 240×135 | 0.125 | 0.25 | Vulkan 渲染 480×270 |
| 3840×2160 | 1920×1080 | 1920×1080 | 0.5 | 1.0 | 不缩放，4K→1080 由 GL 插值 |
| 3840×2160 | 960×540 | 960×540 | 0.25 | 0.5 | Vulkan 渲染 1920×1080 |
| 1920×1080 | 480×720 (竖屏) | 480×270 | 0.25 | 0.5 | 竖屏 canvas，视频按宽度适配，两侧黑边 |
| 1920×1080 | 720×480 (扁屏) | 720×405 | 0.375 | 1.0 | 扁 canvas，视频按宽度适配，上下黑边，ratio>0.25 不缩放 |
| 1080×1920 (竖视频) | 480×720 (竖屏) | 405×720 | 0.375 | 1.0 | 竖视频竖屏，按高度适配，两侧黑边，不缩放 |

**为什么 > 1/2 不缩放：**
- canvas 显示 960×540 时，每个 CSS 像素对应 video 约 2 个像素，GL 线性采样插值效果很好
- 如果 scale=0.5，Vulkan 只输出 960×540 的帧，相当于用低分辨率"硬看"，运动场景会有明显模糊
- 保留原始分辨率让 GL 插值，画质更好，代价只是多传一些像素（但 Vulkan → JS 的数据量确实更大）

**为什么 ≤ 1/2 可以缩放：**
- canvas 只有 480×270 时，每个 CSS 像素对应 video 约 4 个像素，人眼已经无法区分细节
- 此时 scale=0.5 输出 960×540，对 480×270 的 canvas 来说仍然是超采样，观感无损
- 同时减少 Vulkan 渲染和数据传输量

#### 流程

```
canvas 大小变化 (ResizeObserver)
  │
  ├─ 通知主进程 resize(offscreen.width/height)
  │     └─ 更新 WebGL viewport
  │
  └─ 如果 autoSizeScale 开启 && 启用了 Vulkan:
        │
        ├─ ratio = min(canvasW/videoW, canvasH/videoH)
        │
        ├─ ratio > 1/2  → disableSizeChange() (不缩放)
        ├─ ratio ≤ 1/2 且 > 1/4 → enableSizeScale(0.5)
        ├─ ratio ≤ 1/4 → enableSizeScale(0.25)
        │
        └─ 视频源未就绪 → 等待 onReady 后再计算
```

### 4.5 时序

```
1. 页面加载 → canvas 创建 → transferControlToOffscreen → 渲染器初始化
2. ResizeObserver 首次触发 → 发送 RESIZE_CANVAS → offscreen resize + viewport 更新
3. 播放开始 → onReady → 获取视频源分辨率 → 计算 autoSizeScale
4. 用户操作(全屏/多屏切换/窗口resize) → ResizeObserver → RESIZE_CANVAS → 重新计算
5. 播放器销毁 → disconnect ResizeObserver
```

### 4.6 player_test.js 多屏联动的改动

现有的多屏缩放逻辑（`updateMultiScreenScale`）改为依赖 autoSizeScale 自动计算，不再手动指定 scale 值：

- 双击全屏 → canvas 变大 → ResizeObserver → offscreen resize → autoSizeScale 重新计算（scale=1，因为 canvas ≥ video）
- 恢复多屏 → canvas 变小 → ResizeObserver → offscreen resize → autoSizeScale 重新计算（scale<1，因为 canvas < video）

手动 `enableSizeScale` 按钮仍保留。手动和自动互不操作，谁最后调了谁生效：

- 手动调用 `enableSizeScale()` / `disableSizeChange()` 不影响 `autoSizeScale` 的开关状态
- `autoSizeScale` 计算出新的 scale 后直接调用 `enableSizeScale` / `disableSizeChange`，覆盖手动值
- 用户可通过 `setAutoSizeScale(true/false)` 控制自动模式开关

## 5. 注意事项

- **devicePixelRatio**: 高 DPI 屏幕必须乘 DPR，否则 canvas 缓冲区仍然是 CSS 像素大小，在 4K+DPR2 屏上仍会糊
- **WebGL 上下文数量限制**: 约 16 个，超出会切 WebGPU。resize 不影响上下文数量
- **resize 频率**: 分级策略天然防抖——scale 只有 1.0/0.5/0.25 三档，ratio 在同一档内变化无需任何操作。只需记录当前档位，跨档时才触发 resize。例如 ratio 从 0.3→0.35，scale 都是 0.5，不触发。ratio 从 0.51→0.49 跨档才触发。ResizeObserver 的高频回调自然被过滤掉
- **onReady 时序**: autoSizeScale 需要视频源分辨率，在 onReady 之前无法计算。首次 onReady 时应主动触发一次计算
- **enableSizeScale 粒度**: C++ 层 `sizeScale` 是 `float`，支持任意浮点值（直接 `width * scale, height * scale`），不限于离散值。当前设计用 0.5/0.25 分档已经足够，如果将来需要更细粒度可以扩展
