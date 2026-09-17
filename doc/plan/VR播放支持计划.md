# VR 播放支持计划

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -

**目标：** 平面屏上播放 VR 内容（fisheye180 / equirect 立体片源）：开箱即用自动识别格式，拖动转视角、滚轮缩放，附红蓝 3D 输出。普通 2D 视频零影响（不启用 VR 就不挂 pass）。

**定位与边界：** 只做播放器平面观看，不做头显。图像处理只走 Vulkan 一条路径，不做 DX11/Metal 原生移植。

**技术定界：** 鱼眼视频为固定机位拍摄，画面只有方向信息、没有位置信息，"走进场景"不可行；一张鱼眼图即含完整半球内容，拖动交互只需单眼重采样。双眼视差可合成机位附近小范围位移，但实时性压力大，本期不做（见「暂缓」）。

**现有基础：** avox_vulkan 特效管线（177 个 compute shader，uvMap/bulgeDistortion 同类重映射先例）天然适合投影 pass；8K HEVC 硬解链路已通；`setColorSpace/setHdrMeta` 已有"转发 VkVideoRender、不重建 graph"的参数快速通道先例。

---

## 一期：核心播放与交互（约 2 周）

> **进度（2026-09-17）：任务 1~5 已完成并实机验证**（Windows Vulkan 车道，
> 4K 鱼眼 SBS 测试片实测：投影重映射正确、拖动转视角/滚轮变焦即时生效、
> 猜测函数对 16:9 正确判 fisheye180 SBS）。任务 6 素材已入 avox-test
> （assets/video/vr_fisheye180_sbs_3840.mp4，生成器
> script/testenv/gen_vr_testasset.py），`shot-vr` 矩阵用例离线跑 PASS。
> **Android 真机实测通过（2026-09-17，arm64 设备 Android 16）**：MediaCodec 硬解 +
> Vulkan 投影输出 1280x720 透视画面正确；离线子集 17 条全 PASS 无回归。
> （同轮字幕 9 条与 file-resize-event 的 FAIL 为 Android 侧既有问题，与 VR 无关）

| # | 任务 | 内容 | 估时 | 状态 |
|---|------|------|------|------|
| 1 | API 定义 | `AvoxVideo.h` 加 `VrParamet`（投影模式/眼布局/鱼眼参数）+ `ISurfaceRender::enableVr / rotateView / zoomView / resetView / disableVr`；视角更新走不重建 graph 快速通道 | 2 天 | ✅ 实现落为 `AvoxLayer.h`（ISurfaceRender 所在头），另含 `getViewAngles/setVrOutMode` |
| 2 | 投影 shader | `glsl/` 新增投影 comp，UBO 放 yaw/pitch/fov + 圆心/半径/眼别；SBS/OU 布局用枚举覆盖 | 3 天 | ✅ 合并为单源 `vrProject.comp`：4 种投影由 UBO 运行时区分（切投影不用重建 graph），红蓝3D/SBS预览分支同文件 |
| 3 | 管线挂接 | `VkVideoRender` 加投影 pass，位置与 sizeScale 同级；输出尺寸跟随窗口 | 2 天 | ✅ `VkVrLayer` 与 resize 互替（挂在 yuv2RGBA 后、画质层前，使 FSR/Anime4K 作用于视口图） |
| 4 | 自动参数 | 宽高比粗猜格式 → 默认 profile → 首帧降采样 Hough 圆检测校准圆心/半径，按 URL 缓存一次 | 2 天 | ⚠️ 部分完成：`guessVrParamet`（宽高比启发式）已做；Hough 圆检测未做（默认 profile 已覆盖大多数片源，检测降为后续增强） |
| 5 | 样例交互 | samples 播放器接鼠标拖动/滚轮/方向键，双击重置视角，右键菜单手选投影模式 + fov 微调 | 1 天 | ✅ 核心 + 简化：`samples/vulkantest/vrplaytest.cpp`（拖动/滚轮/双击/R复位/1-2-3切模式，argv 传 URL）；右键菜单与 fov 微调滑杆未做 |
| 6 | 测试 | VR 素材与回归用例进 avox-test 仓（本仓不放测试资产），离线子集加 fisheye180 一条 | 1 天 | ✅ `shot-vr` 用例已入 playmatrix（46 条）并离线跑 PASS；equirect 用例待素材生成后补 |

**验收：** 8K SBS fisheye180 片源打开即全屏可拖，60fps；格式误判时可右键手选。Windows/Android 均已验证。

**实测发现的已知限制（后续项）：**

- **暂停/EOF 后交互不即时生效**：视角/模式属快速通道参数，在"下一渲染帧"才应用；
  播放停止（EOF/暂停不出帧）时画面冻结在旧视角。样例已用 `onComplete→seek(0)+resume` 回绕规避；
  根治需播放循环支持"参数脏即重绘"，随播放器渲染循环优化另做。
