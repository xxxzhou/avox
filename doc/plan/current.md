# 当前计划

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -


本文档记录当前正在进行的开发计划。

## 2026.09 计划 (M1 收口, panvox 双仓对齐)

backlog 全景与逐项施工方案见 [backlog/README.md](backlog/README.md)。

- [ ] A-1 字幕链收尾: playmatrix G 组(真源 avox-test)跑绿 → 编码自愈接线+信号暴露 → ASS 样式/延迟接口 → PGS e2e → [a01](backlog/a01-ass-pgs.md)
- [ ] A-2 秒起播: 先埋指标(打开→首帧/seek→首帧 ms) → probe 降档(留保底字段) → seek 精确化 → [a02](backlog/a02-fast-start-seek.md)
- [ ] A-3 VP9/WEBM: 先复现定性(W38 仓内无记录) → D3D11VA/MediaCodec/VT 按平台接入 → [a03](backlog/a03-vp9-webm.md)
- [ ] A-6 FFmpeg 9.0.1 收口: 适配已完成, 剩五平台回归 + UE 链路验证 → [a06](backlog/a06-ffmpeg9.md)
- [ ] A-5 avox_remote(建议提前): panvox P-4 刮削硬依赖, M1 末期启动 → [a05](backlog/a05-remote-vfs.md)

## 2026.09 计划 (离线超分转码)

实时超分上限不足, 改走 `createRecorder(true)` 离线解码→增强→编码出片 (只走 Real-ESRGAN,
BSD-3-Clause 可商用), 配合 panvox 媒体库「画质增强」挂机任务 → [ai/离线超分转码方案.md](ai/离线超分转码方案.md)

- [ ] P0 avox 管线验证: enhancetest 样例 + 色彩空间透传 + 离屏空输出崩溃复核
- [ ] P1 panvox 集成: pvx_enhance shim (镜像 pvx_aisub job 模型) + Dart 任务队列

## 2026.09 计划 (VR 播放支持)

平面屏 VR 播放（fisheye/equirect 立体片源自动识别 + 拖动视角/缩放 + 红蓝 3D），只走 Vulkan 图像处理路径，不做头显 → [VR播放支持计划.md](VR播放支持计划.md)

- [ ] V-1 一期: API + 投影 shader + 管线挂接 + 自动参数(圆检测) + 样例交互 + 回归用例 (约 2 周)
- [ ] V-2 二期: 红蓝 3D 输出 + 立体强度滑杆 + 画质补偿(FSR/Anime4K) (约 3~4 天)

## 2026.04 计划 (已过期, 待归档)

### WebAssembly 完善

- [ ] WebGL 渲染支持
- [ ] 音频播放支持
- [ ] 性能优化

### 文档维护

- [x] README.md 重写
- [x] CLAUDE.md 更新
- [x] AI 模块文档
- [x] 构建文档更新

## 已归档计划

### 字体渲染接口 (已完成)

```cpp
class IFontLayer {
 public:
  virtual ~IFontLayer() {}

 public:
  // 设置字体信息 fontName 对应 asset/fonts/ 下面的字体文件名
  virtual bool loadFont(const char* fontName, const FontInfo& fontInfo) = 0;
  // 更新字体排版
  virtual void setFontLayout(const FontLayout& fontLayout) = 0;
  // opacity 不透明度，0 完全不透明，1 完全透明
  virtual void setFontColor(float r, float g, float b, float opacity = 0) = 0;
  // 绘制文本
  virtual void drawText(const char* text) = 0;
};

extern "C" {
// 通过 ISurfaceRender 获取 IFontLayer
// 只在 Vulkan 后端可用，其他平台返回 nullptr
AVOX_EXPORT IFontLayer* enableRenderFont(ISurfaceRender* render);
AVOX_EXPORT void disableRenderFont(ISurfaceRender* render);
}
```

此接口已实现，位于 `avox_freetype` 模块。
