# [archived] avox M0 设计笔记（导入 avox 前的脚手架）

> 本笔记来自 avox 初始 M0 脚手架（全新重写方案），现已改为以 avox(AVPlay) 代码为底座导入；
> 原始文件见 git 历史（3586d2d 及之前）。设计思路仍可作为后续演进的参考。

# gpu

Vulkan-centered image processing pipeline, evolved from the aoce layer system
(PipeGraph / Layer / cross-API interop — see the oeip → aoce lineage in the root README).

Design targets:
- GpuFrame is backend-neutral; D3D11 / Metal / GLES native textures map into the
  Vulkan pipeline with zero copies (AHardwareBuffer on Android, IOSurface on iOS,
  shared textures on Windows).
- Layers compose into a graph: input → effects[] → output, per-platform presentation
  stays thin.
