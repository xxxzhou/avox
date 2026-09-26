#include "PocRestamper.hpp"

#include "../codec/H264Parse.hpp"
#include "../codec/H265Parse.hpp"
#include "../module/LogHelper.hpp"
#include "../source/PacketBuf.hpp"

namespace avox {

namespace {
// dts 回跳超过该帧数视为 seek 重入, 记账重置重新起量
constexpr int64_t kSeekRewindFrames = 2;
// lsb 回绕周期的合理上限(log2_minus4 <= 12), 防SPS解析出野值
constexpr int64_t kMaxLsbWrap = 1 << 16;
// NOPTS 类野值闸: |pts|/|dts| 超过视为无效时间戳(约34年, 正常流到不了)
constexpr int64_t kPtsSanity = 1LL << 40;
}  // namespace

PocRestamper::PocRestamper() = default;

PocRestamper::~PocRestamper() = default;

void PocRestamper::setup(VCodecId vcodecId, double fps) {
  codecId = vcodecId;
  frameDurMs = fps > 1.0 ? (int64_t)(1000.0 / fps + 0.5) : 0;
  if (codecId == VCodecId::h264) {
    pocStep = 2;  // frame_mbs_only 下相邻显示帧 lsb 差 2
    h264Parse = std::make_unique<H264Parse>();
    h264Parse->enableParseSLICE = true;
  } else if (codecId == VCodecId::h265) {
    pocStep = 1;  // h265 POC 每帧 +1
    h265Parse = std::make_unique<H265Parse>();
  } else {
    mode = Mode::bypass;
  }
}

void PocRestamper::reset() {
  mode = Mode::armed;
  bPocReady = false;
  fullPoc = 0;
  prevLsb = 0;
  fullPoc0 = 0;
  pts0 = 0;
  bPts0 = false;
  lastDts = 0;
  bAnchor = false;
}

// 遍历包内 NAL, 配置帧喂上下文, 首个 VCL 解 slice 头取 poc。
// 多帧包(第二个新帧 VCL)整包只有单一 pts 无法逐帧改写, 放弃本包
bool PocRestamper::processPacketNals(AvoxPacket& packet, bool& bIdr) {
  // 每包自判 avcc/annexb(与 processVideo 的 bvcc 判定同源), 借 split 拿 NAL 视图
  std::vector<AvoxPacket> nalus;
  if (checkAvccPacket(packet.data.data, packet.data.size)) {
    splitAvccNalu(packet, nalus);
  } else {
    splitAnnexbNalu(packet, nalus);
  }
  bIdr = false;
  int32_t frameCount = 0;
  bool bParsed = false;
  for (auto& nal : nalus) {
    if (nal.data.size <= nal.prefixSize) {
      continue;
    }
    if (codecId == VCodecId::h264) {
      // slice 解析需显式开关; 上下文缺失时内部自行失败, 不炸
      if (!h264Parse->parse(nal.data.data, nal.data.size, true)) {
        continue;
      }
      const H264NAL nalType = h264Parse->curUnit->nal;
      if (nalType == H264NAL::NAL_SPS) {
        const H264SpsPtr sps = h264Parse->h264Contex->sps;
        if (sps) {
          // pic_order_cnt_type!=0 无从解回绕显示序, 永久旁路
          if (sps->pic_order_cnt_type != 0) {
            mode = Mode::bypass;
            return false;
          }
          lsbWrap = (int64_t)1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
          if (lsbWrap < 8 || lsbWrap > kMaxLsbWrap) {
            mode = Mode::bypass;
            return false;
          }
          bPocReady = true;
        }
        continue;
      }
      if (nalType != H264NAL::NAL_IDR && nalType != H264NAL::NAL_B_P) {
        continue;  // PPS/SEI/AUD 等
      }
      const H264SliceHeaderPtr slice = h264Parse->curSlice;
      if (!slice || slice->first_mb_in_slice != 0) {
        continue;  // 上下文未备或组内续片
      }
      if (++frameCount > 1) {
        return false;
      }
      bIdr = (nalType == H264NAL::NAL_IDR);
      if (updatePoc((int64_t)slice->pic_order_cnt_lsb, bIdr)) {
        bParsed = true;
      }
    } else {
      h265Parse->parse(nal.data.data, nal.data.size, true);
      const H265NAL nalType = h265Parse->curUnit->nal;
      if (nalType == H265NAL::NAL_SPS) {
        // operator[] 会插入空键, 必须先 count 守卫
        if (h265Parse->h265Contex->spsSet.count(0) != 0) {
          lsbWrap = (int64_t)1 << (h265Parse->h265Contex->spsSet[0]
                                       ->log2_max_pic_order_cnt_lsb_minus4 +
                                   4);
          if (lsbWrap < 8 || lsbWrap > kMaxLsbWrap) {
            mode = Mode::bypass;
            return false;
          }
          bPocReady = true;
        }
        continue;
      }
      const bool bVcl = (uint8_t)nalType <= 21;  // 0~21 为 VCL
      if (!bVcl) {
        continue;  // VPS/PPS/SEI/AUD 等
      }
      const H265SliceHeaderPtr slice = h265Parse->curSlice;
      if (!slice || slice->first_slice_segment_in_pic_flag == 0) {
        continue;
      }
      if (++frameCount > 1) {
        return false;
      }
      // IRAP(16~21)起新 POC 周期
      bIdr = (uint8_t)nalType >= 16;
      if (updatePoc((int64_t)slice->slice_pic_order_cnt_lsb, bIdr)) {
        bParsed = true;
      }
    }
  }
  return bParsed;
}

bool PocRestamper::updatePoc(int64_t lsb, bool bIrap) {
  if (!bPocReady) {
    return false;
  }
  if (!bAnchor) {
    fullPoc = lsb;
    prevLsb = lsb;
    fullPoc0 = lsb;
    bAnchor = true;
    return true;
  }
  if (bIrap) {
    // IRAP 起 GOP: 显示位紧接上一 GOP 末帧, lsb 归零不参与差分
    fullPoc += pocStep;
    prevLsb = lsb;
    return true;
  }
  int64_t d = lsb - prevLsb;
  if (d <= -lsbWrap / 2) {
    d += lsbWrap;
  } else if (d > lsbWrap / 2) {
    d -= lsbWrap;
  }
  prevLsb = lsb;
  const int64_t prev = fullPoc;
  fullPoc += d;
  if (fullPoc < prev && mode == Mode::armed) {
    // 解码序里 POC 倒挂 = B 帧重排实证, 激活注入
    mode = Mode::active;
    if (!bLogged) {
      log(LogLevel::info,
          "poc restamp active: stream reorders but pts==dts (ctts lost), "
          "restamping pts by POC");
      bLogged = true;
    }
  }
  return true;
}

bool PocRestamper::feed(AvoxPacket& packet) {
  if (mode == Mode::bypass || frameDurMs <= 0) {
    return false;
  }
  const bool bSanePts = packet.pts > -kPtsSanity && packet.pts < kPtsSanity;
  const bool bSaneDts = packet.dts > -kPtsSanity && packet.dts < kPtsSanity;
  // seek 自愈: 文件序 dts 明显回跳即重置记账(下一 IDR 前可能解不准, 可接受)
  if (bAnchor && bSaneDts && packet.dts < lastDts - frameDurMs * kSeekRewindFrames) {
    reset();
  }
  // 容器本就有显示时间戳(pts!=dts): 不是丢 ctts 的流, 永久旁路
  if (bAnchor && bSanePts && packet.pts != packet.dts) {
    if (mode == Mode::active) {
      log(LogLevel::warn, "poc restamp: container pts!=dts, stop restamp");
    }
    mode = Mode::bypass;
    return false;
  }
  bool bIdr = false;
  if (!processPacketNals(packet, bIdr)) {
    return false;
  }
  if (!bAnchor) {
    return false;
  }
  if (bSaneDts) {
    lastDts = packet.dts;
  }
  // 锚定首帧原始 pts 作为显示格基准
  if (!bPts0) {
    if (!bSanePts) {
      return false;
    }
    pts0 = packet.pts;
    bPts0 = true;
  }
  // armed 只记账不改写: VFR/无B流(pts==dts且poc单调)永不激活, 时间戳零触碰;
  // 激活帧起 pts = 首帧pts + 显示位*帧长, 激活点接缝由下游 restampFramePts 兜底
  if (mode != Mode::active) {
    return false;
  }
  const int64_t dispUnits = fullPoc - fullPoc0;
  const int64_t newPts = pts0 + dispUnits * frameDurMs / pocStep;
  if (bSanePts && newPts != packet.pts) {
    packet.pts = newPts;
    return true;
  }
  return false;
}

}
