# play — 播放音视频

Phase 1 — 核心播放 + 交互式 OSD 验证模式。

## 用法

```bash
# 播放本地文件 / 直播流
avox_cli play -i video.mp4
avox_cli play -i rtsp://192.168.1.100/live

# 硬解 / 低延迟
avox_cli play -i video.mp4 -hard
avox_cli play -i rtsp://... -lowlatency -delay 100

# 离屏渲染 (无窗口, 配合 -screenshot 取帧)
avox_cli play -i video.mp4 -offscreen -screenshot ./shots -shot-interval 1000
```

## 选项 (节选)

| 选项 | 类型 | 说明 | 默认 |
|------|------|------|------|
| `-i` | String | 输入源 (必填) | - |
| `-hard` | Bool | 硬解 (默认软解) | 关 |
| `-io` | String | IO 方案: auto/ffmpeg/zlmediakit | auto |
| `-offscreen` | Bool | 离屏渲染 | 关 |
| `-speed` | Number | 播放速度 | 1.0 |
| `-lowlatency` | Bool | 低延迟模式 | 关 |
| `-screenshot` | String | 截图目录 | - |

## 对应 SDK API

- `createMediaPlayer()` → `IMediaPlayer*`
- `IMediaPlayer`: `open/close/pause/resume/seek/speed`, `getState/getPosition/getDuration/getFps`, `getOption`, `getSurfaceRender`, `getMuxer(bTranscode)`

---

## 交互式 OSD 验证模式 (设计)

### 目标

窗口模式下 (默认 `setSurface(nullptr)`), play 不只是「播完就退出」。借鉴 `vkwindowtest.cpp` / `vkfonttest.cpp`,
让用户用键盘实时操作播放器, 并在画面上用字体叠加 OSD (speed / 时间 / 状态 / 录制),
用于**人工验证 `IMediaPlayer` / `ISurfaceRender` / `IMediaMuxer`** 三个核心接口。
这样 `avox_cli play` 即成为手动回归测试台。

### 现状基础

- `CmdPlay.cpp` 已在主线程跑 Win32 消息泵 (`PeekMessage` + `TranslateMessage/DispatchMessage`),
  窗口模式必须如此否则窗口卡死。**键盘只需在同一个 PeekMessage 循环里加 `WM_KEYDOWN` 分支**, 无需新线程。
- 画面文字走 `avox_freetype` 模块的 `IFontLayer` (实现 `VkFontLayer`, 一个 Vulkan compute 层,
  在渲染图链最末合成, 文字永远盖在最终帧之上)。多块文字共享一张 canvas、一次 dispatch, 渲染 N 块 ≈ 渲染 1 块。

### 文字叠加 API (`src/avox_freetype/FreetypeExport.h`)

```cpp
struct FontLayout { Alignment alignment; float x, y; float width, height; };
// x,y 是锚点 (0~1 归一化); width/height 是最大宽高比, 超过自动换行
class IFontLayer {
  virtual bool setFont(const char* name, int32_t size) = 0;   // 触发图重建, 别每帧调
  virtual void setColor(float r,float g,float b,float opacity=0)=0; // 全局, 所有块共享
  virtual void setScale(float scale) = 0;                      // canvas 分辨率, 触发图重建
  virtual FontLayout getLayout(int32_t index) = 0;            // 越界自动 resize
  virtual void updateLayout(int32_t index, const FontLayout&) = 0; // 触发图重建
  virtual void setTextLayout(int32_t index) = 0;              // 选当前块
  virtual void drawText(const char* text) = 0;                // 画到当前块 (每帧便宜)
};
extern "C" IFontLayer* enableRenderFont(ISurfaceRender* render);
extern "C" void disableRenderFont(ISurfaceRender* render);
```

要点: `opacity` 语义反的 (0=不透明, 1=全透); `setFont/setScale/updateLayout` 会 `resetGraph`, **只有 `drawText` 是每帧廉价**, 所以排版只配一次, 每帧只刷文本。

### OSD 分区 (启动时配一次 layout, 之后只 drawText)

| index | 位置 | 内容 | 刷新 |
|-------|------|------|------|
| 0 | 左上 | 操作提示 `[Space]暂停 [←→]±10s [A/S/D]0.25/1/4x [P]截图 [R]录制 [Q]退出` | 静态 |
| 1 | 左下 | `state | fps | speed` + 录制状态 | ~250ms |
| 2 | 右下 | `当前时间 / 总时长` (取自 `getPosition()-getStartTime()` 与 `getDuration()`) | ~250ms |

时间格式化: `snprintf("%02d:%02d:%02d / %02d:%02d:%02d  %.2fx", ...)`, 直播源 `getDuration()<=0` 时只显示当前位置。
所有 `IFontLayer` 调用包在 `#ifdef AVOX_ENABLE_FREETYPE` 里, 关闭时功能优雅降级 (只剩键盘控制, 无 OSD)。

### 键盘映射 → 待验证接口

| 键 | 动作 | 验证对象 |
|----|------|----------|
| `Space` | `pause()` / `resume()` 切换 | `IMediaPlayer` |
| `←` / `→` | `seek(getPosition() ∓ 10000)` | `IMediaPlayer` (直播源 seek 行为) |
| `A` / `S` / `D` / `F` | `speed(0.25/1/4/0.5)` | `IMediaPlayer` 变速 |
| `P` | `getSurfaceRender()->screenShot()` + `saveImagePath()` | `ISurfaceRender` |
| `B` | 切 `enableWatermark` / `enableAnime4K` / `updateBrightness` 等 | `ISurfaceRender` 后处理 |
| `R` | 启停录制: `getMuxer(true)` → `setMuxerType/open/close` | `IMediaMuxer` |
| `Q` / `Esc` | `close()` + 退出循环 | 生命周期 |

录制 (`R`): 默认转码 muxer `mp->getMuxer(true)`, `setMuxerType(MuxerType::ffmpeg)`,
`setVideoCodec/setAudioCodec/setHardEncode`, 输出到 `./record_<时间戳>.mp4`;
OSD index 1 同步显示 `[REC]` 状态。复用 `IImageBuffer`/`saveImagePath` 的截图模式做帧采样验证。

### 实现要点

1. `open()` 之后 `enableRenderFont(getSurfaceRender())`, 配 3 个 layout (照搬 `vkfonttest.cpp` 的 `initFontLayouts`)。
2. 现有 PeekMessage 循环内加 `WM_KEYDOWN` switch (与 `vkfonttest.cpp:168` 同构), 50ms sleep 响应足够灵敏。
3. 循环里维护 `lastOsdUpdate`, 每 250ms 重新 `setTextLayout(1/2); drawText(...)`, 不必每帧刷。
4. `-offscreen` / 管道非交互模式跳过 OSD 与键盘, 保持原有「播完即退」。
5. Ctrl+C 已由 Shell 在命令执行前摘掉处理器交还给 play, play 现有的 SIGINT→`gRunning=false` 仍生效。

### 注意

- 颜色全局: 多块文字无法各自设色; 要分色只能开多个 `IFontLayer` 或接受统一色。
- `setFont/setScale/updateLayout` 触发图重建, 切字体/缩放有可感卡顿 (单次按键可接受), 切勿放进 250ms 刷新路径。
- 文字渲染依赖 Vulkan 后端; DX11/无 GPU 路径下 `enableRenderFont` 返回 nullptr, 需判空。
