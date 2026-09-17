# VR 播放支持计划

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -

**目标：** 平面屏上播放 VR 内容（fisheye180 / equirect 立体片源）：开箱即用自动识别格式，拖动转视角、滚轮缩放，附红蓝 3D 输出。普通 2D 视频零影响（不启用 VR 就不挂 pass）。

**定位与边界：** 只做播放器平面观看，不做头显。图像处理只走 Vulkan 一条路径，不做 DX11/Metal 原生移植。

**技术定界：** 鱼眼视频为固定机位拍摄，画面只有方向信息、没有位置信息，"走进场景"不可行；一张鱼眼图即含完整半球内容，拖动交互只需单眼重采样。双眼视差可合成机位附近小范围位移，但实时性压力大，本期不做（见「暂缓」）。

**现有基础：** avox_vulkan 特效管线（177 个 compute shader，uvMap/bulgeDistortion 同类重映射先例）天然适合投影 pass；8K HEVC 硬解链路已通；`setColorSpace/setHdrMeta` 已有"转发 VkVideoRender、不重建 graph"的参数快速通道先例。

---

## 一期：核心播放与交互（约 2 周）

| # | 任务 | 内容 | 估时 |
|---|------|------|------|
| 1 | API 定义 | `AvoxVideo.h` 加 `VrParamet`（投影模式/眼布局/鱼眼参数）+ `ISurfaceRender::enableVr / setViewAngles / disableVr`；视角更新走不重建 graph 快速通道 | 2 天 |
| 2 | 投影 shader | `glsl/` 新增 `vrFisheye180.comp`、`vrEquirect.comp`，UBO 放 yaw/pitch/fov + 圆心/半径/眼别；SBS/OU 布局用枚举覆盖 | 3 天 |
| 3 | 管线挂接 | `VkVideoRender` 加投影 pass，位置与 sizeScale 同级；输出尺寸跟随窗口 | 2 天 |
| 4 | 自动参数 | 宽高比粗猜格式 → 默认 profile（fisheye180：fov=180°、圆内切居中）→ 首帧降采样 Hough 圆检测校准圆心/半径（avox_opencv），按 URL 缓存一次 | 2 天 |
| 5 | 样例交互 | samples 播放器接鼠标拖动/滚轮/方向键，双击重置视角，右键菜单手选投影模式 + fov 微调（±15%，按文件记忆） | 1 天 |
| 6 | 测试 | VR 素材与回归用例进 avox-test 仓（本仓不放测试资产），离线子集加 fisheye180/equirect 各一条 | 1 天 |

**验收：** 8K SBS fisheye180 片源打开即全屏可拖，60fps；格式误判时可右键手选。Windows Vulkan 先行，Android 同代码路径顺带验证。

## 二期：双眼增值（约 3 天）

| # | 任务 | 内容 | 估时 |
|---|------|------|------|
| 7 | 红蓝 3D | `VrOutMode { mono, anaglyph, sbsPreview }`，anaglyph 在投影后双采样合成（R 取左眼、GB 取右眼），配红青眼镜出立体 | 1.5 天 |
| 8 | 立体强度 | 视差缩放滑杆（双眼方向偏移量可调），救过强/过弱片源 | 1 天 |

**验收：** anaglyph 开关即时生效；立体强度调节肉眼可见深浅变化。

## API 草图

```cpp
// AvoxVideo.h (命名随 Anime4KParamet 惯例)
enum class VrProjection { fisheye180, fisheye360, equirect };
enum class VrEyeLayout { sbs, ou };
enum class VrOutMode { mono, anaglyph, sbsPreview };
struct VrParamet {
  VrProjection projection;  // 投影模式, 自动检测可覆盖
  VrEyeLayout eyeLayout;    // 眼布局
  float fisheyeFov;         // 鱼眼镜头 fov, 默认 180
  vec2 center[2];           // 鱼眼圆心(像素), 自动检测填充
  float radius[2];          // 鱼眼圆半径(像素), 自动检测填充
};
// ISurfaceRender
virtual void enableVr(const VrParamet& paramet);       // 挂投影 pass, 重建 graph
virtual void setViewAngles(float yaw, float pitch, float fov);  // 快速通道, 不重建 graph
virtual void setVrOutMode(VrOutMode mode);             // 输出模式(二期)
virtual void disableVr();                              // 摘除 pass
```

手势映射由宿主/样例负责，SDK 只提供视角参数接口。

## 暂缓与不做

- **头显 / OpenXR 输出**：产品定位是平面播放器，不做；shader 保留 eye 参数，未来若做是同一 pass 挂两次
- **六自由度行走**：固定机位内容无位置信息，物理上不可行
- **小范围位移（视差合成）**：双目深度估计 + 重投影技术可行，但实时性压力大，暂缓，需要时再立项评估
- **DX11 原生路径 HLSL 移植**：不做，图像处理只保留 Vulkan 一条路径

## 风险

- **鱼眼数学边缘**：fov 接近 90°、视线扫到半球边界的接缝/翻转，一期预留一天调参
- **圆检测鲁棒性**：非纯黑边、圆不完整的片源，检测失败回退默认 profile（默认值本就覆盖大多数内容）
- **8K 移动端性能**：单 pass 单采样比 Anime4K 轻得多，理论无压力，一期 Android 实测确认
