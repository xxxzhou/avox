# VT 路径补 RPU/HDR 解析 —— 实现与后续计划（交接文档）

> 状态: **P1 已完成并构建通过**, P2/P3 待做
> 最后更新: 2026-09-29 23:57
> 涉及仓库: `avox`(已改) + `avox-test`(待改)
> 约束: **只改 avox + avox-test**; **不动 FFmpeg 仓**(`/d/Work/github/ffmpeg`)

---

## 0. 一句话背景

硬解腿（D3D11VA / VideoToolbox / MediaCodec / Vulkan）**只解码**，不会把码流里的
Dolby Vision RPU 与 HDR SEI 交出来（FFmpeg 也不会给硬解帧挂 DOVI/HDR side data）。
但 DV 的**动态元数据**（L1 膝点、整形曲线）与 HDR 的**静态元数据**（MDCV/CLLI）
必须拿到，否则：
- 蓝光/HDR 高码率片源在硬解下颜色是错的（L1 缺失 ⇒ tone map 恒按静态峰值）；
- 用户已明确产品口径：**硬解为主、软解只是兜底，且硬解必须解对颜色**。

⇒ 方案：**在喂包处旁路扫描码流，自提 RPU/HDR SEI**，与用哪条解码腿无关。

---

## 1. 已完成的工作（P0 + P1）

### 1.1 P0：NAL 类型表补全

FFmpeg 的 DV RPU 走在 H.265 的 **NAL type 62**，而 avox 的枚举原本只到 40。

| 文件 | 改动 |
|---|---|
| `src/avox/codec/H265Common.hpp` | `enum H265NaluType` 补 `MMP_H265_NALU_TYPE_DOVI_RPU_NUT = 62` 与 `63` |
| `src/avox/codec/H26XHelper.hpp` | `AVOX_MAP_H265_NAL` 宏原本从 40 直接跳到 63，补上 `NAL_DOVI_RPU` |

### 1.2 P1 核心：`MetaExtractor` —— 解码器无关的元数据提取器

**新增文件**

| 路径 | 行数 | 作用 |
|---|---|---|
| `src/avox/video/MetaExtractor.hpp` | 73 | 类声明 + `Callbacks{onHdrMeta, onDoviMeta}` |
| `src/avox/video/MetaExtractor.cpp` | 256 | 实现：NAL 拆分 → SEI/RPU 解析 → 去重 → 回调 |

**关键设计点**

- **`extract(packet)`** 在**喂包时**扫（不是解码后），拿的是原始码流；
- `scanNalus` 按 `bAvcc` 走 `splitAvccNalu` / `splitAnnexbNalu`；
- h265：判 NAL 62 走 `parseRpu`，判 NAL 39/40 走 `parseSei`；h264：判 NAL 6 走 `parseSei`；
- **去重闸**：`lastHdr` / `lastDovi` 与上次比较后才回调（避免每帧重复派发）；
  - HdrMeta 比 `maxCLL/maxLuminance/maxFALL/l1MaxNits/l1MinNits` —— **L1 必须参与比较**，
    否则 DV 场景切换时（静态三元组不变、仅 L1 变）会丢动态膝点；
  - DoviMeta 用 `memcmp`；
- `reset()` 清去重状态 + 释放 DV 上下文（会话边界：seek / 重开流）。

### 1.3 ⭐ 新增 vendor 门面：`dovi_rpu_wrap.{h,cpp}`

**这是本轮最重要的架构决定**，见 §3 的坑 2。

FFmpeg 的 `ff_dovi_rpu_parse` **不在任何平台的发布导出表里**（Windows 是手筛导出、
Unix 走 `libavcodec.v` 白名单），所以必须把源码 vendor 进 avox。

```
src/avox/codec/dovi/                    (自建子树, 141K)
├── dovi_rpu_wrap.h                     ← 主树唯一可见的门面
├── dovi_rpu_wrap.cpp                   ← 在这里引内部头
├── README.md                           ← 完整踩坑记录
├── libavcodec/
│   ├── dovi_rpudec.c  dovi_rpu.c  golomb.c    (FFmpeg 原件)
│   ├── dovi_rpu.h  golomb.h  get_bits.h  bitstream.h
│   │   mathops.h  vlc.h                        (内部头, SDK 不发布)
│   └── config.h                                (**自写 shim**)
└── libavutil/
    ├── internal.h                              (**自写 shim**)
    └── attributes_internal.h
```

