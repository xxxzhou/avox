# avox

Cross-platform media framework: **capture → GPU process → render → record**.

Third generation of my media engine line:

**oeip** (2019, UE4/Unity multimedia) → **aoce** (2021, GPU image processing, [GPUImage-on-Vulkan port series](https://zhuanlan.zhihu.com/people/zhou-xin-12-70-21/posts)) → **avox** (2026, unified media + GPU pipeline).

## Status

`M0` — scaffold. Public interface skeleton, modern build, CI, test harness.

## Design

- **Backend-neutral GPU frames** — D3D11 / Metal / GLES native textures map into a
  Vulkan-centered processing pipeline without copies (AHardwareBuffer on Android,
  IOSurface on iOS, shared textures on Windows).
- **Push-model sources** — cameras, files and streams implement one source interface;
  the pipeline never knows what produced a frame.
- **Plugins carry capabilities** — device capture, broadcast IO, AI modules load as
  plugins; the core stays small and media-pure.

See [doc/architecture.md](doc/architecture.md) and [plugins/README.md](plugins/README.md).

## Build

```bash
cmake --preset windows   # or: linux / android-arm64
cmake --build build/windows --config Release
ctest --test-dir build/linux --output-on-failure
```

Requirements: CMake 3.24+, C++20 compiler. Vulkan module and plugins are optional
(`AVOX_ENABLE_VULKAN`, `AVOX_ENABLE_PLUGINS`).

## License

[Apache-2.0](LICENSE)
