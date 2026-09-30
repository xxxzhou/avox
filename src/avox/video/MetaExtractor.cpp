#include "MetaExtractor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../codec/H26XHelper.hpp"
// vendor 的 FFmpeg DV RPU 解析器门面(见 codec/dovi/README.md):
// 内部头(avcodec.h / libavutil/internal.h 等)只在 codec/dovi/ 里引, 主树不碰。
#include "../codec/dovi/dovi_rpu_wrap.h"

namespace avox {

namespace {

// H.265 NAL 类型: 39=PREFIX_SEI, 40=SUFFIX_SEI, 62=DV RPU
constexpr uint8_t kNalSeiPrefix = 39;
constexpr uint8_t kNalSeiSuffix = 40;
constexpr uint8_t kNalDoviRpu = 62;
// SEI payload type: 137=mastering display colour volume, 144=content light level
constexpr uint32_t kSeiMdcv = 137;
constexpr uint32_t kSeiClli = 144;
// SEI payload type 4 = user_data_registered_itu_t_t35(h264/h265 同值)
constexpr uint32_t kSeiT35 = 4;
// ST2094-10(HDR10+) T.35 头 7B 定长: country + provider(be16) + oriented(be16)
// + app_id + version。provider 0x003C 是三星注册码, HDR10+ 社区工具(hdr10plus_tool)
// 与 ffmpeg(itut35.c)都按 provider==0x3C && oriented==1 && app_id==4 分发
constexpr uint8_t kT35CountryUs = 0xB5;
constexpr uint16_t kT35ProviderHdr10plus = 0x003C;
constexpr uint16_t kT35OrientedCode = 0x0001;
constexpr uint8_t kSt2094App4Id = 4;
constexpr uint8_t kSt2094App4Version = 1;
constexpr int32_t kT35St2094HeaderSize = 7;
// 窗口数域 u(2), 合法 1..3; 多窗时第 2..n 窗的处理窗描述各 153bit, L1 只取帧级
constexpr uint32_t kSt2094MaxWindows = 3;
constexpr int32_t kSt2094WindowBits = 153;
// ST2094-10 L1 17bit 码 = ST2084 PQ 信号 × 100000(工具校验上限 100000)
constexpr float kSt2094L1Scale = 100000.0f;
// DV RPU 首字节 NAL prefix(ff_dovi_rpu_parse 自己校验并跳过)
constexpr uint8_t kRpuNalPrefix = 25;
// RPU 整形曲线段类型: 0=多项式, 其余为 MMR
constexpr int32_t kDoviMappingPolynomial = 0;

// ST 2084 PQ 码值(0..1)逆 EOTF 换亮度 nits(0..10000); 与 FFVDecoder 同式
float pqToNits(float v) {
  if (v <= 0.0f) {
    return 0.0f;
  }
  const float m1 = 2610.0f / 16384.0f, m2 = 2523.0f / 4096.0f * 128.0f;
  const float c1 = 3424.0f / 4096.0f, c2 = 2413.0f / 4096.0f * 32.0f,
              c3 = 2392.0f / 4096.0f * 32.0f;
  const float p = std::pow(v, 1.0f / m2);
  return 10000.0f * std::pow(std::max(p - c1, 0.0f) / (c2 - c3 * p), 1.0f / m1);
}
// 反仿真: 去 RBSP 里 00 00 03 的 03(EPB)。dst 由调用方给出足够空间(src.size 足够)
int32_t unescapeRbsp(const uint8_t* src, int32_t size, std::vector<uint8_t>& dst) {
  dst.resize(size > 0 ? size : 0);
  int32_t out = 0;
  int32_t zeros = 0;
  for (int32_t i = 0; i < size; i++) {
    const uint8_t b = src[i];
    if (zeros >= 2 && b == 0x03) {
      zeros = 0;
      continue;
    }
    zeros = (b == 0) ? zeros + 1 : 0;
    dst[out++] = b;
  }
  return out;
}

// MSB-first 位读器(ST2094-10 位域流): 越界置 over 由调用方判败, 返回 0 兜底
struct BitRd {
  const uint8_t* d;
  int32_t size;      // 字节数
  int32_t pos = 0;   // 位游标
  bool over = false;
  uint32_t u(int32_t n) {
    uint32_t v = 0;
    for (int32_t i = 0; i < n; i++) {
      if (pos >= size * 8) {
        over = true;
        return 0;
      }
      v = (v << 1) | (uint32_t)((d[pos >> 3] >> (7 - (pos & 7))) & 1);
      pos++;
    }
    return v;
  }
};

}  // namespace

void MetaExtractor::reset() {
  lastHdr = {};
  lastDovi = {};
  if (doviCtx) {
    dovi_rpu_ctx_free(doviCtx);
    doviCtx = nullptr;
  }
}

void MetaExtractor::extract(const AvoxPacket& packet) {
  if (codecId != VCodecId::h264 && codecId != VCodecId::h265) {
    return;
  }
  scanNalus(packet);
}

void MetaExtractor::scanNalus(const AvoxPacket& packet) {
  std::vector<AvoxPacket> nalus;
  if (bAvcc) {
    splitAvccNalu(packet, nalus);
  } else {
    splitAnnexbNalu(packet, nalus);
  }
  HdrMeta hdr = {};
  DoviMeta dovi = {};
  bool bHdrHit = false;
  bool bDoviHit = false;
  for (auto& nalu : nalus) {
    const uint8_t* d = nalu.data.data;
    int32_t size = nalu.data.size;
    if (size <= nalu.prefixSize) {
      continue;
    }
    d += nalu.prefixSize;
    size -= nalu.prefixSize;
    if (codecId == VCodecId::h265) {
      if (size < 2) {
        continue;
      }
      const uint8_t nalType = (uint8_t)((d[0] & 0x7E) >> 1);
      if (nalType == kNalDoviRpu) {
        // RPU 载荷须 RBSP(反仿真后); 首字节是 NAL prefix 25, 解析器自己跳
        const int32_t n = unescapeRbsp(d + 2, size - 2, rbspBuf);
        if (n > 0) {
          parseRpu(rbspBuf.data(), n, hdr, dovi);
          bDoviHit = true;
          // parseRpu 命中 L1 时也会把 hdr.l1* 填上
          if (hdr.valid) {
            bHdrHit = true;
          }
        }
      } else if (nalType == kNalSeiPrefix || nalType == kNalSeiSuffix) {
        const int32_t n = unescapeRbsp(d + 2, size - 2, rbspBuf);
        if (n > 0) {
          parseSei(rbspBuf.data(), n, hdr);
          bHdrHit = hdr.valid || bHdrHit;
        }
      }
    } else {  // h264: SEI 无 RPU
      if ((d[0] & 0x1F) != 6) {
        continue;
      }
      const int32_t n = unescapeRbsp(d + 1, size - 1, rbspBuf);
      if (n > 0) {
        parseSei(rbspBuf.data(), n, hdr);
        bHdrHit = hdr.valid || bHdrHit;
      }
    }
  }
  // 无 RPU 帧沿用上次 L1: RPU 每帧应有, 缺帧不回跳静态膝点(防抖)
  if (bDoviHit == false) {
    // dovi 无更新, 保持 hdr.l1 不动(hdr 已由 SEI 填静态)
  }
  if (bHdrHit && hdr.valid) {
    // L1 参与比较: DV 场景切换静态三元组不变、仅 L1 变, 不比则动态膝点失联;
    // HDR10+ 场景切换同此(avg/max 联动)
    if (hdr.maxCLL != lastHdr.maxCLL || hdr.maxLuminance != lastHdr.maxLuminance ||
        hdr.maxFALL != lastHdr.maxFALL || hdr.l1MaxNits != lastHdr.l1MaxNits ||
        hdr.l1AvgNits != lastHdr.l1AvgNits ||
        hdr.l1MinNits != lastHdr.l1MinNits) {
      lastHdr = hdr;
      // 探针走 stderr: 同 VideoTrack 桩(引擎 info 日志在 playtest 不可见)
      fprintf(stderr,
              "[hdrmeta] maxLum=%u cll=%u fall=%u l1max=%.1f l1avg=%.1f "
              "l1min=%.4f\n",
              hdr.maxLuminance, hdr.maxCLL, hdr.maxFALL, (double)hdr.l1MaxNits,
              (double)hdr.l1AvgNits, (double)hdr.l1MinNits);
      if (cb.onHdrMeta) {
        cb.onHdrMeta(hdr);
      }
    }
  }
  if (bDoviHit && dovi.valid) {
    if (memcmp(&dovi, &lastDovi, sizeof(DoviMeta)) != 0) {
      lastDovi = dovi;
      fprintf(stderr, "[hdrmeta] dovi piv=%d/%d/%d\n",
              (int)dovi.comp[0].numPivots, (int)dovi.comp[1].numPivots,
              (int)dovi.comp[2].numPivots);
      if (cb.onDoviMeta) {
        cb.onDoviMeta(dovi);
      }
    }
  }
}

// SEI payload 遍历: 137(mdcv, 24B) / 144(clli, 4B), 字段语义对齐
// AVMasteringDisplayMetadata / AVContentLightMetadata
void MetaExtractor::parseSei(const uint8_t* rbsp, int32_t size, HdrMeta& meta) {
  int32_t i = 0;
  auto rd16 = [&](int32_t o) -> uint16_t {
    return (uint16_t)((rbsp[i + o] << 8) | rbsp[i + o + 1]);
  };
  auto rd32 = [&](int32_t o) -> uint32_t {
    return ((uint32_t)rbsp[i + o] << 24) | ((uint32_t)rbsp[i + o + 1] << 16) |
           ((uint32_t)rbsp[i + o + 2] << 8) | rbsp[i + o + 3];
  };
  while (i + 2 <= size) {
    uint32_t type = 0, paySize = 0;
    while (i < size && rbsp[i] == 0xFF) {
      type += 255;
      i++;
    }
    if (i >= size) {
      break;
    }
    type += rbsp[i++];
    while (i < size && rbsp[i] == 0xFF) {
      paySize += 255;
      i++;
    }
    if (i >= size) {
      break;
    }
    paySize += rbsp[i++];
    if (i + (int32_t)paySize > size) {
      break;
    }
    if (type == kSeiMdcv && paySize >= 24) {
      // 基色序 G,B,R, xy 各 16bit 按 1/50000; 亮度 32bit 按 1e-4 nits
      for (int32_t g = 0; g < 3; g++) {
        meta.primaries[g * 2] = rd16(g * 4) / 50000.0f;
        meta.primaries[g * 2 + 1] = rd16(g * 4 + 2) / 50000.0f;
      }
      meta.whitePoint[0] = rd16(12) / 50000.0f;
      meta.whitePoint[1] = rd16(14) / 50000.0f;
      meta.maxLuminance = rd32(16) / 10000;
      meta.minLuminance = rd32(20) / 10000;
      meta.valid = true;
    } else if (type == kSeiClli && paySize >= 4) {
      meta.maxCLL = rd16(0);
      meta.maxFALL = rd16(2);
      meta.valid = true;
    } else if (type == kSeiT35 && paySize >= kT35St2094HeaderSize) {
      // T.35 载荷首字节 = country code; 非 ST2094-10 签名由内部自滤
      parseHdr10plusL1(rbsp + i, (int32_t)paySize, meta);
    }
    i += (int32_t)paySize;
  }
}

// T.35 user data → HDR10+(SMPTE ST 2094-10 Application 4) L1 还原。
// 位语法(载荷已反仿真, MSB first, 与 hdr10plus_tool/ffmpeg itut35.c 同源):
//   num_windows u(2) [多窗时每窗 153bit 处理窗描述]
//   targeted_display_max_luminance u(27) actual_peak_flag u(1)
//     [flag=1 时: rows u(5) cols u(5) + rows*cols×u(4), v1 流必无, 防御性跳过]
//   逐窗 L1: maxscl 3×u(17) average u(17) num_dist u(4) 每项 u(7)+u(17)
//            fraction_bright_pixels u(10)
//   [其后再是 mastering 标志/逐窗膝点贝塞尔/色饱和映射, L1 不需要, 不读]
// 还原口径: l1Max=各窗 maxscl 峰值, l1Avg=窗0 average, l1Min 恒 0(流内无逐帧
// 最小信号); 17bit 码 = PQ 信号×100000, pqToNits 与 DV L1 同式。
bool MetaExtractor::parseHdr10plusL1(const uint8_t* d, int32_t size,
                                     HdrMeta& meta) {
  if (size < kT35St2094HeaderSize || d[0] != kT35CountryUs) {
    return false;
  }
  const uint16_t provider = (uint16_t)((d[1] << 8) | d[2]);
  const uint16_t oriented = (uint16_t)((d[3] << 8) | d[4]);
  if (provider != kT35ProviderHdr10plus || oriented != kT35OrientedCode ||
      d[5] != kSt2094App4Id || d[6] != kSt2094App4Version) {
    return false;
  }
  BitRd br{d + kT35St2094HeaderSize, size - kT35St2094HeaderSize};
  const uint32_t numWindows = br.u(2);
  if (br.over || numWindows < 1 || numWindows > kSt2094MaxWindows) {
    return false;
  }
  // 位游标直跳处理窗描述段; 越界由后续 u() 的 over 兜住
  br.pos += (int32_t)(numWindows - 1) * kSt2094WindowBits;
  br.u(27);  // targeted_system_display_maximum_luminance(profile A 必 0)
  if (br.u(1) != 0) {
    const uint32_t rows = br.u(5);
    const uint32_t cols = br.u(5);
    if (br.over) {
      return false;
    }
    for (uint32_t k = 0; k < rows * cols && !br.over; k++) {
      br.u(4);
    }
  }
  if (br.over) {
    return false;
  }
  uint32_t maxScl = 0;
  uint32_t avgMaxrgb = 0;
  for (uint32_t w = 0; w < numWindows; w++) {
    for (int32_t c = 0; c < 3; c++) {
      const uint32_t v = br.u(17);
      if (v > maxScl) {
        maxScl = v;
      }
    }
    if (w == 0) {
      avgMaxrgb = br.u(17);
    } else {
      br.u(17);
    }
    const uint32_t nDist = br.u(4);
    for (uint32_t k = 0; k < nDist && !br.over; k++) {
      br.u(7);   // percentage
      br.u(17);  // percentile
    }
    br.u(10);  // fraction_bright_pixels
    if (br.over) {
      return false;
    }
  }
  meta.l1MaxNits = pqToNits((float)maxScl / kSt2094L1Scale);
  meta.l1AvgNits = pqToNits((float)avgMaxrgb / kSt2094L1Scale);
  meta.l1MinNits = 0.0f;
  meta.valid = true;
  return true;
}

void MetaExtractor::parseRpu(const uint8_t* rbsp, int32_t size, HdrMeta& hdr,
                             DoviMeta& dovi) {
  if (size <= 0) {
    return;
  }
  if (!doviCtx) {
    doviCtx = dovi_rpu_ctx_new(dvProfile);
  }
  DoviRpuResult r = {};
  // 调用契约: 传 RBSP(反仿真后), 首字节为 NAL prefix 25
  if (dovi_rpu_parse(doviCtx, rbsp, size, &r) != 0) {
    if (rpuFailLogs < 3) {
      rpuFailLogs++;
      // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
      fprintf(stderr, "[hdrmeta] rpu parse fail size=%d n=%d\n", size,
              rpuFailLogs);
    }
    return;
  }
  if (rpuOkLogs < 1) {
    rpuOkLogs++;
    fprintf(stderr,
            "[hdrmeta] rpu parsed hasL1=%d l1max=%.1f l1min=%.4f hasMap=%d\n",
            r.bHasL1, (double)r.l1MaxNits, (double)r.l1MinNits, r.bHasMapping);
  }
  if (r.bHasL1) {
    hdr.l1MaxNits = r.l1MaxNits;
    hdr.l1MinNits = r.l1MinNits;
    hdr.valid = true;
  }
  if (!r.bHasMapping) {
    return;
  }
  for (int32_t c = 0; c < 3; c++) {
    DoviMeta::Comp& dst = dovi.comp[c];
    dst.numPivots = r.numPivots[c];
    for (int32_t k = 0; k < r.numPivots[c] && k < 9; k++) {
      dst.pivots[k] = r.pivots[c][k];
    }
    for (int32_t k = 0; k < r.numPivots[c] - 1 && k < 8; k++) {
      if (r.mappingIdc[c][k] == kDoviMappingPolynomial) {
        for (int32_t j = 0; j < 3; j++) {
          dst.polyCoef[k][j] = r.polyCoef[c][k][j];
        }
      } else {
        dst.mmrOrder[k] = r.mmrOrder[c][k];
        dst.mmrConstant[k] = r.mmrConstant[c][k];
        for (int32_t j = 0; j < r.mmrOrder[c][k] && j < 3; j++) {
          for (int32_t i2 = 0; i2 < 7; i2++) {
            dst.mmrCoef[k][j][i2] = r.mmrCoef[c][k][j][i2];
          }
        }
      }
    }
  }
  for (int32_t k = 0; k < 3; k++) {
    dovi.nonlinearOffset[k] = r.nonlinearOffset[k];
  }
  for (int32_t k = 0; k < 9; k++) {
    dovi.nonlinear[k] = r.nonlinear[k];
    dovi.linear[k] = r.linear[k];
  }
  dovi.valid = true;
}

}  // namespace avox
