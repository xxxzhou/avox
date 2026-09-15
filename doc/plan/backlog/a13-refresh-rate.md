# A-13 刷新率自适应

优先级 P1 · 里程碑 M4(桌面先行) · 计划状态:零起点
平台层显示模式协商;移动端无此概念,限定桌面。

## 出口判据

1. 23.976/24Hz 片源在支持的高刷显示器上协商到匹配模式(24/48/72…),judder 可感知改善。
2. vsync 可配置(现硬编码恒开),API 可关。
3. 协商失败/不支持的设备静默保持现状,无副作用。

## 现状(代码落点)

- **全仓无显示模式枚举/切换代码**:无 EnumDisplaySettings/QueryDisplayConfig(Windows)、
  无 CGDisplayMode(macOS)、无 XRandR(Linux)。
- **vsync 硬编码面**:DX11 恒 `Present(1,0)`(`src/avox_windows/dx11/Dx11Window.cpp:132`),
  swapchain RefreshRate 写死 60/1(:214-215);Vulkan 有 FIFO/Mailbox/Immediate 分支
  (`src/avox_vulkan/vulkan/VkWindow.cpp:496-541`)、`vsync` 成员在
  (`VkWindow.hpp:55`)但**核心层无任何 setter,外部不可配置**;
  Apple MetalWindow 是空壳、无 CVDisplayLink。
- **帧步调与显示无关**:媒体时钟 10ms 轮询驱动(`src/avox/player/MediaPlayer.cpp:490`)。

## 任务拆解

- [ ] T1 vsync 透出(先行,一切的前置):DX11 Present 间隔与 VK vsync 成员从
      ISurfaceRender/DX11+VK 渲染层 API 可设;默认行为不变(现值)。
- [ ] T2 显示模式枚举:Windows EnumDisplaySettings/QueryDisplayConfig 列当前与可用模式;
      macOS CGDisplayMode 列表 + 切换;Linux XRandR(桌面三平台,先 Win+mac)。
- [ ] T3 匹配协商:视频 fps → 目标模式选择算法(整数倍优先:24→24/48/72/120;其次最小公倍);
      切换时机策略(仅全屏/由产品开关控制——默认保守,切模式会闪屏);
      切换失败回退原模式。
- [ ] T4 联动口子:协商结果 + 当前模式查询接口暴露(FFI),产品侧(P-17 相邻)做开关;
      display-link 级对齐(替换 10ms 轮询)列为后续深化,不进本计划。
- [ ] T5 用例:60Hz/144Hz 显示器 + 24Hz 片源对比录屏(avox-test 归档,人工走查口径)。

## 验收

- 支持协商的平台:24Hz 片源切到 24/48 模式,对比录屏 panshot 无明显抽动;
  API 关闭时行为与现状完全一致。

## 风险与开放问题

- 切显示模式闪屏/多显示器定位问题,产品策略(默认关、全屏才开)必须先行,别做成立即默认开。
- Wayland 下模式协商受合成器限制,Linux 可能只能做「查询提示」不做切换。
- 与 a11/a9 的渲染管线改动在同一个 M4 交付窗,注意排期串行避免渲染层冲突。