**门面接口**（`dovi_rpu_wrap.h`）：

```c
typedef void* DoviRpuCtx;                 // 内部是 FFmpeg 的 DOVIContext

typedef struct DoviRpuResult {
  int32_t bHasL1; float l1MaxNits, l1MinNits;
  int32_t bHasMapping;
  int32_t numPivots[3];
  float   pivots[3][9];
  int32_t mappingIdc[3][8];
  int32_t mmrOrder[3][8];
  float   polyCoef[3][8][3];
  float   mmrConstant[3][8];
  float   mmrCoef[3][8][3][7];
  float   nonlinearOffset[3];
  float   nonlinear[9];
  float   linear[9];
} DoviRpuResult;

DoviRpuCtx dovi_rpu_ctx_new(int32_t dvProfile);
void      dovi_rpu_ctx_free(DoviRpuCtx ctx);
int32_t   dovi_rpu_parse(DoviRpuCtx ctx, const uint8_t* rbsp, int32_t size,
                         DoviRpuResult* out);
```

定点归一（pivot 除 `2^bl_bit_depth-1`、系数除 `2^coef_log2_denom`）全部收在
wrap.cpp 里，主树只见 float。

**为什么需要两个 shim**（都不是 FFmpeg 原件）：

| 文件 | 原因 |
|---|---|
| `libavcodec/config.h` | 上游由 FFmpeg 构建生成，**不随二进制发布**。自写最小版只声明 vendor 三文件真正读到的宏（`ARCH_*` 全 0 / `HAVE_*` / `CONFIG_SAFE_BITSTREAM_READER`）。`ARCH_*` 在这批文件里**唯一作用**是决定 include 哪个 arch 专属 `mathops.h` —— 关掉即走 generic C，因此 `x86/` 子目录已删。 |
| `libavutil/internal.h` | 上游会 `#include "config.h"` 与 `"libm.h"`，拖出一长串依赖。vendor 源实际只用到 `avpriv_request_sample`（声明）与 `SUINT` 两个符号，故用最小 shim 替掉。 |

### 1.4 全链路接线：`dvProfile` 从容器到解析器

```
IOParseFF.cpp:1055   AV_PKT_DATA_DOVI_CONF → dvProfile          (原有)
  → VTrackDesc.dvProfile
  → VideoTrack::setTrackDesc  → dvProfile = trackDesc.dvProfile (本轮补, 原先被丢弃)
  → VDecoderTask.cpp:91 / :226  cand->setDvProfile(...)          (本轮补, 两处建候选)
  → VideoDecoder::setDvProfile
  → MetaExtractor::setDvProfile
  → dovi_rpu_ctx_new(dvProfile)                                  (填进 ctx->cfg)
```

`dvProfile <= 0` 时解析器按 HEVC 自行判定（不会出错，只是可能判错 profile）。

### 1.5 硬解腿接入点（关键结论）

`MetaExtractor` 的调用在 **`VideoDecoder::decoderImp()` 开头**（格式转换之前）：

```cpp
DecodeResult VideoDecoder::decoderImp(AvoxPacket& vdata) {
  // 元数据旁路扫描: HDR SEI / DV RPU 与解码器无关, 硬解腿也必须在此自提
  if (metaExtractor) {
    metaExtractor->setAvcc(bvcc);
    metaExtractor->extract(vdata);
  }
  ...
```

**Windows 硬解腿已自动接入** —— `Dx11VDecoder : public VideoDecoder`
（`src/avox_windows/dx11/Dx11VDecoder.hpp:15`），且**它没有 override `decoderImp`**
（只 override 了 `onPreDecoder`），所以走的就是基类那条路径。

回调经 `dispatch(&IVideoDecoderOb::onHdrMeta / onDoviMeta)` 派发到 `VideoTrack`
→ `WindowRender`，消费端**零改动**。

---

## 2. 构建状态与验证

### 2.1 构建结果（2026-09-29 23:52）

| 项 | 值 |
|---|---|
| 产物 | `avox/build/windows/avox/install/AMD64/Release/avox.dll` |
| 大小 | 5,275,648 B |
| 错误 | **0**（编译 + 链接均通过） |
| 已部署到 panvox | ✅ md5 `07ac00663e0845995b623a988aba7da3` 一致 |
| shim | `panvox_native.dll` 重编并同步（23:56） |

