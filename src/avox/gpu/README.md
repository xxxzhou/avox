# gpu

Vulkan-centered image processing pipeline, evolved from the aoce layer system
(PipeGraph / Layer / cross-API interop — see the oeip → aoce lineage in the root README).

Design targets:
- GpuFrame is backend-neutral; D3D11 / Metal / GLES native textures map into the
  Vulkan pipeline with zero copies (AHardwareBuffer on Android, IOSurface on iOS,
  shared textures on Windows).
- Layers compose into a graph: input → effects[] → output, per-platform presentation
  stays thin.
