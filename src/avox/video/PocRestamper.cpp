#include "PocRestamper.hpp"

#include "../codec/H264Parse.hpp"
#include "../codec/H265Parse.hpp"
#include "../module/LogHelper.hpp"
#include "../source/PacketBuf.hpp"

namespace avox {

namespace {
// dts 回跳超过该帧数视为 seek 重入, 记账重置重新起量
constexpr int64_t kSeekRewindFrames = 2;
// IRAP 上本轴与容器时间的容许偏差: 正常流只有容器自身毫秒级取整(实测 ≤7ms),
// 超过即判定 seek 落点, 重锚回容器时间轴
constexpr int64_t kRebaseOffMs = 1000;
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
  frameDurUs = fps > 1.0 ? (int64_t)(1000000.0 / fps + 0.5) : 0;
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
  maxFullPoc = 0;
  prevLsb = 0;
  fullPoc0 = 0;
  pts0 = 0;
  pts0Us = 0;
  bPts0 = false;
  lastDts = 0;
  bAnchor = false;
}

void PocRestamper::activate(const char* reason) {
  mode = Mode::active;
  if (!bLogged) {
    log(LogLevel::info, "poc restamp active: ", reason, ", restamping pts by POC");
    bLogged = true;
  }
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
          // VUI 权威声明有重排帧 → 流必然乱序显示, pts==dts 即 ctts 缺失。
          // 据此首帧即预激活, 消掉「等 POC 倒挂实证」期间的接缝错位(激活前已
          // 下发的参考帧保持解码序戳, 显示位实测错后 3 格而非 1ms 级)。
          // 无此声明则维持原样, 仍等倒挂实证; 有 ctts 的正常流下一帧 pts!=dts
          // 即被 bypass 闸拦下, 首帧 dispUnits=0 恒等不改写
          if (sps->vui_parameters_present_flag &&
              sps->vui_seq_parameters.bitstream_restriction_flag &&
              sps->vui_seq_parameters.num_reorder_frames > 0) {
            activate("SPS VUI declares reorder frames (pts==dts)");
          }
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
    maxFullPoc = lsb;
    bAnchor = true;
    return true;
  }
  if (bIrap) {
    // IRAP 起 GOP: 显示位紧接上一 GOP 的**显示序末帧**。末帧必须取本 GOP 的
    // 最大显示位而非解码序末帧——自适应 B 位置下解码序末帧常是显示位靠前的
    // B, 直接 +pocStep 会撞上已占格(重复 pts)并留一个空格, 下游
    // restampFramePts 的单调守卫再把重复值摊成纯 33ms 格 → 每 GOP 一次半帧跳
    fullPoc = maxFullPoc + pocStep;
    prevLsb = lsb;
    maxFullPoc = fullPoc;
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
  if (fullPoc > maxFullPoc) {
    maxFullPoc = fullPoc;
  }
  if (fullPoc < prev && mode == Mode::armed) {
    // 解码序里 POC 倒挂 = B 帧重排实证, 激活注入
    activate("stream reorders but pts==dts (ctts lost)");
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
  // 显示格原点常态只锚首帧——轴绝对均匀(实测 33/34ms 严格交替, 无格点抖动)。
  // 但帧计数轴只在连续播放时与容器时间轴重合: seek 后容器时间大跳、轴却原地
  // 续数, 实测 seek 到 1:24:13 后轴停在 10.4s(容器已到 5051s), 音频被
  // alignPacketPts 拉去追视频 → 持续抖动。故在 IRAP 上校验本轴与容器时间是否
  // 严重不符(正常流两者只差容器自身的毫秒级取整, 实测 ≤7ms), 是则重锚到该
  // IRAP 的容器戳——IRAP 解码位次与显示位次重合, 容器戳即其显示时刻。用 dts
  // 而非 pts 做基准: dts 单调且无 NOPTS 类野值, 是解封装侧最可靠的时钟
  if (bIdr && bPts0 && bSaneDts) {
    const int64_t axisMs = (pts0Us + (fullPoc - fullPoc0) * frameDurUs / pocStep) / 1000;
    const int64_t offMs = packet.dts - axisMs;
    if (offMs > kRebaseOffMs || offMs < -kRebaseOffMs) {
      log(LogLevel::info, "poc restamp rebase: axis off ", offMs,
          " ms (seek), rebase to container time");
      pts0 = packet.dts;
      pts0Us = pts0 * 1000;
      fullPoc0 = fullPoc;
    }
  }
  // 首帧锚定: 显示格基准 = 首帧原始 pts
  if (!bPts0) {
    if (!bSanePts) {
      return false;
    }
    pts0 = packet.pts;
    pts0Us = pts0 * 1000;
    bPts0 = true;
    fullPoc0 = fullPoc;
  }
  // armed 只记账不改写: VFR/无B流(pts==dts且poc单调)永不激活, 时间戳零触碰;
  // 激活帧起 pts = 首帧pts + 显示位*帧长, 激活点接缝由下游 restampFramePts 兜底
  if (mode != Mode::active) {
    return false;
  }
  // 显示格步长按 µs: ms 整数格对 29.97(33.367ms) 每帧欠 0.367ms, 视频钟相对
  // 音频持续落后, 每 ~4.5s 攒满丢帧门限触发静默丢帧追赶 = 周期跳帧。VT 车道
  // 透传包 pts 吃满漂移; FFmpeg 车道输出走 dts 链不吃包 pts 故无感
  const int64_t dispUnits = fullPoc - fullPoc0;
  const int64_t newPtsUs = pts0Us + dispUnits * frameDurUs / pocStep;
  const int64_t newPts = newPtsUs / 1000;
  if (bSanePts && newPts != packet.pts) {
    packet.pts = newPts;
    return true;
  }
  return false;
}

}