**决定性证据**：链接零 `LNK2019` ⇒ `ff_dovi_rpu_parse` / `ff_dovi_ctx_unref` 的引用
由 vendor 的 `.c` 满足了（否则会像中途那轮一样报「无法解析的外部符号」）。

### 2.2 ⚠️ 尚未做的验证：硬解腿端到端

**这是 P1 的收尾项，也是 P2/P3 的前置**。

素材：`avox-test/assets/video/test_h265_dv_l1gate_640x360.mkv`（30 个 RPU）
参考值：`source_max_pq = 3079` / `source_min_pq = 7`（FFmpeg 官方路径已逐位对齐）

需要做的：

1. **加探针**。`MetaExtractor.cpp` 里**目前没有任何日志/探针**，无法从日志判断是否命中。
   建议加一行（`AVOX_DEBUG` 或 LogLevel::info 守卫）：

   ```cpp
   // 在 scanNalus 派发回调处
   LOGFLF(LogLevel::info, "[dovi] dispatch valid=", (int)dovi.valid,
          " l1max=", hdr.l1MaxNits, " l1min=", hdr.l1MinNits,
          " hdr=", (int)hdr.valid);
   ```

2. **跑硬解腿**。`PlayCase::hardDecode` 默认 `true`（`PlayCases.hpp:161`），但
   DV 三个用例**刻意设成 `false` 走软解**：

   | 用例 | 行号 | 当前 |
   |---|---|---|
   | `dv-l1gate` | `PlayCases.hpp:502` | `hardDecode = false` |
   | `dv-l1base` | `PlayCases.hpp:516` | `hardDecode = false` |
   | `dv-l1var` | `PlayCases.hpp:535` | `hardDecode = false` |

   要验硬解需新开用例或改这三个标志（改前先确认软解腿仍绿，避免混淆归因）。

3. **判据**：探针命中 + `l1max` 与参考值一致（3079/7 那一档）+ `onHdrMeta` 也来
   （MDCV/CLLI 两条 SEI）。

---

## 3. ⚠️ 构建接线纪律（踩过的坑，务必先读）

这一节是本次**最耗时**的部分。三条都实际炸过。

### 坑 1：`build_common.py` 的 `-G` 双引号 —— 只有 FORCE 重配才炸

```
CMake Warning: Ignoring extra path from command line: "2022"
CMake 配置失败，错误码: 1
```

根因在 `build_common.py:419`：

```python
cmake_args += ["-A", AVOX_TARGET_ARCH, "-G", f"\"{AVOX_WIN_VS_VERSION}\""]
```

内嵌引号是配合旧 `os.system` 拼串写的；后来 `build_module()` 加了
「参数含空格就再包一层引号」（`build_common.py:178`）⇒ 变成
`-G ""Visual Studio 17 2022""` ⇒ cmake 把内层引号当初级参数解析。

**⚠️ 这是公共脚本的缺陷，本次未擅自修改。** 需要你决定是否修（去掉那对内嵌引号即可）。

绕过办法：手工跑一次 configure，之后 build 就正常。

### 坑 2：VS 生成器的源文件级 `INCLUDE_DIRECTORIES` 是**替换**语义

`MetaExtractor.cpp` 原本直接 `#include ".../dovi_rpu.h"`（FFmpeg 内部头），
结果两条路都炸：

| 做法 | 结果 |
|---|---|
| `include_directories()` **全局**追加 vendor 目录 | vendor 自带的 `libavutil/internal.h` shim **盖掉**真头 ⇒ **7000+ error 全库崩** |
| **源文件级** `INCLUDE_DIRECTORIES` / `COMPILE_OPTIONS` 挂到主树文件 | VS 下该属性**替换** `<AdditionalIncludeDirectories>`，顶掉 toolset 默认 `<IncludePath>`（WinSDK ucrt/shared/um） |

第二种症状极具误导性 —— 失败点在 `<ctime>`：

