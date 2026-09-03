# avox architecture

Working design notes for the third generation. Decisions here are earned from two
prior generations (oeip, aoce) — each section states the decision and the reason.

## Goals

1. One media pipeline, five platforms: Windows / Android / iOS / Linux / WebAssembly.
2. GPU data stays on the GPU: decode output → processing → encode/render without
   CPU round-trips.
3. Core stays media-pure; capabilities (devices, broadcast IO, AI) arrive as plugins.
4. A build anyone can reproduce: presets, CI, tests from day one.

## Non-goals (for now)

- Swapping Vulkan for per-platform RHI implementations — Vulkan remains the single
  processing backend; native APIs only present and exchange textures.
- Shipping every filter from aoce — the port happens incrementally, driven by demos.

## Module map

```
core/    interfaces + orchestration (player, tracks, clock)      [this repo, M1]
gpu/     Vulkan pipeline (PipeGraph layers, cross-API interop)    [from aoce lineage]
plugins/ mf camera, decklink, realsense, ndi, ffmpeg demuxer     [tiered roadmap]
engines/ godot GDExtension, unity, unreal wrappers               [thin, after M2]
```

## Key decisions

| # | decision | reason |
|---|----------|--------|
| 1 | GpuFrame is a plain struct + backend enum, not an object graph | crosses plugin/ABI boundaries without ownership traps |
| 2 | Sources push, players pull-queue | one threading model for camera and network sources |
| 3 | One stream-normalization point (NALU formats, config frames) | per-decoder special cases were the #1 bug source last generation |
| 4 | Proprietary SDKs are never vendored | DeckLink/NDI/Rivermax plugins detect a local SDK install |
| 5 | CMake presets + CI + tests are M0 deliverables | build modernization is the first-mile deliverable, not an afterthought |

## Roadmap

| milestone | content |
|-----------|---------|
| M0 | scaffold: interfaces, presets, CI, test harness (this) |
| M1 | vertical slice: ffmpeg source → decode → gpu pipeline → window render |
| M2 | hard-decode interop (AHardwareBuffer / IOSurface / D3D11), Godot wrapper |
| M3 | broadcast plugins (NDI / DeckLink), record/muxer |
