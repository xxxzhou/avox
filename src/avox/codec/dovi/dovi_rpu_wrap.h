// Dolby Vision RPU 解析器门面: 隔离 FFmpeg 内部头, 供 avox 主树调用。
//
// 为什么要有这层: ff_dovi_rpu_parse 需要 DOVIContext 的完整定义(在
// libavcodec/dovi_rpu.h 里), 而该头会裸写 #include "avcodec.h" 并牵出
// libavutil/internal.h 等**未随二进制发布**的内部头。把这些 include 路径挂到
// avox 主树的源文件上是行不通的 —— VS 生成器下源文件级 INCLUDE_DIRECTORIES 是
// **替换**语义, 会顶掉 toolset 默认 <IncludePath>(WinSDK ucrt/shared/um),
// 症状是 <ctime> 报 clock_t 不是全局命名空间成员。
//
// ⇒ 所有引内部头的代码留在本目录, 主树只见本头(纯 POD + 裸函数指针)。
// 详见同目录 README.md。
#pragma once

#include <stdint.h>

// 不透明上下文(内部是 FFmpeg 的 DOVIContext)
typedef void* DoviRpuCtx;

// 一帧 RPU 解析结果。字段与 avox 的 DoviMeta / L1 亮度同位, 由 wrap 侧归一化,
// 调用方无需知道 FFmpeg 的定点表示。
typedef struct DoviRpuResult {
  // L1 (逐帧动态膝点), 已转 nits; bHasL1=0 时无效
  int32_t bHasL1;
  float l1MaxNits;
  float l1MinNits;

  // 整形曲线, 已按位深/系数分母归一
  int32_t bHasMapping;
  int32_t numPivots[3];
  float pivots[3][9];
  int32_t mappingIdc[3][8];
  int32_t mmrOrder[3][8];
  float polyCoef[3][8][3];
  float mmrConstant[3][8];
  float mmrCoef[3][8][3][7];

  // 颜色变换矩阵
  float nonlinearOffset[3];
  float nonlinear[9];
  float linear[9];
} DoviRpuResult;

#ifdef __cplusplus
extern "C" {
#endif

// 建/毁上下文。dvProfile <= 0 时解析器按 HEVC 自行判定。
DoviRpuCtx dovi_rpu_ctx_new(int32_t dvProfile);
void dovi_rpu_ctx_free(DoviRpuCtx ctx);

// 解析一帧 RPU。rbsp 须为**反仿真后**的 RBSP, 首字节是 NAL prefix 25
// (解析器自己校验并跳过)。返回 0 成功, <0 失败。
int32_t dovi_rpu_parse(DoviRpuCtx ctx, const uint8_t* rbsp, int32_t size,
                       DoviRpuResult* out);

#ifdef __cplusplus
}
#endif