```
ctime(21,25): error C2039: "clock_t": 不是 "`global namespace'" 的成员
ctime(21,13): error C2873: “clock_t”: 符号不能用在 using 声明中
```

本机装着 **14.43 与 14.44 两套 MSVC**，把误判引向「工具集版本不匹配」，实际根因是
**ucrt 路径丢了**。

**判据（30 秒定案）**：拿一份最小 `int main(){}` 也编不过 + INCLUDE 打印正常
⇒ 去看命令行有没有 `/I`。**MSVC 的 `/I` 是覆盖 `INCLUDE` 环境变量而非追加** ——
命令行只要出现任何一个 `/I`，整条 `INCLUDE` 就被忽略。

**最终解法**：新增 C 风格门面（§1.3）。引内部头的代码全部留在 `codec/dovi/` 目录内
（那里用源文件级 include 是安全的，只影响本目录文件），主树只见 POD 结构。

CMake 侧正确写法：

```cmake
# src/CMakeLists.txt
if(AVOX_ENABLE_FFMPEG)
  set(AVOX_DOVI_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/avox/codec/dovi")
  list(APPEND AVOX_SOURCE
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/dovi_rpudec.c
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/dovi_rpu.c
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/golomb.c
    ${AVOX_DOVI_VENDOR_DIR}/dovi_rpu_wrap.cpp)
  # ...
  set(AVOX_DOVI_VENDOR_INCLUDES
       "${AVOX_DOVI_VENDOR_DIR}" "${AVOX_DOVI_VENDOR_DIR}/libavcodec")
  foreach(_d ${FFMPEG_INCLUDE_DIRS})
    list(APPEND AVOX_DOVI_VENDOR_INCLUDES
         "${_d}/libavcodec" "${_d}/libavutil" "${_d}")
  endforeach()
  set_source_files_properties(   # ⚠️ 只挂本目录的文件!
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/dovi_rpudec.c
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/dovi_rpu.c
    ${AVOX_DOVI_VENDOR_DIR}/libavcodec/golomb.c
    PROPERTIES LANGUAGE C INCLUDE_DIRECTORIES "${AVOX_DOVI_VENDOR_INCLUDES}")
  set_source_files_properties(
    ${AVOX_DOVI_VENDOR_DIR}/dovi_rpu_wrap.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${AVOX_DOVI_VENDOR_INCLUDES}")
endif()
```

### 坑 3：`dovi_rpu.h` 没有 `extern "C"` 守卫 ⇒ LNK2019

```
error LNK2019: 无法解析的外部符号
  "void __cdecl ff_dovi_ctx_unref(struct DOVIContext *)"
  (?ff_dovi_ctx_unref@@YAXPEAUDOVIContext@@@Z)
```

看**修饰后的名字**：`?xxx@@YA...` 是 C++ 修饰。FFmpeg 的内部头是纯 C 头且**自身没有
`extern "C"` 守卫**。修法（已落在 `dovi_rpu_wrap.cpp`）：

```cpp
extern "C" {
#include "libavcodec/dovi_rpu.h"
}
```

### 坑 4（补充）：加源文件后必须 FORCE 重配

`add_sub_path()`（`cmake/AVOXHelper.cmake:45`）用 `file(GLOB *.cpp *.mm)` 收源码，
**GLOB 在 configure 期展开**。而 `build_windows.py` 默认增量，只 `cmake --build`，
**不重跑 configure** ⇒ 新文件进不了构建图，表现为「构建成功、零错误、但新代码没进去」。

另：`add_sub_path` **只收 `.cpp`/`.mm`，不收 `.c`** —— 纯 C 源码必须单独列出并设
`LANGUAGE C`。

### 坑 5（补充）：公共头改动会牵连 shim

`src/avox/Avox*.h`（公共导出头）改动会进 `install/include`，被 panvox 的 shim 消费。
只改内部头不用管；改了公共头 + 增量构建 ⇒ `install/include` 不刷 ⇒ shim 按旧头编
⇒ **虚表错位崩（0xC0000005 级）**。

判据：`cmp install/include/avox/X.h src/avox/X.h`（**别只看 mtime**，工具环境里
mtime 不可靠，`deploy_runtime.ps1` 的 stale 告警曾误报）。

本次未改任何公共头，`AvoxVideo.h`/`AvoxCodec.h`/`AvoxDef.h` 三个 `cmp` 全 SAME。

---

## 4. 正确的构建流程（可直接抄）

```bash
# ① 改了源码(尤其加了新文件) → 必须手工重配, 绕开 build_common.py 的 -G 缺陷
cd /d/Work/github/avox/build/windows/avox
MSYS_NO_PATHCONV=1 cmake ../../../ -DCMAKE_BUILD_TYPE=Release \
    -DAVOX_DIST_FLAVOR=commercial -A x64 -G "Visual Studio 17 2022"

# ② 构建
MSYS_NO_PATHCONV=1 cmake --build . --config Release --parallel

# ③ 查错误(判据: 0 表示通过; LNK2019 = 符号没链上)
grep -cE "error C[0-9]|error LNK|fatal error" /tmp/build.log

# ④ 部署到 panvox
cd /d/Work/github/panvox
#  用 PowerShell 工具跑(不是 Bash):
#  & powershell -ExecutionPolicy Bypass -File tools\deploy_runtime.ps1 -Config Release

# ⑤ 验证哈希一致
md5sum /d/Work/github/avox/build/windows/avox/install/AMD64/Release/avox.dll \
       /d/Work/github/panvox/app/build/windows/x64/runner/Release/avox.dll
```

**shim 会撞 `MSB6001: 已添加项。字典中的关键字:"Path"…"PATH"`**（工具环境同时有
`Path` 与 `PATH`）。绕过：纯 Python `subprocess` 传归一化 env（大小写去重），
脚本见 `%TEMP%\build_shim.py` 的思路，或改用非工具环境跑。

**语法检查工具**（省一次全量构建）：
```bash
cd /d/Work/github/avox
python tools/syntax_check_msvc.py src/avox/video/MetaExtractor.cpp
```
（本轮新建，含 `sdk_includes()` 修好的 `/I` 覆盖问题与 `/DWIN32`）

---

## 5. 后续计划（P2 → P5）

### P2｜iOS / macOS 接入 + 去豁免  ← **用户指定先 mac 测, 不用 iOS**

```bash
ssh mac        # 已配免密
```

| 步骤 | 说明 |
|---|---|
| 2.1 | 确认 `src/avox_apple/IOSVDecoder.mm` 是否 override 了 `decoderImp`。**若没 override，则自动已接入**（同 Windows） |
| 2.2 | 若 override 了，在它的实现开头补 `metaExtractor` 调用（样式同 `VideoDecoder::decoderImp`） |
| 2.3 | mac 构建：`cd /Volumes/PSSD/work/github/avox && python3 build_mac.py`（默认全量，慢但稳） |
| 2.4 | **⚠️ mac 出包位 ≠ 安装位**：必须 `ditto` 到 `~/Applications/panvox.app`（先退在跑实例），测功能一律测装机位。构建树 products 只是出包位。 |
| 2.5 | 去 avox-test 的 `flatOk` 豁免（`PlayCases.hpp:242` 定义 / `:726` 使用处），把 `dv-shot-*` 改成硬断言 |
| 2.6 | 跑 `dv-l1gate` / `dv-l1base` / `dv-l1var`，判据 `dv_p8_scene_diff.py --expect-l1-change` |

### P3｜Android 接入

| 步骤 | 说明 |
|---|---|
| 3.1 | 同 2.1/2.2，查 `src/avox_android/AndVDecoder.cpp` 是否 override `decoderImp` |
| 3.2 | **先核导出表**：用户说 avcodec 是今天用 ffmpeg9 新编的，需确认 `libavcodec.so` 是否仍走 `libavcodec.v` 白名单（若是，vendor 路线依然必需，本次已做） |
| 3.3 | 构建：`python build_android.py`（NDK 26.1.10909125，见 `build_common.py:29`） |

### P4｜（可选）软解腿反向复用去重

`src/avox_ffmpeg/decoder/FFVDecoder.cpp`（注意是 `decoder/` 子目录）里已有自己的
元数据实现 —— 那是本次 `MetaExtractor` 的**参照实现**，逻辑重复：

| FFVDecoder.cpp | MetaExtractor.cpp | 说明 |
|---|---|---|
| `parseHdrSideData`（:209） | — | 侧数据路径，硬解腿拿不到，只软解用 |
| `fillDoviMeta`（:260） | `parseRpu` 后半段 | **逐字段同口径**（pivot 除 `2^bl-1`，系数除 `2^coef_log2_denom`） |
| `parseHdrSeiRbsp`（:303） | `parseSei` | SEI 137/144，语义一致 |
| `updateHdrMeta` 附近（:470-485） | `scanNalus` 尾部 | **去重逻辑也几乎一样**（`memcmp` DoviMeta、无 RPU 帧沿用上次 L1） |

可考虑让 `FFVDecoder` 也复用 `MetaExtractor`，删掉重复代码。

**建议：只在 P1-P3 全绿后再做**，避免动软解腿引入回归。

### P5｜三平台回归 + 报告

含 Windows。产出回归报告，并修订 **panvox 仓**（不是 avox 仓）的
`docs/adr-0009-hdr-strategy.md` §4 —— 该文档目前对硬解腿能否拿到 DV 元数据的
描述与现状不符。

---

## 6. 两条贯穿全程的硬纪律

### 纪律 1：`ff_dovi_rpu_parse` 吃的是 **RBSP**，不是 raw Annex-B

上游 `hevcdec.c` 传的是 `rpu_nal->data + 2`，其中 `data` = **RBSP**（`00 00 03` 的
`03` 已剥），`raw_data` 才是原始流。

**喂 raw 的假象**：头部 58 bit 全对（EP 在头之后），但 mapping 段**位错** ⇒
`affected_dm_id = 32` / `num_pivots_minus_2 = 32`（恒为 32，即首个载荷字节 0x80）。

实测 163B raw 反仿真后 151B，立即解析正确。

另：解析器**自己跳过首字节 NAL prefix 25**（`VALIDATE(rpu[0], 25, 25); rpu++`），
调用方须从 NAL 载荷首字节（含 `0x19`）传入。

> `MetaExtractor::scanNalus` 里的 `unescapeRbsp()` 就是干这个的，已正确实现。

### 纪律 2：只改 avox + avox-test，不动 FFmpeg 仓

`/d/Work/github/ffmpeg` 是**独立仓**，本次全程未碰。vendor 的文件是从那里**拷出来**
的副本 —— 升级 FFmpeg 时需同步重拷并复核 `config.h` shim 是否需要补宏。

---

## 7. 交付物清单

### 已改（avox，未提交）

```
 M src/CMakeLists.txt                       DOVI vendor 段
 M src/avox/codec/H265Common.hpp            NAL 62/63
 M src/avox/codec/H26XHelper.hpp            NAL_DOVI_RPU 宏
 M src/avox/player/VideoTrack.cpp           dvProfile 落成员
 M src/avox/player/VideoTrack.hpp           dvProfile 成员 + getter
 M src/avox/video/VDecoderTask.cpp          两处 setDvProfile
 M src/avox/video/VideoDecoder.cpp          metaExtractor 集成 + decoderImp 调用
 M src/avox/video/VideoDecoder.hpp          metaExtractor 成员
?? src/avox/codec/dovi/                     vendor 子树(141K, 含 README)
?? src/avox/video/MetaExtractor.{hpp,cpp}   提取器
?? tools/syntax_check_msvc.py               MSVC 单文件语法检查
```

（另有 `?? NUL.obj` 是构建残留的垃圾文件，不是本次产物，可删。）

### 待改（avox-test）

- `l1_avox/playmatrix/PlayCases.hpp`：`dv-l1gate` / `dv-l1base` / `dv-l1var` 去
  `hardDecode=false`，并去 `flatOk` 豁免。

### 待加

- `MetaExtractor.cpp` 的探针（见 §2.2）。

---

## 8. 参考

- 踩坑全文：`src/avox/codec/dovi/README.md`
- 上游来源：`/d/Work/github/ffmpeg`（`libavcodec/` 与 `libavutil/`）
- 素材生成脚本：`avox-test/assets/gen/gen_dv_l1gate.py`
- 差分判据：`avox-test/.../dv_p8_scene_diff.py --expect-l1-change`
- 相关 ADR：`panvox/docs/adr-0009-hdr-strategy.md`（§4 待修订；**在 panvox 仓**）
- 参照实现：`src/avox_ffmpeg/decoder/FFVDecoder.cpp`（软解腿的同类逻辑）
- 技能沉淀：`~/.workbuddy-ai/skills/avox-windows-build-triage/SKILL.md`
  （含 `build_common.py` -G 缺陷、`/I` 覆盖 INCLUDE、VS 源文件级 include 替换语义）