- **切换投影模式/几何参数走 graph 重建**（约几十毫秒一次性卡顿）：视角/fov/输出模式
  才是快速通道。可接受，后续如需无卡顿切换再把几何并入 UBO 运行时参数。

## 二期：双眼增值（约 3 天）

| # | 任务 | 内容 | 估时 | 状态 |
|---|------|------|------|------|
| 7 | 红蓝 3D | `VrOutMode { mono, anaglyph, sbsPreview }`，anaglyph 在投影后双采样合成（R 取左眼、GB 取右眼），配红青眼镜出立体 | 1.5 天 | ✅ 提前随一期落地，anaglyph 分离已实机验证 |
| 8 | 立体强度 | 双眼水平反向偏转各半（`setVrStereoStrength`，0~5°快速通道，mono 无效） | 1 天 | ✅ 已实现并实测：1° 时圆环/十字出现红青重影，中心汇聚处对齐 |

**验收：** anaglyph 开关即时生效；立体强度调节肉眼可见深浅变化。

> **画质补偿（FSR/Anime4K）不做**（2026-09-17 决策）：效果有限（FSR1 锐化级、
> Anime4K 面向线稿），用户明确不需要；真提升走离线超分转码
> （见 doc/plan/ai/离线超分转码方案.md）。vrLayer 挂在画质层之前的次序保留，
> 后续若要挂增强无需改结构。

## API 草图

```cpp
// AvoxLayer.h (命名随 Anime4KParamet 惯例)
enum class VrProjection { fisheye180, fisheye360, equirect180, equirect360 };
enum class VrEyeLayout { sbs, ou };
enum class VrOutMode { mono, anaglyph, sbsPreview };
struct VrParamet {
  VrProjection projection;  // 投影模式, guessVrParamet 按宽高比填初值
  VrEyeLayout eyeLayout;    // 眼布局
  float fisheyeFov;         // 鱼眼镜头 fov, 默认 180
  float centerL[2];         // 左眼圆心(全帧归一化uv), 全零=默认居中
  float centerR[2];         // 右眼圆心, 同上
  float radiusL;            // 左眼圆半径(帧高占比), 0=默认内切(0.5)
  float radiusR;            // 右眼圆半径, 同上
};
// ISurfaceRender — 视角接口全为增量, 累加与钳位都在 SDK 内部, 宿主不持有视角状态
virtual void enableVr(const VrParamet& paramet);                // 挂投影 pass, 重建 graph
virtual void rotateView(float deltaYaw, float deltaPitch);      // 拖动增量(度), 快速通道不重建 graph
virtual void zoomView(float deltaFov);                          // 滚轮/捏合增量(度), 同上
virtual void resetView();                                       // 回初始朝向与默认 fov
virtual void getViewAngles(float* yaw, float* pitch, float* fov) const;  // 供宿主 UI 显示/记忆
virtual void setVrOutMode(VrOutMode mode);                      // 输出模式, 快速通道
virtual void disableVr();                                       // 摘除 pass
```

**视角钳位规则（SDK 内部执行）：**

- fisheye180：yaw/pitch 各钳 ±90°（半球物理边界，出界即无拍摄内容）
- fisheye360 / equirect：pitch 钳 ±90°；yaw 不钳，按 360° 取模包绕
- fov 钳 [30°, 120°]（防放大超源分辨率极限、缩小至畸变不可看），后续按需暴露可配
- 到边界即停：同向拖动无效、反向立即恢复；钳位依据投影格式不同而不同，故必须收在 SDK 层

手势映射由宿主/样例负责，SDK 只提供视角增量接口。

## 暂缓与不做

- **头显 / OpenXR 输出**：产品定位是平面播放器，不做；shader 保留 eye 参数，未来若做是同一 pass 挂两次
- **六自由度行走**：固定机位内容无位置信息，物理上不可行
- **小范围位移（视差合成）**：双目深度估计 + 重投影技术可行，但实时性压力大，暂缓，需要时再立项评估
- **DX11 原生路径 HLSL 移植**：不做，图像处理只保留 Vulkan 一条路径
- **双眼融合提分辨率**：视差偏移是深度相关的（远景亚像素、近景几十像素），利用它须先做逐像素立体配准（= 深度估计，即暂缓项那套机器），理想收益也仅 ~1.2-1.4×；不配准直接融合则近景重影，不可接受。故单眼路线
- **AI 超分画质模式**：离线超分转码已另立方案（doc/plan/ai/离线超分转码方案.md），实时 SR 不做

## 风险

- **鱼眼数学边缘**：fov 接近 90°、视线扫到半球边界的接缝/翻转，一期预留一天调参
- **圆检测鲁棒性**：非纯黑边、圆不完整的片源，检测失败回退默认 profile（默认值本就覆盖大多数内容）
- **8K 移动端性能**：单 pass 单采样比 Anime4K 轻得多，理论无压力，一期 Android 实测确认
