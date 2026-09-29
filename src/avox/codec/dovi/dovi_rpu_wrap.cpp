#include "dovi_rpu_wrap.h"

#include <math.h>
#include <string.h>

#include <libavutil/dovi_meta.h>

// dovi_rpu.h 是纯 C 头且自身没有 extern "C" 守卫: 不包起来的话 C++ 侧会把
// ff_dovi_* 按 C++ 名字修饰去链接, 与 vendor .c 编出的 C 符号对不上(LNK2019)。
extern "C" {
#include "libavcodec/dovi_rpu.h"
}

namespace {

// ST 2084 PQ 码值(0..1)逆 EOTF 换亮度 nits(0..10000)
float pqToNits(float v) {
  if (v <= 0.0f) {
    return 0.0f;
  }
  const float m1 = 2610.0f / 16384.0f, m2 = 2523.0f / 4096.0f * 128.0f;
  const float c1 = 3424.0f / 4096.0f, c2 = 2413.0f / 4096.0f * 32.0f,
              c3 = 2392.0f / 4096.0f * 32.0f;
  const float p = ::powf(v, 1.0f / m2);
  return 10000.0f * ::powf((p - c1) / (c2 - c3 * p), 1.0f / m1);
}

}  // namespace

DoviRpuCtx dovi_rpu_ctx_new(int32_t dvProfile) {
  auto* ctx = new DOVIContext();
  memset(ctx, 0, sizeof(DOVIContext));
  ctx->enable = 1;
  // 容器 DOVI conf 已知则填入(影响 profile/level 判定); 否则解析器按 HEVC 猜
  if (dvProfile > 0) {
    ctx->cfg.dv_profile = (uint8_t)dvProfile;
    ctx->cfg.rpu_present_flag = 1;
    ctx->cfg.bl_present_flag = 1;
    ctx->cfg.dv_md_compression = AV_DOVI_COMPRESSION_NONE;
  }
  return ctx;
}

void dovi_rpu_ctx_free(DoviRpuCtx ctx) {
  if (!ctx) {
    return;
  }
  ff_dovi_ctx_unref((DOVIContext*)ctx);
  delete (DOVIContext*)ctx;
}

int32_t dovi_rpu_parse(DoviRpuCtx ctxHandle, const uint8_t* rbsp, int32_t size,
                       DoviRpuResult* out) {
  if (!ctxHandle || !rbsp || size <= 0 || !out) {
    return -1;
  }
  auto* ctx = (DOVIContext*)ctxHandle;
  // 调用契约: 传 RBSP(反仿真后), 首字节为 NAL prefix 25
  if (ff_dovi_rpu_parse(ctx, rbsp, (size_t)size, 0) < 0) {
    return -1;
  }
  memset(out, 0, sizeof(*out));

  // L1 亮度(逐帧, 12bit PQ): 扩展块里 level==1 的动态块
  if (ctx->ext_blocks) {
    for (int32_t k = 0; k < ctx->ext_blocks->num_dynamic; k++) {
      const AVDOVIDmData& ext = ctx->ext_blocks->dm_dynamic[k];
      if (ext.level == 1) {
        out->l1MaxNits = pqToNits(ext.l1.max_pq / 4095.0f);
        out->l1MinNits = pqToNits(ext.l1.min_pq / 4095.0f);
        out->bHasL1 = 1;
        break;
      }
    }
  }

  const AVDOVIRpuDataHeader* header = &ctx->header;
  const AVDOVIDataMapping* mapping = ctx->mapping;
  const AVDOVIColorMetadata* color = ctx->color;
  if (!mapping || !color || header->bl_bit_depth == 0) {
    return 0;
  }
  const float pivotNorm = 1.0f / (float)((1 << header->bl_bit_depth) - 1);
  const float coefNorm = 1.0f / (float)(1u << (header->coef_log2_denom & 31));
  for (int32_t c = 0; c < 3; c++) {
    const AVDOVIReshapingCurve& src = mapping->curves[c];
    out->numPivots[c] = src.num_pivots;
    for (int32_t k = 0; k < src.num_pivots && k < 9; k++) {
      out->pivots[c][k] = (float)src.pivots[k] * pivotNorm;
    }
    for (int32_t k = 0; k < src.num_pivots - 1 && k < 8; k++) {
      out->mappingIdc[c][k] = src.mapping_idc[k];
      if (src.mapping_idc[k] == AV_DOVI_MAPPING_POLYNOMIAL) {
        for (int32_t j = 0; j < 3; j++) {
          out->polyCoef[c][k][j] = (float)((double)src.poly_coef[k][j] * coefNorm);
        }
      } else {
        out->mmrOrder[c][k] = src.mmr_order[k];
        out->mmrConstant[c][k] =
            (float)((double)src.mmr_constant[k] * coefNorm);
        for (int32_t j = 0; j < src.mmr_order[k]; j++) {
          for (int32_t i = 0; i < 7; i++) {
            out->mmrCoef[c][k][j][i] =
                (float)((double)src.mmr_coef[k][j][i] * coefNorm);
          }
        }
      }
    }
  }
  for (int32_t k = 0; k < 3; k++) {
    out->nonlinearOffset[k] = (float)av_q2d(color->ycc_to_rgb_offset[k]);
  }
  for (int32_t k = 0; k < 9; k++) {
    out->nonlinear[k] = (float)av_q2d(color->ycc_to_rgb_matrix[k]);
    out->linear[k] = (float)av_q2d(color->rgb_to_lms_matrix[k]);
  }
  out->bHasMapping = 1;
  return 0;
}
