# Vendored FFmpeg Dolby Vision RPU parser

从 FFmpeg 源码树取出的 Dolby Vision RPU 解析器，编进 libavox 自用。

## 为什么 vendor

`ff_dovi_rpu_parse` 等符号不在 FFmpeg 官方发布的二进制里：

- Windows：`avcodec-63.dll` 是**手筛导出表**（约 159 条），不含 `ff_dovi_*`；
- Android / Linux：`libavcodec.so` 走 `libavcodec/libavcodec.v` 白名单
  （`global: av_*; avcodec_*; avpriv_*; local: *;`）⇒ `ff_dovi_*` 被隐藏；
- darwin / ios：`.a` 静态库可见 `_ff_dovi_rpu_parse`，可直接链接。

⇒ vendor 是三端统一解，不依赖各平台是否导出符号。

## 为什么需要这些「内部头」

上游 `dovi_rpudec.c` 依赖 libavcodec 内部头（`golomb.h` / `get_bits.h` /
`bitstream.h` / `mathops.h` / `vlc.h` / `dovi_rpu.h`），它们**不在** SDK 的
`include/` 里（那里只有公共 API）。因此一并拷进本目录。

两处 shim 是自写的（不是 FFmpeg 原件），原因如下：

| 文件 | 说明 |
|---|---|
| `libavcodec/config.h` | 上游由 FFmpeg 构建生成，**不随二进制发布**。本文件只声明 vendor 三文件真正读到的宏（`ARCH_*` / `HAVE_*` / `CONFIG_SAFE_BITSTREAM_READER` 等），与宿主 FFmpeg 构建解耦。 |
| `libavutil/internal.h` | 上游会 `#include "config.h"` 与 `"libm.h"`（拖入一长串依赖）。vendor 源实际只用到 `avpriv_request_sample`（声明）与 `SUINT` 两个符号，故用最小 shim 替掉。 |

`dovi_rpudec.c` 顶部比上游多一行 `#include "libavutil/internal.h"`：上游靠
`config.h` 传递引入，单独编时缺失。

## ⚠️ 调用纪律（踩过的坑）

**`ff_dovi_rpu_parse` 吃的是 RBSP（反仿真后、`00 00 03` 的 `03` 已剥），不是 raw
Annex-B。** 上游 `hevcdec.c` 传的是 `rpu_nal->data + 2`，其中 `data` = RBSP，
`raw_data` 才是原始流。

喂 raw（含 `00 00 03`）的假象：头部 58 bit 全对，但 mapping 段位错 ⇒
`affected_dm_id = 32` / `num_pivots_minus_2 = 32`（恒为 32，即首个载荷字节 0x80）。
实测 163B raw 反仿真后 151B，立即解析正确。

另：解析器**自己跳过首字节 NAL prefix 25**（`VALIDATE(rpu[0], 25, 25); rpu++`），
调用方须从 NAL 载荷首字节（含 0x19）传入。

## 已实测结论（2026-09-29）

- 编译闭包 = 本目录三个 `.c`：`dovi_rpudec.c` + `dovi_rpu.c` + `golomb.c`，零错误；
- 符号闭包：13 个由 FFmpeg DLL 提供 + 5 个自编译提供 + 0 缺失，可链接；
- 端到端数值对齐（素材 `avox-test/assets/video/test_h265_dv_l1gate_640x360.mkv`，
  30 个 RPU）：FFmpeg 官方路径与 vendor 路径同为
  `source_max_pq=3079 source_min_pq=7` —— 逐位一致。

## ⚠️ 构建接线纪律（踩过的坑）

**引内部头的代码一律留本目录，主树只经 `dovi_rpu_wrap.h` 门面调用。**

原因是两条都试过、两条都炸：

| 做法 | 结果 |
|---|---|
| `include_directories()` 全局追加本目录 | 本目录自带 `libavutil/internal.h` 的极简 shim 会**盖掉**真正的 `libavutil/internal.h`，全库编译崩（实测 7000+ error） |
| 源文件级 `INCLUDE_DIRECTORIES` / `COMPILE_OPTIONS` 挂到主树文件 | VS 生成器下该属性把 `<AdditionalIncludeDirectories>` 变成**替换**语义，顶掉 toolset 默认 `<IncludePath>`（含 WinSDK `ucrt/shared/um`） |

第二种的症状极具误导性 —— 失败点在 `<ctime>` 里：

```
ctime(21,25): error C2039: "clock_t": 不是 "`global namespace'" 的成员
```

看起来像 CRT/工具集版本不匹配（本机装有 14.43 与 14.44 两套，更坐实了这个误判），
实则是 ucrt 路径丢了。实测 `MetaExtractor.cpp` 中招。

⇒ 最终方案：新增 `dovi_rpu_wrap.{h,cpp}`，把 `ff_dovi_rpu_parse` 的调用、
`DOVIContext` 的全部字段读取、定点归一都收进来，只对外暴露 POD 结构
`DoviRpuResult` + 三个裸函数。`MetaExtractor.cpp` 从此不引任何 FFmpeg 内部头。

另：三个 `.c` 与 wrap `.cpp` 仍需**源文件级** include（它们在本目录内，不牵连主树），
且 `.c` 要 `LANGUAGE C`（`add_sub_path` 只收 `.cpp`/`.mm` 且默认按 C++ 编）。
include 顺序：本目录在前（提供自己的 shim），SDK 在后。

## 上游来源

`/d/Work/github/ffmpeg`（libavcodec/ 与 libavutil/）。升级 FFmpeg 时须同步重拷
这些文件，并复核 `config.h` shim 是否需要补宏。
